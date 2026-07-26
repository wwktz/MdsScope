// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

namespace mds_client_internal {

using Message = MdsIpClient::Message;

QByteArray MdsIpClient::message(qint8 dtype, qint8 nargs, qint8 descrIdx, quint8 messageId, const QByteArray& body)
{
        QByteArray packet;
        QDataStream out(&packet, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << qint32(48 + body.size());
        out << qint32(0);
        out << qint16(body.size());
        out << nargs;
        out << descrIdx;
        out << qint8(messageId);
        out << dtype;
        out << qint8(0xC3);
        out << qint8(0);
        for (int i = 0; i < 8; ++i) {
            out << qint32(0);
        }
        packet.append(body);
        return packet;
    }

bool MdsIpClient::writeMessage(QTcpSocket& socket, const QByteArray& packet, QString* error)
{
        // Never start another MDSIP request after cancellation. Thin/Medium may
        // finish draining the response that was already in flight, but fallback
        // paths must not write a new request afterwards. Full cancellation may
        // already have aborted the socket in readFully().
        if (currentCanceled()) {
            *error = "operation canceled";
            return false;
        }
        if (socket.state() != QAbstractSocket::ConnectedState || !socket.isOpen()) {
            *error = socket.errorString().isEmpty()
                         ? QStringLiteral("MDS socket is not connected")
                         : socket.errorString();
            return false;
        }
        if (socket.write(packet) != packet.size()) {
            *error = socket.errorString();
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        while (socket.bytesToWrite() > 0) {
            if (shouldAbortForCurrentCancel()) {
                socket.abort();
                *error = "operation canceled";
                return false;
            }
            if (!socket.waitForBytesWritten(50)) {
                if (socket.state() != QAbstractSocket::ConnectedState) {
                    *error = socket.errorString();
                    return false;
                }
                if (timer.elapsed() >= kNetworkTimeoutMs) {
                    // A timed-out socket is desynced from the peer; abort it so it
                    // cannot be reused as a warm connection with a half-written
                    // request still pending.
                    *error = socket.errorString();
                    socket.abort();
                    return false;
                }
            }
        }
        return true;
    }

bool MdsIpClient::readFully(QTcpSocket& socket,
                            QByteArray* out,
                            qsizetype size,
                            bool knownResponseBody,
                            QString* error)
{
        const int idleTimeoutMs = currentReadIdleTimeoutMs();
        QElapsedTimer idleTimer;
        idleTimer.start();
        QElapsedTimer transferTimer;
        transferTimer.start();
        QElapsedTimer cancelDrainTimer;
        bool cancelDrainLogged = false;
        out->reserve(size);
        while (out->size() < size) {
            // Interactive Thin/Medium refreshes ask us to preserve connections
            // when switching shots. Finish consuming the one response already
            // in flight so the socket reaches a clean protocol boundary.
            //
            // Reads without an idle deadline are adaptive on cancellation:
            // preserve the socket only when the observed transfer rate predicts
            // that its remaining body will arrive faster than rebuilding an
            // MDSIP connection. Otherwise abort immediately. A header wait has
            // no known body size or useful throughput sample, so it remains
            // immediately abortable.
            if (currentCanceled()) {
                bool canDrainCurrentResponse =
                    currentPreserveConnectionsOnCancel() && idleTimeoutMs > 0;
                if (currentPreserveConnectionsOnCancel() && idleTimeoutMs == 0 && knownResponseBody) {
                    if (!cancelDrainTimer.isValid()) {
                        cancelDrainTimer.start();
                    }
                    const qsizetype remaining = size - out->size();
                    const qsizetype buffered =
                        std::min(remaining, static_cast<qsizetype>(socket.bytesAvailable()));
                    const int reconnectMs =
                        std::clamp(reconnectCostEstimateMs(),
                                   250,
                                   kConnectionSetupTimeoutMs);
                    bool predictedFasterThanReconnect = buffered >= remaining;
                    qint64 predictedRemainingMs = 0;
                    if (!predictedFasterThanReconnect && out->size() == 0 && buffered > 0) {
                        // Consume one already-buffered chunk to obtain a real
                        // throughput sample before choosing drain vs abort.
                        predictedFasterThanReconnect = true;
                    } else if (!predictedFasterThanReconnect && out->size() > 0) {
                        const qint64 elapsedMs = std::max<qint64>(1, transferTimer.elapsed());
                        const long double estimate =
                            static_cast<long double>(remaining - buffered)
                            * static_cast<long double>(elapsedMs)
                            / static_cast<long double>(out->size());
                        predictedRemainingMs =
                            static_cast<qint64>(std::min<long double>(estimate, 60'000.0L));
                        predictedFasterThanReconnect = predictedRemainingMs <= reconnectMs;
                    }
                    canDrainCurrentResponse =
                        predictedFasterThanReconnect && cancelDrainTimer.elapsed() < reconnectMs;
                    if (!cancelDrainLogged || !canDrainCurrentResponse) {
                        traceMdsLine(
                            QString("cancel_drain keep=%1 remaining=%2 buffered=%3 predicted_ms=%4 reconnect_ms=%5")
                                .arg(canDrainCurrentResponse ? 1 : 0)
                                .arg(remaining)
                                .arg(buffered)
                                .arg(predictedRemainingMs)
                                .arg(reconnectMs));
                        cancelDrainLogged = true;
                    }
                }
                if (!canDrainCurrentResponse) {
                    socket.abort();
                    *error = "operation canceled";
                    return false;
                }
            }
            if (socket.bytesAvailable() <= 0) {
                if (!socket.waitForReadyRead(50)) {
                    // No buffered data and the peer is no longer connected: the
                    // rest of the message will never arrive, so fail fast instead
                    // of spinning until the timeout elapses.
                    if (socket.bytesAvailable() <= 0
                        && socket.state() != QAbstractSocket::ConnectedState) {
                        *error = socket.errorString();
                        return false;
                    }
                    if (idleTimeoutMs > 0 && idleTimer.elapsed() >= idleTimeoutMs) {
                        // A timed-out read leaves a partial response buffered; abort
                        // so the socket is not reused with stale bytes (which would
                        // misalign the next request's response).
                        *error = socket.errorString();
                        socket.abort();
                        return false;
                    }
                    continue;
                }
            }
            const QByteArray chunk = socket.read(size - out->size());
            if (!chunk.isEmpty()) {
                out->append(chunk);
                // This is an inactivity timeout, not a total-transfer timeout.
                // A slow response remains valid as long as bytes keep arriving.
                idleTimer.restart();
            }
        }
        return true;
    }

Message MdsIpClient::readMessage(QTcpSocket& socket, QString* error)
{
        error->clear();
        QByteArray header;
        Message msg;
        if (!readFully(socket, &header, 48, false, error)) {
            return msg;
        }
        QDataStream in(header);
        in.setByteOrder(QDataStream::BigEndian);
        qint32 msgLen = 0;
        qint8 nargs = 0;
        qint8 descrIdx = 0;
        qint8 messageId = 0;
        qint8 clientType = 0;
        qint8 ndims = 0;
        in >> msgLen >> msg.status >> msg.length >> nargs >> descrIdx >> messageId >> msg.dtype >> clientType >> ndims;
        if (msgLen < 48 || msgLen > 128 * 1024 * 1024) {
            *error = "invalid MDSIP message length";
            // The stream boundary can no longer be trusted. Never let this
            // connection return to the warm cache.
            socket.abort();
            return msg;
        }
        if (!readFully(socket, &msg.body, msgLen - 48, true, error)) {
            msg.body.clear();
        }
        return msg;
    }

bool MdsIpClient::handshake(QTcpSocket& socket, QString* error)
{
        const QByteArray user("JAVA_USER");
        if (!writeMessage(socket, message(14, 1, 0, 1, user), error)) {
            return false;
        }
        readMessage(socket, error);
        return error->isEmpty();
    }

quint8 MdsIpClient::nextMessageId()
{
        thread_local quint8 messageId = 2;
        const quint8 currentMessageId = messageId;
        if (++messageId == 0) {
            messageId = 1;
        }
        return currentMessageId;
    }

bool MdsIpClient::sendValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        const QByteArray body = expr.toUtf8();
        return writeMessage(socket, message(14, 1, 0, nextMessageId(), body), error);
    }

bool MdsIpClient::queueValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        if (currentCanceled()) {
            *error = "operation canceled";
            return false;
        }
        if (socket.state() != QAbstractSocket::ConnectedState || !socket.isOpen()) {
            *error = socket.errorString().isEmpty()
                         ? QStringLiteral("MDS socket is not connected")
                         : socket.errorString();
            return false;
        }
        const QByteArray body = expr.toUtf8();
        const QByteArray packet = message(14, 1, 0, nextMessageId(), body);
        if (socket.write(packet) != packet.size()) {
            *error = socket.errorString();
            return false;
        }
        return true;
    }

bool MdsIpClient::flushQueuedValues(QTcpSocket& socket, QString* error)
{
        QElapsedTimer timer;
        timer.start();
        while (socket.bytesToWrite() > 0) {
            if (shouldAbortForCurrentCancel()) {
                socket.abort();
                *error = "operation canceled";
                return false;
            }
            if (!socket.waitForBytesWritten(50)) {
                if (socket.state() != QAbstractSocket::ConnectedState) {
                    *error = socket.errorString();
                    return false;
                }
                if (timer.elapsed() >= kNetworkTimeoutMs) {
                    // A timed-out socket is desynced from the peer; abort it so it
                    // cannot be reused as a warm connection with a half-written
                    // request still pending.
                    *error = socket.errorString();
                    socket.abort();
                    return false;
                }
            }
        }
        return true;
    }

Message MdsIpClient::value(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        const QByteArray body = expr.toUtf8();
        const quint8 currentMessageId = nextMessageId();
        if (!writeMessage(socket, message(14, 1, 0, currentMessageId, body), error)) {
            return {};
        }
        return readMessage(socket, error);
    }

bool MdsIpClient::clearTimeContext(QTcpSocket& socket, QString* error)
{
        QString cleanupError;
        if (socket.state() != QAbstractSocket::ConnectedState || !socket.isOpen()) {
            cleanupError = socket.errorString().isEmpty()
                               ? QStringLiteral("MDS socket is not connected")
                               : socket.errorString();
        } else {
            // SetTimeContext is session state. Even after cancellation, this
            // paired cleanup is required before the socket may be reused by a
            // later Thin/Medium/Full request. Suppress cancellation only for
            // this one protocol exchange; every other post-cancel write remains
            // blocked by writeMessage().
            CurrentCancelSuppressionGuard cancelSuppression;
            const Message reply = value(socket, QStringLiteral("SetTimeContext()"), &cleanupError);
            if (cleanupError.isEmpty() && (reply.status & 1) == 0) {
                cleanupError = QStringLiteral("SetTimeContext cleanup failed");
            }
        }
        if (cleanupError.isEmpty()) {
            if (currentCanceled()) {
                traceMdsLine(QStringLiteral("time_context_cleanup_after_cancel ok=1"));
            }
            return true;
        }

        // A socket with uncertain server-side context must never return to the
        // warm pool. Only this MDS connection is discarded; other worker
        // sockets and the outer SSH tunnel remain untouched.
        traceMdsLine(QString("time_context_cleanup_after_cancel ok=0 error=%1")
                         .arg(cleanupError.simplified()));
        socket.abort();
        if (error && error->isEmpty()) {
            *error = cleanupError;
        }
        return false;
    }

QVector<double> MdsIpClient::numericFromMessage(const Message& msg, QString* error)
{
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }
        QVector<double> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);

        if (msg.dtype == 11 || msg.dtype == 53) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            values.reserve(msg.body.size() / 8);
            while (!in.atEnd()) {
                double v = 0.0;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
            return values;
        }

        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                float v = 0.0f;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
        }
        return values;
    }

QVector<double> MdsIpClient::numericValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        return numericFromMessage(value(socket, expr, error), error);
    }

QVector<double> MdsIpClient::cachedNumericValue(QTcpSocket& socket,
                                          const QString& expr,
                                          QHash<QString, QVector<double>>* cache,
                                          QString* error)
{
        if (!cache) {
            return numericValue(socket, expr, error);
        }
        if (cache->contains(expr)) {
            if (error) {
                error->clear();
            }
            return cache->value(expr);
        }
        QVector<double> values = numericValue(socket, expr, error);
        if (!values.isEmpty()) {
            cache->insert(expr, values);
        }
        return values;
    }

int MdsIpClient::intValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return 0;
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return 0;
        }

        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if ((msg.dtype == 11 || msg.dtype == 53) && msg.body.size() >= 8) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            double v = 0.0;
            in >> v;
            return std::isfinite(v) && v > 0.0 && v <= std::numeric_limits<int>::max()
                       ? static_cast<int>(std::llround(v))
                       : 0;
        }
        if ((msg.dtype == 10 || msg.dtype == 52) && msg.body.size() >= 4) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            float v = 0.0f;
            in >> v;
            return std::isfinite(v) && v > 0.0f && v <= static_cast<float>(std::numeric_limits<int>::max())
                       ? static_cast<int>(std::lround(v))
                       : 0;
        }
        if (msg.body.size() == 8 && msg.dtype != 11 && msg.dtype != 53) {
            qint64 v = 0;
            in >> v;
            return v > 0 && v <= std::numeric_limits<int>::max() ? static_cast<int>(v) : 0;
        }
        if (msg.body.size() >= 4) {
            qint32 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() >= 2) {
            qint16 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() == 1) {
            return static_cast<quint8>(msg.body[0]);
        }
        return 0;
    }

QVector<int> MdsIpClient::intVectorValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }

        QVector<int> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if (msg.body.size() % 4 == 0) {
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                qint32 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        if (msg.body.size() % 2 == 0) {
            values.reserve(msg.body.size() / 2);
            while (!in.atEnd()) {
                qint16 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        values.reserve(msg.body.size());
        for (char byte : msg.body) {
            values.push_back(static_cast<quint8>(byte));
        }
        return values;
    }
} // namespace mds_client_internal

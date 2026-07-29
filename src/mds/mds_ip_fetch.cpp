// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal/mds_ip_client.hpp"

#include "core/mds_helpers.hpp"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QNetworkProxy>

#include <memory>
#include <utility>

namespace mds_client_internal {

using EastThinPlan = MdsIpClient::EastThinPlan;
using NativeRequest = MdsIpClient::NativeRequest;
using ScaledSignalExpr = MdsIpClient::ScaledSignalExpr;
using SignalFetchResult = MdsIpClient::SignalFetchResult;
using ThinSampling = MdsIpClient::ThinSampling;
using UniformTimebase = MdsIpClient::UniformTimebase;

QVector<SignalFetchResult> MdsIpClient::fetchGroupResults(const QVector<NativeRequest>& requests, bool openOnly) const
{
        CurrentCancelGuard cancelGuard(cancel_, preserveConnectionsOnCancel_);
        // Handshake and TreeOpen after a Full abort are connection setup, not a
        // normal signal read. In particular, an SSH-forwarded MDSIP session can
        // legitimately need 4-5 seconds here.
        CurrentReadTimeoutGuard setupTimeoutGuard(kConnectionSetupTimeoutMs);
        if (requests.isEmpty()) {
            return {};
        }
        if (isCanceled()) {
            // Keep the thread-local warm connections: they are on a clean request
            // boundary here (no partial exchange), so cancelling this group must
            // not throw away sockets the next fetch will immediately reuse.
            return {};
        }

        QVector<SignalFetchResult> results;
        results.reserve(requests.size());
        const PlotSpec& firstPlot = requests.front().plot;
        const SignalSpec& firstSig = requests.front().sig;
        QElapsedTimer groupTimer;
        groupTimer.start();
        QElapsedTimer stageTimer;
        CachedMdsConnection* cached = threadLocalConnection(firstSig);
        QTcpSocket* socketPtr = nullptr;
        QString error;
        qint64 connectMs = 0;
        qint64 handshakeMs = 0;
        qint64 openMs = 0;
        bool usedCachedConnection = false;
        bool reusedOpenTree = false;
        bool usedJavaOpenFallback = false;

        for (int attempt = 0; attempt < 2; ++attempt) {
            const bool canReuseSocket = cached && cached->socket && cached->socket->state() == QAbstractSocket::ConnectedState;
            usedCachedConnection = canReuseSocket;
            if (!canReuseSocket) {
                // A rapid Full switch can invalidate many worker sockets at
                // once. Bound concurrent session setup so the persistent SSH
                // tunnel and MDS server are not hit by a 16-connection storm.
                SemaphoreGuard setupGuard(connectionSetupSemaphore(firstSig));
                if (isCanceled()) {
                    error = "operation canceled";
                    resetConnection(cached);
                    break;
                }
                resetConnection(cached);
                cached->socket = std::make_unique<QTcpSocket>();
                cached->socket->setProxy(QNetworkProxy::NoProxy);
                stageTimer.restart();
                cached->socket->connectToHost(serverHost(firstSig.serverIp), serverPort(firstSig.serverIp));
                QElapsedTimer connectTimer;
                connectTimer.start();
                while (cached->socket->state() != QAbstractSocket::ConnectedState) {
                    if (isCanceled()) {
                        error = "operation canceled";
                        resetConnection(cached);
                        break;
                    }
                    if (!cached->socket->waitForConnected(50)) {
                        // A refused/unreachable host drops straight to
                        // UnconnectedState; there is nothing left to wait for, so
                        // fail fast instead of spinning until the timeout. A host
                        // that silently drops packets stays in ConnectingState and
                        // still waits out the full timeout.
                        if (cached->socket->state() == QAbstractSocket::UnconnectedState) {
                            error = cached->socket->errorString();
                            resetConnection(cached);
                            break;
                        }
                        if (connectTimer.elapsed() >= kConnectionSetupTimeoutMs) {
                            error = cached->socket->errorString();
                            resetConnection(cached);
                            break;
                        }
                    }
                }
                if (!error.isEmpty()) {
                    break;
                }
                connectMs = stageTimer.elapsed();

                stageTimer.restart();
                if (!handshake(*cached->socket, &error)) {
                    error = error.isEmpty() ? "MDS handshake failed" : error;
                    resetConnection(cached);
                    break;
                }
                handshakeMs = stageTimer.elapsed();
                if (!openOnly) {
                    // Do not let the deliberately concurrent 16-socket startup
                    // prewarm inflate the cost of one later lazy reconnect.
                    observeReconnectCost(static_cast<int>(connectMs + handshakeMs));
                }
            }

            socketPtr = cached->socket.get();
            if (openOnly) {
                traceMdsLine(QString("profile_prewarm socket_reused=%1 connect_ms=%2 handshake_ms=%3 server=%4")
                                 .arg(usedCachedConnection ? 1 : 0)
                                 .arg(connectMs)
                                 .arg(handshakeMs)
                                 .arg(serverKey(firstSig)));
                return {};
            }
            reusedOpenTree = cached->currentTree.compare(firstSig.experiment.trimmed(), Qt::CaseInsensitive) == 0
                             && cached->currentShot == firstPlot.shot.trimmed();
            if (reusedOpenTree) {
                error.clear();
                openMs = 0;
                break;
            }

            stageTimer.restart();
            const QString treeOpenExpr = QString("TreeOpen(\"%1\",%2)")
                                             .arg(escapedMdsExpr(firstSig.experiment), firstPlot.shot);
            value(*socketPtr, treeOpenExpr, &error);
            usedJavaOpenFallback = false;
            if (!error.isEmpty()) {
                const QString treeOpenError = error;
                error.clear();
                const QString javaOpenExpr = QString("JavaOpen(\"%1\",%2)")
                                                 .arg(escapedMdsExpr(firstSig.experiment), firstPlot.shot);
                value(*socketPtr, javaOpenExpr, &error);
                usedJavaOpenFallback = error.isEmpty();
                if (!error.isEmpty()) {
                    error = treeOpenError + "; fallback JavaOpen: " + error;
                }
            }
            openMs = stageTimer.elapsed();
            if (error.isEmpty()) {
                cached->currentTree = firstSig.experiment.trimmed();
                cached->currentShot = firstPlot.shot.trimmed();
                break;
            }

            if (openOnly && cached && cached->socket
                && cached->socket->state() == QAbstractSocket::ConnectedState) {
                // A tree may legitimately not exist for the current latest
                // shot. The reply was fully consumed, so keep the globally
                // warmed socket and continue warming the rest of the tree list.
                return {};
            }

            if (usedCachedConnection && attempt == 0) {
                resetConnection(cached);
                error.clear();
                connectMs = 0;
                handshakeMs = 0;
                openMs = 0;
                usedCachedConnection = false;
                reusedOpenTree = false;
                usedJavaOpenFallback = false;
                continue;
            }
            resetConnection(cached);
            break;
        }

        if (!socketPtr || !error.isEmpty()) {
            if (openOnly) {
                traceMdsLine(QString("profile_prewarm_error server=%1 error=%2")
                                 .arg(serverKey(firstSig), error.simplified()));
            }
            QVector<SignalFetchResult> errorResults = groupErrorResults(requests, error);
            emitResults(requests, errorResults);
            return errorResults;
        }
        QTcpSocket& socket = *socketPtr;
        traceMdsLine(QString("group_open_ms=%1 tree=%2 shot=%3 count=%4")
                         .arg(groupTimer.elapsed())
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));
        traceMdsLine(QString("group_open_parts connect_ms=%1 handshake_ms=%2 open_ms=%3 fallback=%4 tree=%5 shot=%6 count=%7")
                         .arg(connectMs)
                         .arg(handshakeMs)
                         .arg(openMs)
                         .arg(usedJavaOpenFallback ? 1 : 0)
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));
        traceMdsLine(QString("group_open_cache socket_reused=%1 tree_reused=%2 tree=%3 shot=%4 count=%5")
                         .arg(usedCachedConnection ? 1 : 0)
                         .arg(reusedOpenTree ? 1 : 0)
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));

        for (int i = 0; i < requests.size(); ++i) {
            if (isCanceled()) {
                // Cancelled between signals on a clean boundary (previous response
                // fully read); keep the open socket warm for the next fetch.
                break;
            }
            const NativeRequest& request = requests[i];
            QString signalError;
            SignalFetchResult result;
            result.loadedIndex = request.loadedIndex;
            result.series = fetchSignalOnOpenSocket(socket,
                                                    request.plot,
                                                    request.sig,
                                                    request.readMode,
                                                    request.maxPoints,
                                                    &signalError);
            emitResult(request, result);
            results.push_back(std::move(result));
        }
        if (cached && cached->socket
            && cached->socket->state() != QAbstractSocket::ConnectedState) {
            // readFully() aborts a cancelled/timed-out Full stream. Remove that
            // exact socket object now, before this worker can service the next
            // shot, rather than leaving a half-dead cache entry to be noticed
            // lazily on reuse.
            resetConnection(cached);
        }
        return results;
    }
} // namespace mds_client_internal

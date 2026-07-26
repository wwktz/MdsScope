// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

namespace mds_client_internal {

using Message = MdsIpClient::Message;

SignalSeries MdsIpClient::makeSeries(QString name, const QVector<double>& y, const QVector<double>& x, int)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = x.isEmpty() ? y.size() : std::min(y.size(), x.size());
        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double px = x.isEmpty() ? static_cast<double>(i) : x[i];
            const double py = y[i];
            if (std::isfinite(px) && std::isfinite(py)) {
                out.points.push_back(QPointF(px, py));
            }
        }
        return out;
    }

SignalSeries MdsIpClient::makeSeriesUniformX(QString name,
                                       const QVector<double>& y,
                                       double start,
                                       double step,
                                       int)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = y.size();
        if (n <= 0 || !std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
            return out;
        }

        out.uniformY.resize(n);
        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; ++i) {
            const double py = y[i];
            out.uniformY[i] = static_cast<float>(py);
            if (std::isfinite(py)) {
                minY = std::min(minY, py);
                maxY = std::max(maxY, py);
            }
        }
        if (!std::isfinite(minY) || !std::isfinite(maxY)) {
            out.uniformY.clear();
            return out;
        }
        out.uniformStart = start;
        out.uniformStep = step;
        out.uniformMinY = minY;
        out.uniformMaxY = maxY;
        return out;
    }

int MdsIpClient::numericElementSize(const Message& msg)
{
        if (msg.dtype == 11 || msg.dtype == 53) {
            return 8;
        }
        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            return 4;
        }
        return 0;
    }

double MdsIpClient::numericAt(const QByteArray& body, int elementSize, int index)
{
        const auto* bytes = reinterpret_cast<const uchar*>(body.constData());
        if (elementSize == 8) {
            const quint64 bits = qFromBigEndian<quint64>(bytes + static_cast<qsizetype>(index) * 8);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }
        const quint32 bits = qFromBigEndian<quint32>(bytes + static_cast<qsizetype>(index) * 4);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

SignalSeries MdsIpClient::makeSeriesUniformXFromMessage(QString name,
                                                  const Message& msg,
                                                  double start,
                                                  double step,
                                                  int,
                                                  QString* error)
{
        SignalSeries out;
        out.name = std::move(name);
        if (!error->isEmpty()) {
            return out;
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return out;
        }

        const int elementSize = numericElementSize(msg);
        if (elementSize <= 0 || msg.body.isEmpty() || msg.body.size() % elementSize != 0
            || !std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
            return out;
        }

        const int n = static_cast<int>(msg.body.size() / elementSize);
        out.uniformY.resize(n);
        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; ++i) {
            const double value = numericAt(msg.body, elementSize, i);
            out.uniformY[i] = static_cast<float>(value);
            if (std::isfinite(value)) {
                minY = std::min(minY, value);
                maxY = std::max(maxY, value);
            }
        }
        if (!std::isfinite(minY) || !std::isfinite(maxY)) {
            out.uniformY.clear();
            return out;
        }
        out.uniformStart = start;
        out.uniformStep = step;
        out.uniformMinY = minY;
        out.uniformMaxY = maxY;
        return out;
    }

} // namespace mds_client_internal

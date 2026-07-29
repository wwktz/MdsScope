// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"
#include "core/mdsscope_internal.hpp"

namespace mds_client_internal {

using EastThinPlan = MdsIpClient::EastThinPlan;
using NativeRequest = MdsIpClient::NativeRequest;
using ScaledSignalExpr = MdsIpClient::ScaledSignalExpr;
using ThinSampling = MdsIpClient::ThinSampling;
using UniformTimebase = MdsIpClient::UniformTimebase;

bool MdsIpClient::isSimpleMdsNode(const QString& expr)
{
        const QString node = expr.trimmed();
        return node.startsWith('\\')
               && !node.contains('(')
               && !node.contains(')')
               && !node.contains('[')
               && !node.contains(']')
               && !node.contains(',')
               && !node.contains(' ');
    }

ScaledSignalExpr MdsIpClient::scaledSimpleSignalExpr(QString expr)
{
        expr = expr.trimmed();
        expr.remove(' ');
        ScaledSignalExpr out;
        if (expr.isEmpty()) {
            return out;
        }

        double sign = 1.0;
        if (expr.startsWith('-')) {
            sign = -1.0;
            expr.remove(0, 1);
        } else if (expr.startsWith('+')) {
            expr.remove(0, 1);
        }

        static const QRegularExpression re(
            R"(^\(?(\\[A-Za-z][A-Za-z0-9_]*)\)?(?:([*/])([0-9]+(?:\.[0-9]+)?))?$)");
        const QRegularExpressionMatch match = re.match(expr);
        if (!match.hasMatch()) {
            return out;
        }

        double scale = sign;
        if (!match.captured(2).isEmpty()) {
            bool ok = false;
            const double value = match.captured(3).toDouble(&ok);
            if (!ok || value == 0.0) {
                return {};
            }
            scale *= match.captured(2) == "*" ? value : (1.0 / value);
        }

        out.valid = true;
        out.baseExpr = match.captured(1);
        out.scale = scale;
        return out;
    }

void MdsIpClient::applySeriesScale(SignalSeries* series, const QString& name, double scale)
{
        series->name = normalizedMdsSignal(name);
        if (scale == 1.0) {
            return;
        }
        for (QPointF& point : series->points) {
            point.setY(point.y() * scale);
        }
        for (float& value : series->uniformY) {
            value = static_cast<float>(static_cast<double>(value) * scale);
        }
        if (std::isfinite(series->uniformMinY) && std::isfinite(series->uniformMaxY)) {
            const double a = series->uniformMinY * scale;
            const double b = series->uniformMaxY * scale;
            series->uniformMinY = std::min(a, b);
            series->uniformMaxY = std::max(a, b);
        }
    }

bool MdsIpClient::isEastTimebaseCandidate(const QString& shotText, const SignalSpec& sig)
{
        const QString experiment = sig.experiment.trimmed().toLower();
        bool shotOk = false;
        const int shot = shotText.trimmed().toInt(&shotOk);
        return (((experiment == "east" && shotOk && shot > 44326) || experiment == "eastpower")
                && isSimpleMdsNode(sig.yExpr));
    }

UniformTimebase MdsIpClient::eastUniformTimebase(QTcpSocket& socket,
                                           const QString& shotText,
                                           const SignalSpec& sig,
                                           QString* error)
{
        const QString yExpr = sig.yExpr.trimmed();
        if (!isEastTimebaseCandidate(shotText, sig)) {
            return {};
        }

        QString localError;
        const int freq = intValue(socket, yExpr + ":freq", &localError);
        if (freq <= 0) {
            if (error) {
                error->clear();
            }
            return {};
        }

        const QVector<double> trigtime = numericValue(socket, yExpr + ":trigtime", &localError);
        if (trigtime.isEmpty() || !std::isfinite(trigtime.front())) {
            if (error) {
                error->clear();
            }
            return {};
        }

        if (error) {
            error->clear();
        }
        UniformTimebase timebase;
        timebase.valid = true;
        timebase.start = trigtime.front();
        timebase.step = 1.0 / static_cast<double>(freq);
        traceMdsLine(QString("east_timebase shot=%1 tree=%2 y=%3 freq=%4 trigtime=%5")
                         .arg(shotText, sig.experiment, yExpr)
                         .arg(freq)
                         .arg(timebase.start, 0, 'g', 12));
        return timebase;
    }

EastThinPlan MdsIpClient::eastThinPlan(QTcpSocket& socket,
                                 const QString& shotText,
                                 const SignalSpec& sig,
                                 int maxPoints,
                                 QString* error)
{
        EastThinPlan plan;
        if (maxPoints <= 0 || !isEastTimebaseCandidate(shotText, sig)) {
            return plan;
        }

        const QString yExpr = sig.yExpr.trimmed();
        QString localError;
        // Java (MdsAccess.getSignal) never calls size() on segmented EAST nodes:
        // it derives the point count from the cheap scalar children daqtime*freq
        // (~3ms) instead of size() (~1200ms per segmented node). Match that here,
        // falling back to size() only when daqtime is unavailable, as Java does.
        const QVector<double> meta = numericValue(socket,
                                                  QString("[%1:daqtime,%1:freq,%1:trigtime]").arg(yExpr),
                                                  &localError);
        if (meta.size() < 3 || !std::isfinite(meta[1]) || !std::isfinite(meta[2])) {
            if (error) {
                error->clear();
            }
            return plan;
        }

        const int freq = static_cast<int>(std::llround(meta[1]));
        if (freq <= 0) {
            if (error) {
                error->clear();
            }
            return plan;
        }

        const double daqtime = std::isfinite(meta[0]) ? meta[0] : 0.0;
        int pointCount = daqtime > 0.0
                             ? static_cast<int>(std::llround(daqtime * static_cast<double>(freq)))
                             : 0;
        if (pointCount <= 0) {
            QString sizeError;
            const int sizeCount = intValue(socket, QString("size(%1)").arg(yExpr), &sizeError);
            if (sizeCount > 0) {
                pointCount = sizeCount;
            }
        }
        if (pointCount <= 0) {
            if (error) {
                error->clear();
            }
            return plan;
        }

        plan.sampling = samplingFromPointCount(pointCount, maxPoints);
        if (plan.sampling.sampledCount <= 0) {
            if (error) {
                error->clear();
            }
            return {};
        }

        plan.timebase.valid = true;
        plan.timebase.start = meta[2];
        plan.timebase.step = 1.0 / static_cast<double>(freq);
        plan.valid = true;
        if (error) {
            error->clear();
        }
        traceMdsLine(QString("east_thin_plan y=%1 size=%2 step=%3 freq=%4 trigtime=%5")
                         .arg(yExpr)
                         .arg(pointCount)
                         .arg(plan.sampling.step)
                         .arg(freq)
                         .arg(plan.timebase.start, 0, 'g', 12));
        return plan;
    }

ThinSampling MdsIpClient::samplingFromPointCount(int pointCount, int maxPoints)
{
        ThinSampling sampling;
        if (maxPoints <= 0 || pointCount <= 0) {
            return sampling;
        }
        sampling.sourceCount = pointCount;
        if (pointCount <= maxPoints) {
            sampling.step = 1;
            sampling.sampledCount = pointCount;
            return sampling;
        }
        sampling.step = std::max(1, (pointCount / maxPoints) + 1);
        sampling.sampledCount = pointCount > 1 ? ((pointCount - 2) / sampling.step) + 1 : pointCount;
        return sampling;
    }

ThinSampling MdsIpClient::thinSampling(QTcpSocket& socket, const QString& yExpr, int maxPoints, QString* error)
{
        if (maxPoints <= 0) {
            return {};
        }

        QString localError;
        const int pointCount = intValue(socket, QString("size(%1)").arg(yExpr), &localError);
        ThinSampling sampling = samplingFromPointCount(pointCount, maxPoints);
        if (sampling.sampledCount <= 0) {
            if (error) {
                *error = localError.isEmpty() ? "empty signal" : localError;
            }
            return sampling;
        }
        if (sampling.step <= 1) {
            if (error) {
                error->clear();
            }
            return sampling;
        }

        if (error) {
            error->clear();
        }
        return sampling;
    }
} // namespace mds_client_internal

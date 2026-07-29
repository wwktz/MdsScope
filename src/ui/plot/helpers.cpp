// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.hpp"

bool signalVisible(const PlotSpec& spec, int index)
{
    return index < 0 || index >= spec.signalSpecs.size()
           || !spec.signalSpecs[index].hidden;
}

void fillMissingAxisBounds(double* minValue,
                           double* maxValue,
                           double defaultMin,
                           double defaultMax)
{
    if (!minValue || !maxValue) {
        return;
    }
    if (!std::isfinite(*minValue) && !std::isfinite(*maxValue)) {
        *minValue = defaultMin;
        *maxValue = defaultMax;
    } else if (!std::isfinite(*minValue)) {
        *minValue = *maxValue - (defaultMax - defaultMin);
    } else if (!std::isfinite(*maxValue)) {
        *maxValue = *minValue + (defaultMax - defaultMin);
    }
}

QPair<int, int> sortedPointIndexRange(const QVector<QPointF>& points,
                                      double xmin,
                                      double xmax)
{
    if (points.isEmpty() || !std::isfinite(xmin)
        || !std::isfinite(xmax)) {
        return {-1, -1};
    }
    if (xmax < xmin) {
        std::swap(xmin, xmax);
    }
    const bool ascending = points.first().x() <= points.last().x();
    const auto beginIt =
        ascending
            ? std::lower_bound(
                  points.cbegin(),
                  points.cend(),
                  xmin,
                  [](const QPointF& point, double x) {
                      return point.x() < x;
                  })
            : std::lower_bound(
                  points.cbegin(),
                  points.cend(),
                  xmax,
                  [](const QPointF& point, double x) {
                      return point.x() > x;
                  });
    const auto endIt =
        ascending
            ? std::upper_bound(
                  points.cbegin(),
                  points.cend(),
                  xmax,
                  [](double x, const QPointF& point) {
                      return x < point.x();
                  })
            : std::upper_bound(
                  points.cbegin(),
                  points.cend(),
                  xmin,
                  [](double x, const QPointF& point) {
                      return x > point.x();
                  });
    if (beginIt == endIt) {
        return {-1, -1};
    }
    return {
        static_cast<int>(std::distance(points.cbegin(), beginIt)),
        static_cast<int>(std::distance(points.cbegin(), endIt)) - 1,
    };
}

QString effectiveYLabel(const PlotSpec& spec,
                        const QVector<SignalSeries>& series)
{
    const QString configuredLabel = spec.yLabel.trimmed();
    if (!configuredLabel.isEmpty()) {
        return configuredLabel;
    }
    for (int i = 0; i < spec.signalSpecs.size(); ++i) {
        if (spec.signalSpecs[i].hidden) {
            continue;
        }
        if (i < series.size()) {
            const QString sourceUnit = series[i].unit.trimmed();
            if (!sourceUnit.isEmpty()) {
                return sourceUnit;
            }
        }
        break;
    }
    return QStringLiteral("a.u.");
}

void rebuildMinMaxIndex(SignalSeries& series)
{
    const int pointCount = series.pointCount();
    series.minYBlocks.clear();
    series.maxYBlocks.clear();
    series.minMaxBlockSize = 0;
    if (pointCount < kMinMaxBlockSize * 4) {
        return;
    }

    const int blockCount =
        (pointCount + kMinMaxBlockSize - 1) / kMinMaxBlockSize;
    series.minYBlocks.reserve(blockCount);
    series.maxYBlocks.reserve(blockCount);
    for (int block = 0; block < blockCount; ++block) {
        const int begin = block * kMinMaxBlockSize;
        const int end =
            std::min(pointCount, begin + kMinMaxBlockSize);
        float minY = std::numeric_limits<float>::infinity();
        float maxY = -std::numeric_limits<float>::infinity();
        for (int i = begin; i < end; ++i) {
            const float y =
                series.hasUniformData()
                    ? series.uniformY[i]
                    : static_cast<float>(series.points[i].y());
            if (!std::isfinite(y)) {
                continue;
            }
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        series.minYBlocks.push_back(minY);
        series.maxYBlocks.push_back(maxY);
    }
    series.minMaxBlockSize = kMinMaxBlockSize;
}

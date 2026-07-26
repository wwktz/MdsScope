// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

inline constexpr int kMinMaxBlockSize = 256;

inline bool signalVisible(const PlotSpec& spec, int index)
{
    return index < 0 || index >= spec.signalSpecs.size() || !spec.signalSpecs[index].hidden;
}

inline void fillMissingAxisBounds(double* minValue,
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

inline QPair<int, int> sortedPointIndexRange(const QVector<QPointF>& points,
                                             double xmin,
                                             double xmax)
{
    if (points.isEmpty() || !std::isfinite(xmin) || !std::isfinite(xmax)) {
        return {-1, -1};
    }
    if (xmax < xmin) {
        std::swap(xmin, xmax);
    }
    const bool ascending = points.first().x() <= points.last().x();
    const auto beginIt = ascending
        ? std::lower_bound(points.cbegin(), points.cend(), xmin, [](const QPointF& point, double x) {
              return point.x() < x;
          })
        : std::lower_bound(points.cbegin(), points.cend(), xmax, [](const QPointF& point, double x) {
              return point.x() > x;
          });
    const auto endIt = ascending
        ? std::upper_bound(points.cbegin(), points.cend(), xmax, [](double x, const QPointF& point) {
              return x < point.x();
          })
        : std::upper_bound(points.cbegin(), points.cend(), xmin, [](double x, const QPointF& point) {
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

inline QString effectiveYLabel(const PlotSpec& spec, const QVector<SignalSeries>& series)
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

inline void rebuildMinMaxIndex(SignalSeries& series)
{
    const int n = series.hasUniformData() ? series.uniformY.size() : series.points.size();
    series.minYBlocks.clear();
    series.maxYBlocks.clear();
    series.minMaxBlockSize = 0;
    if (n < kMinMaxBlockSize * 4) {
        return;
    }

    const int blockCount = (n + kMinMaxBlockSize - 1) / kMinMaxBlockSize;
    series.minYBlocks.reserve(blockCount);
    series.maxYBlocks.reserve(blockCount);
    for (int block = 0; block < blockCount; ++block) {
        const int begin = block * kMinMaxBlockSize;
        const int end = std::min(n, begin + kMinMaxBlockSize);
        float minY = std::numeric_limits<float>::infinity();
        float maxY = -std::numeric_limits<float>::infinity();
        for (int i = begin; i < end; ++i) {
            const float y = series.hasUniformData() ? series.uniformY[i] : static_cast<float>(series.points[i].y());
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

template <typename ValueAt>
void considerIndexedYRange(const SignalSeries& series,
                           int startIndex,
                           int endIndex,
                           ValueAt valueAt,
                           double* minY,
                           double* maxY)
{
    if (!minY || !maxY || startIndex > endIndex) {
        return;
    }
    auto consider = [&](double y) {
        if (!std::isfinite(y)) {
            return;
        }
        *minY = std::min(*minY, y);
        *maxY = std::max(*maxY, y);
    };

    const int blockSize = series.minMaxBlockSize;
    if (blockSize <= 0 || series.minYBlocks.isEmpty() || series.maxYBlocks.isEmpty()) {
        for (int i = startIndex; i <= endIndex; ++i) {
            consider(valueAt(i));
        }
        return;
    }

    while (startIndex <= endIndex && startIndex % blockSize != 0) {
        consider(valueAt(startIndex++));
    }
    while (startIndex + blockSize - 1 <= endIndex) {
        const int block = startIndex / blockSize;
        if (block >= 0 && block < series.minYBlocks.size()) {
            consider(series.minYBlocks[block]);
            consider(series.maxYBlocks[block]);
        }
        startIndex += blockSize;
    }
    while (startIndex <= endIndex) {
        consider(valueAt(startIndex++));
    }
}

template <typename ValueAt>
bool findIndexedYExtrema(const SignalSeries& series,
                         int startIndex,
                         int endIndex,
                         ValueAt valueAt,
                         int* minIndex,
                         int* maxIndex)
{
    if (!minIndex || !maxIndex || startIndex > endIndex) {
        return false;
    }

    int edgeMinIndex = -1;
    int edgeMaxIndex = -1;
    auto considerEdge = [&](int index) {
        const double y = valueAt(index);
        if (!std::isfinite(y)) {
            return;
        }
        if (edgeMinIndex < 0 || y < valueAt(edgeMinIndex)) {
            edgeMinIndex = index;
        }
        if (edgeMaxIndex < 0 || y > valueAt(edgeMaxIndex)) {
            edgeMaxIndex = index;
        }
    };

    const int blockSize = series.minMaxBlockSize;
    if (blockSize <= 0 || series.minYBlocks.isEmpty() || series.maxYBlocks.isEmpty()) {
        for (int i = startIndex; i <= endIndex; ++i) {
            considerEdge(i);
        }
        if (edgeMinIndex < 0 || edgeMaxIndex < 0) {
            return false;
        }
        *minIndex = edgeMinIndex;
        *maxIndex = edgeMaxIndex;
        return true;
    }

    const int originalStart = startIndex;
    const int originalEnd = endIndex;
    while (startIndex <= endIndex && startIndex % blockSize != 0) {
        considerEdge(startIndex++);
    }

    int minBlock = -1;
    int maxBlock = -1;
    double minBlockValue = std::numeric_limits<double>::infinity();
    double maxBlockValue = -std::numeric_limits<double>::infinity();
    while (startIndex + blockSize - 1 <= endIndex) {
        const int block = startIndex / blockSize;
        if (block >= 0 && block < series.minYBlocks.size() && block < series.maxYBlocks.size()) {
            const double blockMin = series.minYBlocks[block];
            const double blockMax = series.maxYBlocks[block];
            if (std::isfinite(blockMin) && blockMin < minBlockValue) {
                minBlockValue = blockMin;
                minBlock = block;
            }
            if (std::isfinite(blockMax) && blockMax > maxBlockValue) {
                maxBlockValue = blockMax;
                maxBlock = block;
            }
        }
        startIndex += blockSize;
    }
    while (startIndex <= endIndex) {
        considerEdge(startIndex++);
    }

    auto resolveBlock = [&](int block, bool findMinimum) {
        int resolved = -1;
        if (block < 0) {
            return resolved;
        }
        const int begin = std::max(originalStart, block * blockSize);
        const int end = std::min(originalEnd + 1, begin + blockSize);
        for (int i = begin; i < end; ++i) {
            const double y = valueAt(i);
            if (!std::isfinite(y)) {
                continue;
            }
            if (resolved < 0
                || (findMinimum ? y < valueAt(resolved) : y > valueAt(resolved))) {
                resolved = i;
            }
        }
        return resolved;
    };

    const int blockMinIndex = resolveBlock(minBlock, true);
    const int blockMaxIndex = resolveBlock(maxBlock, false);
    int resolvedMin = edgeMinIndex;
    int resolvedMax = edgeMaxIndex;
    if (blockMinIndex >= 0
        && (resolvedMin < 0
            || valueAt(blockMinIndex) < valueAt(resolvedMin)
            || (valueAt(blockMinIndex) == valueAt(resolvedMin) && blockMinIndex < resolvedMin))) {
        resolvedMin = blockMinIndex;
    }
    if (blockMaxIndex >= 0
        && (resolvedMax < 0
            || valueAt(blockMaxIndex) > valueAt(resolvedMax)
            || (valueAt(blockMaxIndex) == valueAt(resolvedMax) && blockMaxIndex < resolvedMax))) {
        resolvedMax = blockMaxIndex;
    }
    if (resolvedMin < 0 || resolvedMax < 0) {
        return false;
    }
    *minIndex = resolvedMin;
    *maxIndex = resolvedMax;
    return true;
}

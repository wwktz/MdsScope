// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

inline constexpr int kMinMaxBlockSize = 256;

bool signalVisible(const PlotSpec& spec, int index);
void fillMissingAxisBounds(double* minValue,
                           double* maxValue,
                           double defaultMin,
                           double defaultMax);
QPair<int, int> sortedPointIndexRange(const QVector<QPointF>& points,
                                      double xmin,
                                      double xmax);
QString effectiveYLabel(const PlotSpec& spec,
                        const QVector<SignalSeries>& series);
void rebuildMinMaxIndex(SignalSeries& series);

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

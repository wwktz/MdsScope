// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "helpers.hpp"

namespace {

template <typename PointAt>
QVector<QPointF> pixelColumnEnvelope(const SignalSeries& series,
                                     int sourceCount,
                                     const QRectF& view,
                                     double pixelWidth,
                                     PointAt pointAt)
{
    if (sourceCount <= 0 || !view.isValid() || view.width() <= 0.0
        || !std::isfinite(pixelWidth) || pixelWidth <= 0.0) {
        return {};
    }

    const bool reversed = pointAt(0).x() > pointAt(sourceCount - 1).x();
    auto sourceIndex = [sourceCount, reversed](int logicalIndex) {
        return reversed ? sourceCount - 1 - logicalIndex : logicalIndex;
    };
    auto logicalPointAt = [&](int logicalIndex) {
        return pointAt(sourceIndex(logicalIndex));
    };
    auto lowerBound = [&](double x) {
        int low = 0;
        int high = sourceCount;
        while (low < high) {
            const int middle = low + (high - low) / 2;
            if (logicalPointAt(middle).x() < x) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        return low;
    };
    auto upperBound = [&](double x) {
        int low = 0;
        int high = sourceCount;
        while (low < high) {
            const int middle = low + (high - low) / 2;
            if (x < logicalPointAt(middle).x()) {
                high = middle;
            } else {
                low = middle + 1;
            }
        }
        return low;
    };

    const int visibleBegin = lowerBound(view.left());
    const int visibleEnd = upperBound(view.right());
    const int columns = std::max(1, static_cast<int>(std::ceil(pixelWidth)));
    QVector<QPointF> out;
    out.reserve(std::min(sourceCount, columns * 4 + 2));

    auto appendIfFinite = [&](const QPointF& point) {
        if (std::isfinite(point.x()) && std::isfinite(point.y())) {
            out.push_back(point);
        }
    };

    // Keep one real sample on either side so a segment crossing a view edge
    // remains connected after clipping.
    if (visibleBegin > 0) {
        appendIfFinite(logicalPointAt(visibleBegin - 1));
    }

    int bucketBegin = visibleBegin;
    auto valueAt = [&](int index) {
        return pointAt(index).y();
    };
    for (int column = 0; column < columns && bucketBegin < visibleEnd; ++column) {
        const int bucketEnd = column + 1 == columns
                                  ? visibleEnd
                                  : std::min(visibleEnd,
                                             lowerBound(view.left()
                                                        + view.width() * static_cast<double>(column + 1)
                                                              / static_cast<double>(columns)));
        if (bucketEnd <= bucketBegin) {
            continue;
        }

        const int firstSource = sourceIndex(bucketBegin);
        const int lastSource = sourceIndex(bucketEnd - 1);
        const int sourceBegin = std::min(firstSource, lastSource);
        const int sourceEnd = std::max(firstSource, lastSource);
        int minIndex = -1;
        int maxIndex = -1;
        if (findIndexedYExtrema(series, sourceBegin, sourceEnd, valueAt, &minIndex, &maxIndex)) {
            QVector<int> representativeIndexes{
                firstSource,
                minIndex,
                maxIndex,
                lastSource,
            };
            std::sort(representativeIndexes.begin(), representativeIndexes.end(), [&](int lhs, int rhs) {
                const int lhsLogical = reversed ? sourceCount - 1 - lhs : lhs;
                const int rhsLogical = reversed ? sourceCount - 1 - rhs : rhs;
                return lhsLogical < rhsLogical;
            });
            representativeIndexes.erase(
                std::unique(representativeIndexes.begin(), representativeIndexes.end()),
                representativeIndexes.end());
            for (int index : representativeIndexes) {
                appendIfFinite(pointAt(index));
            }
        }
        bucketBegin = bucketEnd;
    }

    if (visibleEnd < sourceCount) {
        appendIfFinite(logicalPointAt(visibleEnd));
    }
    return out;
}

} // namespace

void PlotWidget::resetScale(bool repaint)
{
    hasView_ = false;
    view_ = {};
    invalidatePlotCache();
    if (repaint) {
        update();
    }
}

void PlotWidget::applyView(const QRectF& view)
{
    if (!view.isValid() || view.width() <= 0 || view.height() <= 0) {
        return;
    }
    view_ = view;
    hasView_ = true;
    invalidatePlotCache();
    update();
}

void PlotWidget::applyXRangeAutoY(double xmin, double xmax)
{
    if (!std::isfinite(xmin) || !std::isfinite(xmax) || xmin == xmax) {
        return;
    }
    if (xmax < xmin) {
        std::swap(xmin, xmax);
    }

    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const SignalSeries& s : series_) {
        auto considerY = [&](double y) {
            if (!std::isfinite(y)) {
                return;
            }
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        };

        if (s.hasUniformData() && s.uniformStep != 0.0) {
            const int n = s.uniformY.size();
            if (n <= 0) {
                continue;
            }
            const double firstX = s.uniformStart;
            const double lastX = s.uniformStart + static_cast<double>(n - 1) * s.uniformStep;
            const double dataMinX = std::min(firstX, lastX);
            const double dataMaxX = std::max(firstX, lastX);
            if (xmin <= dataMinX && xmax >= dataMaxX
                && std::isfinite(s.uniformMinY) && std::isfinite(s.uniformMaxY)) {
                considerY(s.uniformMinY);
                considerY(s.uniformMaxY);
                continue;
            }
            int startIndex = static_cast<int>(std::floor((xmin - s.uniformStart) / s.uniformStep));
            int endIndex = static_cast<int>(std::ceil((xmax - s.uniformStart) / s.uniformStep));
            if (s.uniformStep < 0) {
                startIndex = static_cast<int>(std::floor((xmax - s.uniformStart) / s.uniformStep));
                endIndex = static_cast<int>(std::ceil((xmin - s.uniformStart) / s.uniformStep));
            }
            startIndex = std::clamp(startIndex, 0, n - 1);
            endIndex = std::clamp(endIndex, 0, n - 1);
            if (endIndex < startIndex) {
                std::swap(startIndex, endIndex);
            }
            considerIndexedYRange(s, startIndex, endIndex, [&](int i) {
                return static_cast<double>(s.uniformY[i]);
            }, &minY, &maxY);
        } else {
            if (s.points.isEmpty()) {
                continue;
            }
            auto lowerByX = [](const QPointF& point, double x) {
                return point.x() < x;
            };
            const auto beginIt = std::lower_bound(s.points.cbegin(), s.points.cend(), xmin, lowerByX);
            const auto endIt = std::upper_bound(s.points.cbegin(), s.points.cend(), xmax, [](double x, const QPointF& point) {
                return x < point.x();
            });
            if (beginIt != endIt) {
                const int startIndex = static_cast<int>(std::distance(s.points.cbegin(), beginIt));
                const int endIndex = static_cast<int>(std::distance(s.points.cbegin(), endIt)) - 1;
                considerIndexedYRange(s, startIndex, endIndex, [&](int i) {
                    return s.points[i].y();
                }, &minY, &maxY);
            }
        }
    }

    if (!std::isfinite(minY) || !std::isfinite(maxY)) {
        QRectF bounds = dataBounds();
        minY = bounds.top();
        maxY = bounds.bottom();
    }
    const double yPad = (maxY - minY) * 0.04;
    if (yPad > 0.0) {
        minY -= yPad;
        maxY += yPad;
    }
    QRectF next(QPointF(xmin, minY), QPointF(xmax, maxY));
    expandFlatRange(next);
    view_ = next;
    hasView_ = true;
    invalidatePlotCache();
    update();
}

void PlotWidget::applyYRangeKeepX(double ymin, double ymax)
{
    if (!std::isfinite(ymin) || !std::isfinite(ymax) || ymin == ymax) {
        return;
    }
    if (ymax < ymin) {
        std::swap(ymin, ymax);
    }

    QRectF next = effectiveView();
    if (!next.isValid() || next.width() <= 0.0) {
        next = dataBounds();
    }
    if (!next.isValid() || next.width() <= 0.0) {
        return;
    }
    next.setTop(ymin);
    next.setBottom(ymax);
    expandFlatRange(next);
    view_ = next;
    hasView_ = true;
    invalidatePlotCache();
    update();
}

QRectF PlotWidget::plotRect() const
{
    const FontSettings& fonts = fontSettings();
    QFont axisFont(fonts.family, fonts.axisSize + (largeDisplayMode_ ? 4 : 0));
    axisFont.setPointSize(std::max(7, axisFont.pointSize()));
    const QFontMetrics axisFm(axisFont);
    const int bottomMargin = std::max(18, axisFm.height() + 6);
    return rect().adjusted(2, 3, -3, -bottomMargin);
}

QRectF PlotWidget::dataBounds() const
{
    if (!dataBoundsDirty_ && cachedDataBounds_.isValid()) {
        return cachedDataBounds_;
    }
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < series_.size(); ++i) {
        if (i < spec_.signalSpecs.size() && spec_.signalSpecs[i].hidden) {
            continue;
        }
        const auto& s = series_[i];
        if (s.hasUniformData()) {
            const double x0 = s.uniformStart;
            const double x1 = s.uniformStart + static_cast<double>(s.uniformY.size() - 1) * s.uniformStep;
            minX = std::min(minX, std::min(x0, x1));
            maxX = std::max(maxX, std::max(x0, x1));
            if (std::isfinite(s.uniformMinY) && std::isfinite(s.uniformMaxY)) {
                minY = std::min(minY, s.uniformMinY);
                maxY = std::max(maxY, s.uniformMaxY);
            }
        }
        for (const QPointF& p : s.points) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
    }
    const bool fixedMinX = spec_.customXRange && std::isfinite(spec_.xmin);
    const bool fixedMaxX = spec_.customXRange && std::isfinite(spec_.xmax);
    const bool fixedMinY = spec_.customYRange && std::isfinite(spec_.ymin);
    const bool fixedMaxY = spec_.customYRange && std::isfinite(spec_.ymax);
    if (fixedMinX) minX = spec_.xmin;
    if (fixedMaxX) maxX = spec_.xmax;
    if (fixedMinY) minY = spec_.ymin;
    if (fixedMaxY) maxY = spec_.ymax;
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY)) {
        cachedDataBounds_ = QRectF(0, -1, 1, 2);
        dataBoundsDirty_ = false;
        return cachedDataBounds_;
    }
    const double xPad = (maxX - minX) * 0.01;
    if (xPad > 0.0) {
        if (!fixedMinX) minX -= xPad;
        if (!fixedMaxX) maxX += xPad;
    }
    const double yPad = (maxY - minY) * 0.04;
    if (yPad > 0.0) {
        if (!fixedMinY) minY -= yPad;
        if (!fixedMaxY) maxY += yPad;
    }
    QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    expandFlatRange(bounds);
    cachedDataBounds_ = bounds;
    dataBoundsDirty_ = false;
    return cachedDataBounds_;
}

QRectF PlotWidget::effectiveView() const
{
    if (hasView_ && view_.isValid()) {
        return view_;
    }
    return dataBounds();
}

void PlotWidget::expandFlatRange(QRectF& range) const
{
    if (range.width() <= 0) {
        range.setLeft(range.left() - 0.5);
        range.setRight(range.right() + 0.5);
    }
    if (range.height() <= 0) {
        range.setTop(range.top() - 0.5);
        range.setBottom(range.bottom() + 0.5);
    }
}

QPointF PlotWidget::dataToPixel(const QPointF& p, const QRectF& view, const QRectF& pr) const
{
    const double x = pr.left() + (p.x() - view.left()) / view.width() * pr.width();
    const double y = pr.bottom() - (p.y() - view.top()) / view.height() * pr.height();
    return QPointF(x, y);
}

QPointF PlotWidget::pixelToData(const QPointF& p, const QRectF& view, const QRectF& pr) const
{
    const double x = view.left() + (p.x() - pr.left()) / pr.width() * view.width();
    const double y = view.top() + (pr.bottom() - p.y()) / pr.height() * view.height();
    return QPointF(x, y);
}

QVector<QPointF> PlotWidget::displayPointsForSeries(const SignalSeries& series, const QRectF& view, double pixelWidth) const
{
    if (!series.hasUniformData()) {
        return pixelColumnEnvelope(series,
                                   series.points.size(),
                                   view,
                                   pixelWidth,
                                   [&](int index) { return series.points[index]; });
    }
    return pixelColumnEnvelope(series,
                               series.uniformY.size(),
                               view,
                               pixelWidth,
                               [&](int index) { return series.pointAt(index); });
}

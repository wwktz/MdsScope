// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.hpp"
#include "plot_widget.hpp"

#include <QFont>
#include <QFontMetrics>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace {

double localXResolution(const SignalSeries& series, double x)
{
    if (series.hasUniformData() && std::isfinite(series.uniformStep) && series.uniformStep != 0.0) {
        return std::abs(series.uniformStep);
    }
    const QVector<QPointF>& pointSamples = series.points;
    if (pointSamples.size() < 2) {
        return qQNaN();
    }

    const bool ascending = pointSamples.first().x() <= pointSamples.last().x();
    const auto it = ascending
                        ? std::lower_bound(pointSamples.cbegin(),
                                           pointSamples.cend(),
                                           x,
                                           [](const QPointF& point, double value) {
                                               return point.x() < value;
                                           })
                        : std::lower_bound(pointSamples.cbegin(),
                                           pointSamples.cend(),
                                           x,
                                           [](const QPointF& point, double value) {
                                               return point.x() > value;
                                           });
    const int index = std::clamp(static_cast<int>(std::distance(pointSamples.cbegin(), it)),
                                 0,
                                 static_cast<int>(pointSamples.size()) - 1);
    double resolution = std::numeric_limits<double>::infinity();
    if (index > 0) {
        const double step = std::abs(pointSamples[index].x() - pointSamples[index - 1].x());
        if (step > 0.0) {
            resolution = std::min(resolution, step);
        }
    }
    if (index + 1 < pointSamples.size()) {
        const double step = std::abs(pointSamples[index + 1].x() - pointSamples[index].x());
        if (step > 0.0) {
            resolution = std::min(resolution, step);
        }
    }
    return std::isfinite(resolution) ? resolution : qQNaN();
}

QString trimFixedNumber(QString text)
{
    if (!text.contains('.')) {
        return text;
    }
    while (text.endsWith('0')) {
        text.chop(1);
    }
    if (text.endsWith('.')) {
        text.chop(1);
    }
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString formatPointX(double value, const SignalSeries& series)
{
    const double resolution = localXResolution(series, value);
    if (!std::isfinite(resolution) || resolution <= 0.0) {
        return QString::number(value, 'g', 12);
    }

    int exponent = static_cast<int>(std::floor(std::log10(resolution)));
    double mantissa = resolution / std::pow(10.0, exponent);
    // Infer a human-scale sampling step (for example 1e-6, 2.5e-4 or
    // 1.00016e-4 -> 1e-4) instead of preserving subtraction noise.
    mantissa = std::round(mantissa * 100.0) / 100.0;
    if (mantissa >= 10.0) {
        mantissa /= 10.0;
        ++exponent;
    }

    int mantissaDecimals = 2;
    double scale = 1.0;
    for (int decimals = 0; decimals <= 2; ++decimals) {
        if (std::abs(mantissa * scale - std::round(mantissa * scale)) < 1e-7) {
            mantissaDecimals = decimals;
            break;
        }
        scale *= 10.0;
    }
    const int initialDecimals = std::max(0, -exponent + mantissaDecimals);
    const double roundingTolerance = std::max(
        resolution * 1e-3,
        std::abs(value) * std::numeric_limits<double>::epsilon() * 4.0);
    for (int decimals = initialDecimals; decimals <= 15; ++decimals) {
        const QString formatted = QString::number(value, 'f', decimals);
        bool ok = false;
        const double roundedValue = formatted.toDouble(&ok);
        if (ok && std::abs(roundedValue - value) <= roundingTolerance) {
            return trimFixedNumber(formatted);
        }
    }
    return QString::number(value, 'g', 16);
}

QString pointReadoutText(const SignalSeries& series, const QPointF& point, bool detailed)
{
    if (!detailed) {
        return QStringLiteral("%1, %2")
            .arg(formatPointX(point.x(), series), QString::number(point.y(), 'g', 6));
    }
    // Point-mode values are for inspection, not lossless export. Uniform Y
    // data is stored as float, so showing more than seven digits exposes its
    // binary representation rather than meaningful signal precision.
    const int yDigits = series.hasUniformData() ? 7 : 8;
    return QStringLiteral("%1, %2")
        .arg(formatPointX(point.x(), series), QString::number(point.y(), 'g', yDigits));
}

bool sameDataX(double lhs, double rhs)
{
    const double scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= std::numeric_limits<double>::epsilon() * scale * 8.0;
}

} // namespace

int PlotWidget::legendSeriesAt(const QPointF& pixelPos) const
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        return -1;
    }

    const FontSettings& fonts = fontSettings_;
    QFont legendFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
    const QFontMetrics legendFm(legendFont);
    int legendWidth = 0;
    for (const QString& label : legendLabels_) {
        legendWidth = std::max(legendWidth, legendFm.horizontalAdvance(label));
    }
    if (legendLabels_.isEmpty()) {
        return -1;
    }

    legendWidth += 20;
    const int legendLineHeight = std::max(10, legendFm.height());
    const int legendHeight =
        static_cast<int>(legendLabels_.size()) * legendLineHeight + 4;
    const QRectF legendArea = pr.adjusted(2, 2, -2, -2);
    const double legendW = std::min<double>(legendWidth, std::max(1.0, legendArea.width()));
    const double legendH = std::min<double>(legendHeight, std::max(1.0, legendArea.height()));
    const QRectF legendRect(legendArea.right() - legendW - 1,
                            legendArea.top() + 1,
                            legendW,
                            legendH);
    if (!legendRect.adjusted(-2, -2, 2, 2).contains(pixelPos)) {
        return -1;
    }

    for (int i = 0; i < legendLabels_.size(); ++i) {
        const QRectF rowRect(legendRect.left(),
                             legendRect.top() + 2 + i * legendLineHeight,
                             legendRect.width(),
                             legendLineHeight);
        if (rowRect.adjusted(-2, -1, 2, 1).contains(pixelPos)) {
            return legendSeriesIndexes_.value(i, i);
        }
    }
    return -1;
}

bool PlotWidget::nearestPointForSeries(int seriesIndex,
                                       double dataX,
                                       QPointF* point,
                                       QPointF* pixel) const
{
    if (seriesIndex < 0 || seriesIndex >= series_.size()) {
        return false;
    }
    if (seriesIndex < spec_.signalSpecs.size() && spec_.signalSpecs[seriesIndex].hidden) {
        return false;
    }
    const SignalSeries& s = series_[seriesIndex];
    if (!s.hasData() || !std::isfinite(dataX)) {
        return false;
    }

    QPointF bestPoint;
    auto acceptPointByX = [&](const QPointF& p) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
            return false;
        }
        bestPoint = p;
        return true;
    };

    bool found = false;
    if (s.hasUniformData() && s.uniformStep != 0.0) {
        int index = static_cast<int>(std::llround((dataX - s.uniformStart) / s.uniformStep));
        index = std::clamp(index, 0, static_cast<int>(s.uniformY.size()) - 1);
        found = acceptPointByX(s.pointAt(index));
    } else if (!s.points.isEmpty()) {
        const QVector<QPointF>& pointSamples = s.points;
        const bool ascending = pointSamples.first().x() <= pointSamples.last().x();
        const auto it = ascending
                            ? std::lower_bound(pointSamples.cbegin(),
                                               pointSamples.cend(),
                                               dataX,
                                               [](const QPointF& sample, double value) {
                                                   return sample.x() < value;
                                               })
                            : std::lower_bound(pointSamples.cbegin(),
                                               pointSamples.cend(),
                                               dataX,
                                               [](const QPointF& sample, double value) {
                                                   return sample.x() > value;
                                               });
        int index = std::clamp(static_cast<int>(std::distance(pointSamples.cbegin(), it)),
                               0,
                               static_cast<int>(pointSamples.size()) - 1);
        if (index > 0 && std::abs(pointSamples[index - 1].x() - dataX) <= std::abs(pointSamples[index].x() - dataX)) {
            --index;
        }
        found = acceptPointByX(pointSamples[index]);
    }

    if (!found) {
        return false;
    }
    if (point) {
        *point = bestPoint;
    }
    if (pixel) {
        *pixel = dataToPixel(bestPoint, effectiveView(), plotRect());
    }
    return true;
}

bool PlotWidget::pointForSeriesX(int seriesIndex,
                                 double dataX,
                                 bool interpolate,
                                 QPointF* point,
                                 QPointF* pixel) const
{
    if (!interpolate) {
        return nearestPointForSeries(seriesIndex, dataX, point, pixel);
    }
    if (seriesIndex < 0 || seriesIndex >= series_.size()
        || !signalVisible(spec_, seriesIndex) || !series_[seriesIndex].hasData()
        || !std::isfinite(dataX)) {
        return false;
    }

    const SignalSeries& series = series_[seriesIndex];
    auto acceptPoint = [&](const QPointF& candidate) {
        if (!std::isfinite(candidate.x()) || !std::isfinite(candidate.y())) {
            return false;
        }
        if (point) {
            *point = candidate;
        }
        if (pixel) {
            *pixel = dataToPixel(candidate, effectiveView(), plotRect());
        }
        return true;
    };
    auto interpolateBetween = [&](const QPointF& left, const QPointF& right) {
        if (sameDataX(dataX, left.x())) {
            return acceptPoint(left);
        }
        if (sameDataX(dataX, right.x())) {
            return acceptPoint(right);
        }
        const double dx = right.x() - left.x();
        if (!std::isfinite(left.y()) || !std::isfinite(right.y())
            || !std::isfinite(dx) || dx == 0.0) {
            return false;
        }
        const double y = left.y() + (right.y() - left.y()) * (dataX - left.x()) / dx;
        return acceptPoint(QPointF(dataX, y));
    };
    auto nearestFallback = [&] {
        return nearestPointForSeries(seriesIndex, dataX, point, pixel);
    };

    if (series.hasUniformData()) {
        const int count = series.pointCount();
        if (count < 2 || !std::isfinite(series.uniformStep) || series.uniformStep == 0.0) {
            return nearestFallback();
        }
        const double firstX = series.uniformStart;
        const double lastX = series.uniformStart + static_cast<double>(count - 1) * series.uniformStep;
        if (dataX < std::min(firstX, lastX) || dataX > std::max(firstX, lastX)) {
            return nearestFallback();
        }

        const double logicalIndex = (dataX - series.uniformStart) / series.uniformStep;
        const int nearestIndex = std::clamp(static_cast<int>(std::llround(logicalIndex)), 0, count - 1);
        const QPointF nearest = series.pointAt(nearestIndex);
        if (sameDataX(dataX, nearest.x())) {
            return acceptPoint(nearest);
        }

        const int leftIndex = std::clamp(static_cast<int>(std::floor(logicalIndex)), 0, count - 1);
        const int rightIndex = std::clamp(leftIndex + 1, 0, count - 1);
        if (leftIndex == rightIndex) {
            return nearestFallback();
        }
        if (interpolateBetween(series.pointAt(leftIndex), series.pointAt(rightIndex))) {
            return true;
        }
        return nearestFallback();
    }

    const QVector<QPointF>& samples = series.points;
    if (samples.size() < 2) {
        return nearestFallback();
    }
    const bool ascending = samples.first().x() <= samples.last().x();
    const double minX = std::min(samples.first().x(), samples.last().x());
    const double maxX = std::max(samples.first().x(), samples.last().x());
    if (dataX < minX || dataX > maxX) {
        return nearestFallback();
    }

    auto upper = ascending
                     ? std::lower_bound(samples.cbegin(), samples.cend(), dataX, [](const QPointF& sample, double value) {
                           return sample.x() < value;
                       })
                     : std::lower_bound(samples.cbegin(), samples.cend(), dataX, [](const QPointF& sample, double value) {
                           return sample.x() > value;
                       });
    if (upper != samples.cend() && sameDataX(dataX, upper->x())) {
        return acceptPoint(*upper);
    }
    if (upper == samples.cbegin() || upper == samples.cend()) {
        return nearestFallback();
    }
    const QPointF& left = *(upper - 1);
    const QPointF& right = *upper;
    if (interpolateBetween(left, right)) {
        return true;
    }
    return nearestFallback();
}

double PlotWidget::curvePickRadius() const
{
    return largeDisplayMode_ ? 22.0 : 16.0;
}

int PlotWidget::firstVisibleSeriesWithData() const
{
    for (int i = 0; i < series_.size(); ++i) {
        if (signalVisible(spec_, i) && series_[i].hasData()) {
            return i;
        }
    }
    return -1;
}

int PlotWidget::nearestSeriesAtPixel(const QPointF& pixelPos, double maxDistance, QPointF* point, QPointF* pixel) const
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        return -1;
    }

    // Measure against the drawn polyline segments, not just their vertices: on a
    // steep edge two consecutive vertices can be a whole panel apart, and the
    // cursor legitimately lands in the middle of that segment.
    const QVector<QVector<QPoint>>& polylines = renderedPolylines();
    int bestSeries = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestX = qQNaN();
    for (int i = 0; i < polylines.size(); ++i) {
        const QVector<QPoint>& polyline = polylines[i];
        if (polyline.size() < 2 || !series_[i].hasData()) {
            continue;
        }
        for (int j = 0; j + 1 < polyline.size(); ++j) {
            const QPointF a(polyline[j]);
            const QPointF b(polyline[j + 1]);
            // Cheap reject: the cursor cannot be within maxDistance of a segment
            // whose bounding box it misses by more than that.
            if (pixelPos.x() < std::min(a.x(), b.x()) - maxDistance
                || pixelPos.x() > std::max(a.x(), b.x()) + maxDistance
                || pixelPos.y() < std::min(a.y(), b.y()) - maxDistance
                || pixelPos.y() > std::max(a.y(), b.y()) + maxDistance) {
                continue;
            }
            const QPointF ab = b - a;
            const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
            double t = 0.0;
            if (lengthSquared > 0.0) {
                const QPointF ap = pixelPos - a;
                t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / lengthSquared, 0.0, 1.0);
            }
            const QPointF closest = a + ab * t;
            const double distance = std::hypot(closest.x() - pixelPos.x(), closest.y() - pixelPos.y());
            if (distance < bestDistance) {
                bestSeries = i;
                bestDistance = distance;
                bestX = closest.x();
            }
        }
    }
    if (bestSeries < 0 || bestDistance > maxDistance) {
        return -1;
    }

    // Report the real sample under the picked x so the readout stays on data.
    const double dataX = pixelToData(QPointF(bestX, pixelPos.y()), effectiveView(), pr).x();
    if (!nearestPointForSeries(bestSeries, dataX, point, pixel)) {
        return -1;
    }
    return bestSeries;
}

bool PlotWidget::updateHoverForSeriesX(int seriesIndex, double dataX, bool lockSeries)
{
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !signalVisible(spec_, seriesIndex) || !series_[seriesIndex].hasData()) {
        seriesIndex = firstVisibleSeriesWithData();
    }
    QPointF point;
    QPointF pixel;
    if (seriesIndex < 0 || !nearestPointForSeries(seriesIndex, dataX, &point, &pixel)) {
        const bool changed = !hoverText_.isEmpty();
        hoverText_.clear();
        return changed;
    }

    const QString valueText = pointReadoutText(series_[seriesIndex], point, largeDisplayMode_);
    const QString nextText = valueText;
    const bool nextLocked = lockSeries ? true : hoverSeriesLocked_;
    const bool changed = hoverSeriesIndex_ != seriesIndex
                         || hoverSeriesLocked_ != nextLocked
                         || hoverPixel_ != pixel
                         || hoverData_ != point
                         || hoverText_ != nextText;
    hoverSeriesIndex_ = seriesIndex;
    hoverSeriesLocked_ = nextLocked;
    hoverPixel_ = pixel;
    hoverData_ = point;
    hoverText_ = nextText;
    return changed;
}

bool PlotWidget::activatePointAtViewCenter(int seriesIndex)
{
    if (interactionMode_ != InteractionMode::Point) {
        return false;
    }
    if (seriesIndex < 0) {
        seriesIndex = firstVisibleSeriesWithData();
    } else if (seriesIndex >= series_.size()
               || !signalVisible(spec_, seriesIndex)
               || !series_[seriesIndex].hasData()) {
        return false;
    }
    if (seriesIndex < 0) {
        return false;
    }

    const QRectF view = effectiveView();
    const double centerX =
        view.left() + view.width() * 0.5;
    updateHoverForSeriesX(seriesIndex, centerX, true);
    if (hoverText_.isEmpty()) {
        return false;
    }
    pointTrackingActive_ = true;
    setSyncedPointX(hoverData_.x(), hoverSeriesIndex_);
    emit pointXChanged(hoverData_.x());
    update();
    return true;
}

bool PlotWidget::activatePointAtViewCenterForDataSeries(int ordinal)
{
    if (ordinal < 0) {
        return false;
    }
    int dataOrdinal = 0;
    for (int seriesIndex : std::as_const(legendSeriesIndexes_)) {
        if (seriesIndex < 0 || seriesIndex >= series_.size()
            || !signalVisible(spec_, seriesIndex)
            || !series_[seriesIndex].hasData()) {
            continue;
        }
        if (dataOrdinal == ordinal) {
            return activatePointAtViewCenter(seriesIndex);
        }
        ++dataOrdinal;
    }
    return false;
}

bool PlotWidget::stepActivePoint(int delta)
{
    if (interactionMode_ != InteractionMode::Point || delta == 0 || hoverText_.isEmpty()) {
        return false;
    }
    int seriesIndex = hoverSeriesIndex_;
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !series_[seriesIndex].hasData()) {
        return false;
    }

    const SignalSeries& s = series_[seriesIndex];
    double nextX = qQNaN();
    if (s.hasUniformData() && s.uniformStep != 0.0) {
        int index = static_cast<int>(std::llround((hoverData_.x() - s.uniformStart) / s.uniformStep));
        index = std::clamp(index + delta, 0, static_cast<int>(s.uniformY.size()) - 1);
        nextX = s.uniformStart + static_cast<double>(index) * s.uniformStep;
    } else if (!s.points.isEmpty()) {
        const QVector<QPointF>& pointSamples = s.points;
        const bool ascending = pointSamples.first().x() <= pointSamples.last().x();
        const auto it = ascending
                            ? std::lower_bound(pointSamples.cbegin(),
                                               pointSamples.cend(),
                                               hoverData_.x(),
                                               [](const QPointF& sample, double value) {
                                                   return sample.x() < value;
                                               })
                            : std::lower_bound(pointSamples.cbegin(),
                                               pointSamples.cend(),
                                               hoverData_.x(),
                                               [](const QPointF& sample, double value) {
                                                   return sample.x() > value;
                                               });
        int index = std::clamp(static_cast<int>(std::distance(pointSamples.cbegin(), it)),
                               0,
                               static_cast<int>(pointSamples.size()) - 1);
        if (index > 0 && std::abs(pointSamples[index - 1].x() - hoverData_.x()) <= std::abs(pointSamples[index].x() - hoverData_.x())) {
            --index;
        }
        index = std::clamp(index + delta, 0, static_cast<int>(pointSamples.size()) - 1);
        nextX = pointSamples[index].x();
    }

    if (!std::isfinite(nextX)) {
        return false;
    }
    const bool changed = updateHoverForSeriesX(seriesIndex, nextX, true);
    setSyncedPointX(hoverData_.x(), seriesIndex);
    if (changed && !hoverText_.isEmpty()) {
        emit pointXChanged(hoverData_.x());
    }
    return changed;
}

void PlotWidget::setSyncedPointX(double x, int seriesIndex, bool interpolate)
{
    if (interactionMode_ != InteractionMode::Point || !std::isfinite(x)) {
        clearSyncedPoint();
        return;
    }
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !signalVisible(spec_, seriesIndex) || !series_[seriesIndex].hasData()) {
        seriesIndex = firstVisibleSeriesWithData();
    }

    PointReadout next;
    QPointF point;
    QPointF pixel;
    if (pointForSeriesX(seriesIndex, x, interpolate, &point, &pixel)) {
        next.plotRect = plotRect();
        next.pixel = pixel;
        next.data = point;
        next.visible = next.plotRect.contains(next.pixel);
        next.showText = true;
        next.color = seriesColor(seriesIndex);
    }

    const bool samePoint = syncedPoint_.visible == next.visible
                           && syncedPoint_.showText == next.showText
                           && syncedPoint_.color == next.color
                           && qRound(syncedPoint_.pixel.x()) == qRound(next.pixel.x())
                           && qRound(syncedPoint_.pixel.y()) == qRound(next.pixel.y())
                           && syncedPoint_.data == next.data
                           && syncedPoint_.plotRect.toAlignedRect() == next.plotRect.toAlignedRect();
    if (samePoint) {
        return;
    }
    if (next.visible) {
        next.text = pointReadoutText(series_[seriesIndex], point, largeDisplayMode_);
        next.placementIndex = syncedPoint_.placementIndex;
        updatePointReadoutPlacement(next);
    }

    QRect dirty = syncedPointDirtyRect(syncedPoint_).united(syncedPointDirtyRect(next));
    syncedPoint_ = std::move(next);
    if (dirty.isValid() && !dirty.isEmpty()) {
        update(dirty);
    } else {
        update();
    }
}

void PlotWidget::clearSyncedPoint()
{
    if (!syncedPoint_.visible && syncedPoint_.text.isEmpty()) {
        return;
    }
    const QRect dirty = syncedPointDirtyRect(syncedPoint_);
    syncedPoint_ = {};
    if (dirty.isValid() && !dirty.isEmpty()) {
        update(dirty);
    } else {
        update();
    }
}

bool PlotWidget::updateHover(const QPointF& pixelPos, bool lockSeries)
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        const bool changed = !hoverText_.isEmpty();
        hoverText_.clear();
        if (!hoverSeriesLocked_) {
            hoverSeriesIndex_ = -1;
        }
        return changed;
    }
    const QRectF view = effectiveView();
    const QPointF dataPoint = pixelToData(pixelPos, view, pr);

    if (hoverSeriesLocked_ && !lockSeries && hoverSeriesIndex_ >= 0 && hoverSeriesIndex_ < series_.size()) {
        if (updateHoverForSeriesX(hoverSeriesIndex_, dataPoint.x(), false)) {
            return true;
        }
        return false;
    }

    QPointF point;
    QPointF pixel;
    const int pickedSeries = nearestSeriesAtPixel(pixelPos, curvePickRadius(), &point, &pixel);
    if (pickedSeries >= 0) {
        return updateHoverForSeriesX(pickedSeries, point.x(), lockSeries);
    } else if (hoverSeriesLocked_ && hoverSeriesIndex_ >= 0 && hoverSeriesIndex_ < series_.size()) {
        return updateHoverForSeriesX(hoverSeriesIndex_, dataPoint.x(), false);
    } else {
        const QString nextText =
            QString("%1, %2").arg(dataPoint.x(), 0, 'g', 6).arg(dataPoint.y(), 0, 'g', 6);
        const bool changed =
            hoverPixel_ != pixelPos || hoverData_ != dataPoint || hoverText_ != nextText;
        hoverPixel_ = pixelPos;
        hoverData_ = dataPoint;
        hoverText_ = nextText;
        return changed;
    }
}

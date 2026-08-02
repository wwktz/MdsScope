// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"
#include "core/font_settings.hpp"

#include <QImage>
#include <QPixmap>
#include <QWidget>

class QFontMetrics;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class ShortcutDispatchTestAccess;

class PlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setFontSettings(FontSettings settings);
    void setSpec(PlotSpec spec);
    void setFetchSpec(PlotSpec spec);
    void setShot(QString shot);
    const PlotSpec& spec() const { return spec_; }
    bool hasSeriesData(int index) const;
    QVector<SignalSeries> seriesSnapshot() const;
    void setSeries(int index, SignalSeries series);
    void clearSeries();
    void clearSeriesPreservingView();
    void setSelected(bool selected);
    void setLargeDisplayMode(bool enabled);
    void refreshStyle();
    void setInteractionMode(InteractionMode mode);
    void resetScale(bool repaint = true);
    void applyView(const QRectF& view);
    void applyXRangeAutoY(double xmin, double xmax);
    void applyYRangeKeepX(double ymin, double ymax);
    QRectF currentView() const;
    bool hasView() const { return hasView_; }
    void setSyncedPointX(double x, int seriesIndex, bool interpolate = false);
    void clearSyncedPoint();
    void deactivatePointTracking();
    bool pausePointTracking();
    bool resumePointTracking();
    bool activatePointAtViewCenter(int seriesIndex = -1);
    bool activatePointAtViewCenterForDataSeries(int ordinal);
    bool pointTrackingActive() const { return pointTrackingActive_; }
    void moveCursorToActivePoint() const;
    bool stepActivePoint(int delta);
    int activePointSeriesIndex() const { return hoverSeriesIndex_; }
    double activePointX() const { return hoverData_.x(); }

signals:
    void selected();
    void pointXChanged(double x);
    void pointTrackingPaused();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent*) override;

private:
    friend class ShortcutDispatchTestAccess;

    QRectF plotRect() const;
    QRectF dataBounds() const;
    QRectF effectiveView() const;
    QPointF dataToPixel(const QPointF& p,
                        const QRectF& view,
                        const QRectF& pr) const;
    QPointF pixelToData(const QPointF& p,
                        const QRectF& view,
                        const QRectF& pr) const;
    QColor seriesColor(int index) const;
    void rebuildSeriesColorCache();
    void rebuildLegendCache();
    void expandFlatRange(QRectF& range) const;
    QVector<QPointF> displayPointsForSeries(const SignalSeries& series,
                                            const QRectF& view,
                                            double pixelWidth) const;
    const QVector<QVector<QPoint>>& renderedPolylines() const;
    bool updateHover(const QPointF& pixelPos, bool lockSeries = false);
    bool updateHoverForSeriesX(int seriesIndex,
                               double dataX,
                               bool lockSeries);
    bool nearestPointForSeries(int seriesIndex,
                               double dataX,
                               QPointF* point,
                               QPointF* pixel) const;
    bool pointForSeriesX(int seriesIndex,
                         double dataX,
                         bool interpolate,
                         QPointF* point,
                         QPointF* pixel) const;
    int nearestSeriesAtPixel(const QPointF& pixelPos,
                             double maxDistance,
                             QPointF* point,
                             QPointF* pixel) const;
    double curvePickRadius() const;
    int firstVisibleSeriesWithData() const;
    int legendSeriesAt(const QPointF& pixelPos) const;
    void renderBasePlot(QPainter& painter) const;
    void drawSyncedPoint(QPainter& painter) const;
    void drawZoomRubberBand(QPainter& painter) const;
    void drawSelectionBorder(QPainter& painter) const;
    QRectF pointReadoutArea(const PointReadout& readout) const;
    QRectF pointReadoutTextRect(const PointReadout& readout,
                                const QFontMetrics& metrics,
                                bool* needsBackground = nullptr) const;
    void updatePointReadoutPlacement(PointReadout& readout) const;
    QRect selectionBorderDirtyRect() const;
    QRect zoomRubberBandDirtyRect(const QRectF& band) const;
    QRect syncedPointDirtyRect(const PointReadout& readout) const;
    void invalidatePlotCache();
    void scheduleUpdate();
    void schedulePointHoverUpdate(const QPointF& pixelPos);

    PlotSpec spec_;
    FontSettings fontSettings_;
    QVector<SignalSeries> series_;
    QVector<QColor> seriesColors_;
    QStringList legendLabels_;
    QVector<int> legendSeriesIndexes_;
    QRectF view_;
    mutable QRectF cachedDataBounds_;
    mutable bool dataBoundsDirty_ = true;
    bool hasView_ = false;
    bool selected_ = false;
    bool largeDisplayMode_ = false;
    InteractionMode interactionMode_ = InteractionMode::Point;
    bool dragging_ = false;
    bool zooming_ = false;
    bool updateQueued_ = false;
    QPointF lastDragPos_;
    QPointF zoomStart_;
    QRectF zoomRubberBand_;
    QPointF hoverPixel_;
    QPointF hoverData_;
    QString hoverText_;
    PointReadout syncedPoint_;
    mutable QPixmap baseCache_;
    mutable QImage curveOccupancyMask_;
    mutable QSize baseCacheSize_;
    mutable bool baseCacheDirty_ = true;
    mutable QVector<QVector<QPoint>> polylineCache_;
    mutable QRectF polylineCacheRect_;
    mutable QRectF polylineCacheView_;
    mutable bool polylineCacheDirty_ = true;
    int hoverSeriesIndex_ = -1;
    bool hoverSeriesLocked_ = false;
    bool pointTrackingActive_ = false;
    bool pointHoverQueued_ = false;
    int pointHoverGeneration_ = 0;
    QPointF pendingPointHoverPos_;
};

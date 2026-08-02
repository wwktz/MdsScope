// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/plot/helpers.hpp"
#include "ui/plot/plot_widget.hpp"

#include <QApplication>

#include <cmath>

namespace {
bool near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 1e-9;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    double min = 0.0;
    double max = 10.0;
    fillMissingAxisBounds(&min, &max, 0.0, 1.0);
    if (!near(min, 0.0) || !near(max, 10.0)) {
        return 1;
    }

    min = std::numeric_limits<double>::infinity();
    max = -std::numeric_limits<double>::infinity();
    fillMissingAxisBounds(&min, &max, -1.0, 1.0);
    if (!near(min, -1.0) || !near(max, 1.0)) {
        return 2;
    }

    const QVector<QPointF> descending {
        QPointF(10.0, 2.0),
        QPointF(5.0, 20.0),
        QPointF(0.0, 4.0),
    };
    const QPair<int, int> range = sortedPointIndexRange(descending, 4.0, 6.0);
    if (range.first != 1 || range.second != 1) {
        return 3;
    }
    const QPair<int, int> outside = sortedPointIndexRange(descending, 20.0, 30.0);
    if (outside.first != -1 || outside.second != -1) {
        return 4;
    }

    PlotWidget plot;
    plot.resize(640, 360);
    PlotSpec spec;
    spec.signalSpecs.resize(3);
    spec.signalSpecs[0].yExpr = QStringLiteral("empty");
    spec.signalSpecs[1].yExpr = QStringLiteral("first");
    spec.signalSpecs[2].yExpr = QStringLiteral("second");
    plot.setSpec(spec);
    SignalSeries first;
    first.uniformStart = 0.0;
    first.uniformStep = 1.0;
    first.uniformY = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F};
    SignalSeries second;
    second.uniformStart = 10.0;
    second.uniformStep = 2.0;
    second.uniformY = {5.0F, 6.0F, 7.0F, 8.0F, 9.0F};
    plot.setSeries(1, first);
    plot.setSeries(2, second);

    if (!plot.activatePointAtViewCenter()
        || !plot.pointTrackingActive()
        || plot.activePointSeriesIndex() != 1
        || !near(plot.activePointX(), 4.0)) {
        return 5;
    }
    if (!plot.activatePointAtViewCenterForDataSeries(1)
        || plot.activePointSeriesIndex() != 2
        || !near(plot.activePointX(), 10.0)) {
        return 6;
    }
    if (!plot.pausePointTracking()
        || plot.pointTrackingActive()
        || plot.activePointSeriesIndex() != 2
        || !near(plot.activePointX(), 10.0)) {
        return 7;
    }
    if (!plot.resumePointTracking()
        || !plot.pointTrackingActive()
        || plot.activePointSeriesIndex() != 2
        || !near(plot.activePointX(), 10.0)) {
        return 8;
    }
    if (plot.activatePointAtViewCenterForDataSeries(2)) {
        return 9;
    }
    return plot.activatePointAtViewCenter(8) ? 10 : 0;
}

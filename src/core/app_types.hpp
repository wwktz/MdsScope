// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHashFunctions>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

struct InternalWebBookmark {
    QString alias;
    QString url;
};

enum class InteractionMode {
    Zoom,
    Point,
    Pan,
};

enum class DataReadMode {
    Thin,    // Prefer the prepared EAST _s signal; otherwise use Medium
    Medium,  // Server-side 0.1 ms average resolution where native data is denser
    Full,    // All data, slowest, highest precision
};

enum class ThemeMode {
    Auto = 0,
    Light = 1,
    Dark = 2,
};

struct SignalSpec {
    QString shot;
    QString yExpr;
    QString xExpr;
    QString experiment;
    QString serverIp;
    QString colorName;
    bool manualColor = false;
    bool hidden = false;
    DataReadMode readMode = DataReadMode::Thin;
    // False means no source Rate was present in the parsed configuration.
    // The UI materializes the current default/global minimum in memory; true
    // also allows an explicit Thin choice to be written when the user saves.
    bool readModeExplicit = false;
};

struct PlotSpec {
    QString shot;
    QString title;
    QString xLabel;
    QString yLabel;
    int extractionPoints = 2000;
    bool grid = true;
    bool customXRange = false;
    bool customYRange = false;
    double xmin = qQNaN();
    double xmax = qQNaN();
    double ymin = qQNaN();
    double ymax = qQNaN();
    QVector<SignalSpec> signalSpecs;
};

struct LayoutConfig {
    QString filePath;
    QVector<QVector<PlotSpec>> columns;
};

struct PanelId {
    int column = -1;
    int row = -1;

    bool operator==(const PanelId&) const = default;
};

inline size_t qHash(const PanelId& panel, size_t seed = 0) noexcept
{
    return qHashMulti(seed, panel.column, panel.row);
}

struct SignalSeries {
    QString name;
    QString unit;
    QString error;
    QVector<QPointF> points;
    QVector<float> uniformY;
    QVector<float> minYBlocks;
    QVector<float> maxYBlocks;
    double uniformStart = 0.0;
    double uniformStep = 1.0;
    double uniformMinY = qQNaN();
    double uniformMaxY = qQNaN();
    int minMaxBlockSize = 0;

    bool hasUniformData() const { return !uniformY.isEmpty(); }
    bool hasData() const { return hasUniformData() || !points.isEmpty(); }
    int pointCount() const
    {
        return hasUniformData()
                   ? static_cast<int>(uniformY.size())
                   : static_cast<int>(points.size());
    }
    QPointF pointAt(int index) const
    {
        if (hasUniformData()) {
            return QPointF(uniformStart + static_cast<double>(index) * uniformStep,
                           uniformY[index]);
        }
        return points[index];
    }
};

struct LoadedSignal {
    int column = -1;
    int row = -1;
    int signal = -1;
    QString shot;
    SignalSeries series;
};

struct PointReadout {
    bool visible = false;
    bool showText = true;
    QRectF plotRect;
    QPointF pixel;
    QPointF data;
    QString text;
    QColor color = QColor("#333333");
    int placementIndex = -1;
    bool needsBackground = false;
};

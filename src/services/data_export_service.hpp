// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QDir>
#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>

enum class ExportFormat {
    Text,
    Csv,
    Tsv,
    Json,
};

enum class ExportRange {
    AllData,
    CurrentView,
    CustomXRange,
};

struct DataExportRequest {
    LayoutConfig snapshot;
    QString baseDirPath;
    DataReadMode readMode = DataReadMode::Thin;
    ExportFormat format = ExportFormat::Text;
    bool useCurrentView = false;
    bool useCustomRange = false;
    double customXMin = qQNaN();
    double customXMax = qQNaN();
    QHash<PanelId, QRectF> viewRanges;
};

struct DataExportResult {
    int written = 0;
    QString outputPath;
    QStringList errors;
};

QString exportFileToken(QString text);
QString exportFormatExtension(ExportFormat format);
QString exportFormatSettingValue(ExportFormat format);
ExportFormat exportFormatFromSetting(QString value);
QString uniqueExportPath(const QDir& dir,
                         const QString& baseName,
                         ExportFormat format);
QString exportRangeFileSuffix(bool useXRange, double xmin, double xmax);
bool writeSeriesDataFile(const QString& path,
                         const SignalSeries& series,
                         ExportFormat format,
                         bool useXRange,
                         double xmin,
                         double xmax,
                         QString* error);
DataExportResult runDataExport(const DataExportRequest& request);

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "services/data_export_service.hpp"

#include <QPair>
#include <QString>
#include <QVector>

#include <optional>

class QWidget;

struct ExportDialogResult {
    QVector<QPair<int, int>> panels;
    QVector<int> signalIndexes;
    QString outputBaseDir;
    ExportFormat format = ExportFormat::Text;
    ExportRange range = ExportRange::AllData;
    double customXMin = qQNaN();
    double customXMax = qQNaN();
};

std::optional<ExportDialogResult> selectExportData(
    const LayoutConfig& config,
    const QString& defaultDir,
    ExportFormat defaultFormat,
    QWidget* parent = nullptr,
    const PlotSpec* signalPlot = nullptr);

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/mds_helpers.hpp"
#include "main_window.hpp"
#include "export_dialog.hpp"
#include "ui/plot/plot_widget.hpp"
#include "shared.hpp"
#include "user_preferences.hpp"
#include "services/data_export_service.hpp"

#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QThreadPool>

void MainWindow::openExportDataDialog()
{
    const ExportFormat defaultFormat =
        exportFormatFromSetting(preferences_->exportFormat());
    const std::optional<ExportDialogResult> selection =
        selectExportData(
            config_, exportBasePath_, defaultFormat, this);
    if (!selection) {
        return;
    }

    preferences_->setExportFormat(
        exportFormatSettingValue(selection->format));
    exportDataForPanels(selection->panels,
                        selection->outputBaseDir,
                        static_cast<int>(selection->format),
                        static_cast<int>(selection->range),
                        selection->customXMin,
                        selection->customXMax);
}

void MainWindow::exportCurrentPanelData()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    if (selectedColumn_ >= config_.columns.size() || selectedRow_ >= config_.columns[selectedColumn_].size()) {
        return;
    }
    const ExportFormat defaultFormat =
        exportFormatFromSetting(preferences_->exportFormat());
    LayoutConfig dialogConfig;
    dialogConfig.columns = {{config_.columns[selectedColumn_][selectedRow_]}};
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    if (!currentShot.isEmpty()) {
        dialogConfig.columns[0][0].shot = currentShot;
    }
    dialogConfig = expandedShotLayout(dialogConfig);
    const PlotSpec& plot = dialogConfig.columns[0][0];
    const std::optional<ExportDialogResult> selection =
        selectExportData(
            config_, exportBasePath_, defaultFormat, this, &plot);
    if (!selection) {
        return;
    }

    preferences_->setExportFormat(
        exportFormatSettingValue(selection->format));
    QSet<int> selectedSignalIndexes;
    for (int signal : selection->signalIndexes) {
        selectedSignalIndexes.insert(signal);
    }
    QHash<PanelId, QSet<int>> signalFilter;
    signalFilter.insert({selectedColumn_, selectedRow_}, selectedSignalIndexes);
    exportDataForPanels({{selectedColumn_, selectedRow_}},
                        selection->outputBaseDir,
                        static_cast<int>(selection->format),
                        static_cast<int>(selection->range),
                        selection->customXMin,
                        selection->customXMax,
                        signalFilter);
}

void MainWindow::exportDataForPanels(const QVector<QPair<int, int>>& panels,
                                     const QString& baseDirPath,
                                     int exportFormat,
                                     int exportRange,
                                     double customXMin,
                                     double customXMax,
                                     const QHash<PanelId, QSet<int>>& signalFilter)
{
    if (panels.isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Select at least one panel.");
        return;
    }
    if (baseDirPath.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Choose an output directory.");
        return;
    }
    exportBasePath_ = QDir(baseDirPath.trimmed()).absolutePath();
    preferences_->setExportBasePath(exportBasePath_);
    preferences_->setExportFormat(
        exportFormatSettingValue(
            static_cast<ExportFormat>(exportFormat)));

    LayoutConfig snapshot = config_;
    QSet<QPair<int, int>> selected;
    QHash<PanelId, QRectF> viewRanges;
    const ExportRange rangeMode = static_cast<ExportRange>(exportRange);
    const bool useCurrentView = rangeMode == ExportRange::CurrentView;
    const bool useCustomRange = rangeMode == ExportRange::CustomXRange && std::isfinite(customXMin) && std::isfinite(customXMax);
    if (useCustomRange && customXMin > customXMax) {
        std::swap(customXMin, customXMax);
    }
    for (const auto& panel : panels) {
        selected.insert(panel);
        if (useCurrentView
            && panel.first >= 0 && panel.first < plotWidgets_.size()
            && panel.second >= 0 && panel.second < plotWidgets_[panel.first].size()
            && plotWidgets_[panel.first][panel.second]) {
            viewRanges.insert({panel.first, panel.second},
                              plotWidgets_[panel.first][panel.second]->currentView());
        }
    }
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (!selected.contains({c, r})) {
                snapshot.columns[c][r].signalSpecs.clear();
                continue;
            }
            if (!currentShot.isEmpty()) {
                snapshot.columns[c][r].shot = currentShot;
            }
        }
    }
    snapshot = expandedShotLayout(snapshot);
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            const PanelId filterKey {c, r};
            if (!signalFilter.contains(filterKey)) {
                continue;
            }
            const QSet<int> selectedSignals = signalFilter.value(filterKey);
            QVector<SignalSpec> filteredSignals;
            filteredSignals.reserve(selectedSignals.size());
            for (int i = 0; i < snapshot.columns[c][r].signalSpecs.size(); ++i) {
                if (selectedSignals.contains(i)) {
                    filteredSignals.push_back(snapshot.columns[c][r].signalSpecs[i]);
                }
            }
            snapshot.columns[c][r].signalSpecs = std::move(filteredSignals);
        }
    }

    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        return;
    }

    const DataReadMode readMode = globalRateMode_;
    const ExportFormat format = static_cast<ExportFormat>(exportFormat);
    setStatus(QString("Exporting data from %1 panels...").arg(panels.size()));
    DataExportRequest request;
    request.snapshot = std::move(fetchSnapshot);
    request.baseDirPath = baseDirPath.trimmed();
    request.readMode = readMode;
    request.format = format;
    request.useCurrentView = useCurrentView;
    request.useCustomRange = useCustomRange;
    request.customXMin = customXMin;
    request.customXMax = customXMax;
    request.viewRanges = std::move(viewRanges);

    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start(
        [self, request = std::move(request)] {
        const DataExportResult result = runDataExport(request);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, result] {
            if (!self) {
                return;
            }
            if (!result.errors.isEmpty()) {
                QMessageBox::warning(
                    self,
                    "Export Data",
                    result.errors.join("\n"));
            }
            self->setStatus(
                QString("Exported %1 files to %2")
                    .arg(result.written)
                    .arg(result.outputPath));
        }, Qt::QueuedConnection);
    });
}

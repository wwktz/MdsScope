// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/environment_io.hpp"
#include "core/mds_helpers.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "shared.hpp"
#include "user_preferences.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolButton>

namespace {
void alignLayoutRatesToMinimum(LayoutConfig* config, DataReadMode minimum)
{
    if (!config) {
        return;
    }
    for (QVector<PlotSpec>& column : config->columns) {
        for (PlotSpec& plot : column) {
            for (SignalSpec& sig : plot.signalSpecs) {
                sig.readMode = higherDataReadMode(minimum, sig.readMode);
                // The aligned value is the current in-memory Rate. It is only
                // persisted if the user later invokes Save/Ctrl+S.
                sig.readModeExplicit = true;
            }
        }
    }
}
}

void MainWindow::loadDefaultEnvironment(bool useLatestWhenNoCurrentShot)
{
    const QString defaultTomlConfig = QDir(environmentPath_).filePath("init.toml");
    const QString defaultWebscpConfig = QDir(environmentPath_).filePath("init.webscp");
    if (QFileInfo::exists(defaultTomlConfig)) {
        loadEnvironmentFile(defaultTomlConfig, useLatestWhenNoCurrentShot, false, true);
    } else if (QFileInfo::exists(defaultWebscpConfig)) {
        loadEnvironmentFile(defaultWebscpConfig, useLatestWhenNoCurrentShot, false, true);
    } else {
        loadEnvironmentList(useLatestWhenNoCurrentShot);
    }
}

void MainWindow::loadEnvironmentList(bool useLatestWhenNoCurrentShot)
{
    QDir dir(environmentPath_);
    const auto files = dir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
    if (!files.isEmpty()) {
        loadEnvironmentFile(files.first().absoluteFilePath(), useLatestWhenNoCurrentShot, false, true);
        return;
    }
    setStatus("No environment files found");
}

void MainWindow::clearRecentEnvironmentFiles()
{
    preferences_->clearRecentEnvironmentFiles();
    refreshRecentEnvironmentMenu();
}

void MainWindow::openEnvironmentFile()
{
    const QString allFilter = "All MdsScope Config (*.toml *.webscp)";
    const QString tomlFilter = "MdsScope TOML (*.toml)";
    const QString webscpFilter = "Legacy WebScope Config (*.webscp)";
    const QString filters = allFilter + ";;" + tomlFilter + ";;" + webscpFilter + ";;All Files (*)";
    QString selectedFilter = preferences_->openFileFilter(allFilter);
    const QString path =
        QFileDialog::getOpenFileName(
            this,
            "Open MdsScope Config",
            preferences_->rememberedFileDialogDir(environmentPath_),
            filters,
            &selectedFilter);
    if (!path.isEmpty()) {
        preferences_->setOpenFileFilter(selectedFilter);
        preferences_->rememberFileDialogDir(path);
        loadEnvironmentFile(path);
    }
}

void MainWindow::openRecentEnvironmentFile(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "Open MdsScope Config", "Recent file no longer exists:\n" + path);
        refreshRecentEnvironmentMenu();
        return;
    }
    preferences_->rememberFileDialogDir(path);
    loadEnvironmentFile(path);
}

void MainWindow::refreshRecentEnvironmentMenu()
{
    if (!recentEnvironmentMenu_) {
        return;
    }
    recentEnvironmentMenu_->clear();
    const QStringList files = preferences_->recentEnvironmentFiles();
    if (files.isEmpty()) {
        QAction* empty = recentEnvironmentMenu_->addAction("No Recent Files");
        empty->setEnabled(false);
        return;
    }

    const QFontMetrics fm(recentEnvironmentMenu_->font());
    int menuTextWidth = fm.horizontalAdvance("Clear Recent Files");
    for (const QString& path : files) {
        menuTextWidth = std::max(menuTextWidth, fm.horizontalAdvance(QFileInfo(path).fileName()));
    }
    const int menuWidth = std::clamp(menuTextWidth + 56, 220, 520);
    recentEnvironmentMenu_->setMinimumWidth(menuWidth);
    for (const QString& path : files) {
        const QFileInfo info(path);
        QAction* action = recentEnvironmentMenu_->addAction(fm.elidedText(info.fileName(), Qt::ElideMiddle, menuWidth - 36));
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openRecentEnvironmentFile(path); });
    }
    recentEnvironmentMenu_->addSeparator();
    QAction* clearAction = recentEnvironmentMenu_->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearRecentEnvironmentFiles);
}

void MainWindow::showRecentEnvironmentMenu()
{
    if (!openButton_ || !recentEnvironmentMenu_) {
        return;
    }
    refreshRecentEnvironmentMenu();
    recentEnvironmentMenu_->popup(openButton_->mapToGlobal(QPoint(0, openButton_->height())));
}

void MainWindow::refreshShotHistory()
{
    if (!shotCombo_ || !shotEdit_) {
        return;
    }
    const QString current = shotEdit_->text();
    QSignalBlocker comboBlocker(shotCombo_);
    QSignalBlocker editBlocker(shotEdit_);
    shotCombo_->clear();
    const QFontMetrics fm(shotCombo_->font());
    for (const QString& shot :
         preferences_->recentShotExpressions()) {
        shotCombo_->addItem(fm.elidedText(shot, Qt::ElideMiddle, 300), shot);
        shotCombo_->setItemData(shotCombo_->count() - 1, shot, Qt::ToolTipRole);
    }
    shotCombo_->setEditText(current);
}

bool MainWindow::loadEnvironmentFile(const QString& path,
                                     bool useLatestWhenNoCurrentShot,
                                     bool rememberRecent,
                                     bool prewarmBeforeRefresh)
{
    const QString previousShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const bool shouldFetchLatest = useLatestWhenNoCurrentShot && previousShot.isEmpty();
    QString parseError;
    LayoutConfig loadedConfig = parseEnvironment(path, &parseError);
    if (!parseError.isEmpty()) {
        setStatus(QStringLiteral("Cannot load %1").arg(QFileInfo(path).fileName()));
        QMessageBox::warning(this,
                             QStringLiteral("Open MdsScope Config"),
                             QStringLiteral("Cannot load configuration:\n%1").arg(parseError));
        return false;
    }

    refresh_->cancelDeferredInitialRefresh();
    clearDataPause();
    cancelDataFetch();
    cancelPanelFetch();
    // Do NOT cancel prewarm connections here: if the user manually loads a
    // configuration right after init (the 3-5s window while they decide what to
    // load), prewarm may still be running in the background preparing the
    // connections this new configuration will need. Let it finish instead of
    // discarding that work, so the first fetch reuses the warm sockets.
    refresh_->resetForEnvironmentLoad();
    config_ = std::move(loadedConfig);
    globalRateMode_ = defaultRateMode_;
    // Apply the startup default as a non-persistent minimum. Higher TOML
    // panel/source rates remain unchanged; lower or omitted rates are raised
    // only in memory until the user explicitly saves.
    alignLayoutRatesToMinimum(&config_, globalRateMode_);
    if (!shouldFetchLatest) {
        updateShotControlsFromConfig(previousShot);
    }
    selectedColumn_ = -1;
    selectedRow_ = -1;
    rebuildGrid();
    if (shouldFetchLatest) {
        setLabelTextIfChanged(topInfoLabel_, QStringLiteral("Shot: --"));
        setLabelTextIfChanged(ipInfoLabel_, QStringLiteral("Ip: --"));
        setLabelTextIfChanged(pulseInfoLabel_, QStringLiteral("Pulse: --"));
        setLabelTextIfChanged(itInfoLabel_, QStringLiteral("It: --"));
        setLabelTextIfChanged(timeInfoLabel_, QStringLiteral("Time: --"));
    } else {
        updateTopInfoLabels();
    }
    setStatus(QString("Loaded %1").arg(QFileInfo(path).fileName()));
    if (rememberRecent) {
        preferences_->rememberRecentEnvironmentFile(path);
    }
    if (prewarmBeforeRefresh && !shouldFetchLatest) {
        refresh_->deferInitialRefresh();
        setStatus(QString("Preparing MDS connections for %1...").arg(QFileInfo(path).fileName()));
        if (!prewarmConnections()) {
            refresh_->cancelDeferredInitialRefresh();
            refreshData();
        }
    } else if (!shouldFetchLatest) {
        refreshData();
    } else if (prewarmBeforeRefresh) {
        refresh_->deferInitialRefresh();
    }
    if (shouldFetchLatest) {
        fetchLatestShotAsync();
    }
    QTimer::singleShot(0, this, [this] {
        if (shotEdit_) {
            shotEdit_->clearFocus();
        }
        if (gridHost_) {
            gridHost_->setFocus(Qt::OtherFocusReason);
        }
    });
    return true;
}

void MainWindow::saveCurrentEnvironment()
{
    if (config_.filePath.isEmpty()) {
        saveCurrentEnvironmentAs();
        return;
    }
    if (saveEnvironmentFile(config_.filePath)) {
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

void MainWindow::saveCurrentEnvironmentAs()
{
    const QString path =
        QFileDialog::getSaveFileName(
            this,
            "Save MdsScope Config",
            preferences_->rememberedFileDialogDir(environmentPath_),
            "MdsScope Config (*.toml)");
    if (path.isEmpty()) {
        return;
    }
    if (saveEnvironmentFile(path)) {
        const QFileInfo info(path);
        config_.filePath = info.suffix().isEmpty() ? path + ".toml" : path;
        preferences_->rememberFileDialogDir(config_.filePath);
        loadEnvironmentFile(config_.filePath);
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

bool MainWindow::saveEnvironmentFile(const QString& path)
{
    // Rates were resolved when the configuration opened and may since have
    // been freely changed by the user. Save those exact current values.
    const EnvironmentSaveResult result =
        saveEnvironmentBundle(config_,
                              path,
                              QDir(rootPath_).filePath("data"));

    if (!result.tomlSaved && !result.webscpSaved) {
        QMessageBox::warning(nullptr,
                             "Save",
                             "Cannot write TOML: " + result.tomlError
                                 + "\nCannot write webscp: "
                                 + result.webscpError);
        return false;
    }
    if (!result.tomlSaved) {
        QMessageBox::warning(
            nullptr,
            "Save",
            "Saved webscp, but TOML export failed: " + result.tomlError);
    }
    if (!result.webscpSaved) {
        QMessageBox::warning(nullptr,
                             "Save",
                             "Saved TOML, but webscp export failed: "
                                 + result.webscpError);
    }
    return result.complete();
}

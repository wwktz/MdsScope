// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "ui/plot/plot_widget.hpp"
#include "shared.hpp"
#include "mds_client.hpp"
#include "ssh_tunnel_manager.hpp"
#include "ui/plot/helpers.hpp"

namespace {
QString signalKey(int column, int row, int signal)
{
    return QString::number(column) + ',' + QString::number(row) + ',' + QString::number(signal);
}

QString readModeKey(DataReadMode readMode)
{
    switch (readMode) {
    case DataReadMode::Thin:
        return QStringLiteral("thin");
    case DataReadMode::Medium:
        return QStringLiteral("medium");
    case DataReadMode::Full:
        return QStringLiteral("full");
    }
    return QStringLiteral("thin");
}

QString layoutReadModeKey(const LayoutConfig& config, DataReadMode globalMode)
{
    bool found = false;
    DataReadMode commonMode = DataReadMode::Thin;
    for (const QVector<PlotSpec>& column : config.columns) {
        for (const PlotSpec& plot : column) {
            for (const SignalSpec& sig : plot.signalSpecs) {
                if (sig.hidden) {
                    continue;
                }
                const DataReadMode effectiveMode =
                    effectiveSignalReadMode(globalMode, sig);
                if (!found) {
                    commonMode = effectiveMode;
                    found = true;
                } else if (effectiveMode != commonMode) {
                    return QStringLiteral("mixed");
                }
            }
        }
    }
    return readModeKey(commonMode);
}

bool loadedSignalSourceStillCurrent(const LayoutConfig& fetchSnapshot,
                                    const LayoutConfig& currentConfig,
                                    const LoadedSignal& item)
{
    if (item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= fetchSnapshot.columns.size()
        || item.row >= fetchSnapshot.columns[item.column].size()
        || item.signal >= fetchSnapshot.columns[item.column][item.row].signalSpecs.size()
        || item.column >= currentConfig.columns.size()
        || item.row >= currentConfig.columns[item.column].size()
        || item.signal >= currentConfig.columns[item.column][item.row].signalSpecs.size()) {
        return false;
    }
    return signalDataSourceEqual(
        fetchSnapshot.columns[item.column][item.row].signalSpecs[item.signal],
        currentConfig.columns[item.column][item.row].signalSpecs[item.signal]);
}

}


void MainWindow::refreshData()
{
    refresh_->fullShotDebounceTimer.stop();
    refresh_->pendingPrewarmRefresh = false;
    // A manual load must not block the UI or interrupt the global prewarm.
    // Queue its refresh; the warm-watcher completion starts it on the same
    // persistent worker connections.
    if (refresh_->warmWatcher.isRunning()) {
        clearDataPause();
        syncDisplayConfig();
        refresh_->pendingRefresh = true;
        refresh_->queuedRefreshKey.clear();
        setStatus("Configuration loaded; waiting for global MDS prewarm...");
        return;
    }
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    // Rates were resolved once when the configuration opened. The global value
    // now identifies the last global choice; each signal carries its current
    // freely editable Rate.
    const DataReadMode readMode = globalRateMode_;
    const QString key = refreshKey(readMode);
    if (refresh_->dataOrPanelRunning()) {
        if (key == refresh_->activeRefreshKey || key == refresh_->queuedRefreshKey) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        cancelDataFetch();
        cancelPanelFetch();
        refresh_->activeRefreshKey.clear();
        refresh_->activeFullRateRefreshViews.clear();
        refresh_->clearActivePanel();
        refresh_->invalidateDataFetch();
        refresh_->pendingRefresh = true;
        refresh_->queuedRefreshKey = key;
        refresh_->pendingPanelRefreshes.clear();
        refresh_->clearQueuedLoadedSignals();
        for (auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                plot->clearSeries();
            }
        }
        for (auto it = refresh_->queuedFullRateRefreshViews.cbegin(); it != refresh_->queuedFullRateRefreshViews.cend(); ++it) {
            const int column = it.key().column;
            const int row = it.key().row;
            if (column >= 0 && row >= 0
                && column < plotWidgets_.size()
                && row < plotWidgets_[column].size()) {
                plotWidgets_[column][row]->applyView(it.value());
            }
        }
        refresh_->attemptedSignals.clear();
        refresh_->streamedOk = 0;
        refresh_->streamedFailed = 0;
        setStatus("Data refresh queued until current work stops...");
        return;
    }
    if (sshTunnelManager_ && sshTunnelManager_->preparationInProgress()) {
        // A Rate/shot/config change does not need another SSH tunnel. Keep only
        // the newest data request and run it when the in-flight preparation has
        // made the persistent forwarding process available.
        refresh_->invalidateDataFetch();
        refresh_->pendingRefresh = true;
        refresh_->queuedRefreshKey = key;
        refresh_->sshFullRefreshPending = true;
        setStatus("Data refresh queued until SSH tunnel is ready...");
        return;
    }
    refresh_->pendingRefresh = false;
    refresh_->clearQueuedLoadedSignals();
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->clearSeries();
        }
    }
    for (auto it = refresh_->queuedFullRateRefreshViews.cbegin(); it != refresh_->queuedFullRateRefreshViews.cend(); ++it) {
        const int column = it.key().column;
        const int row = it.key().row;
        if (column >= 0 && row >= 0
            && column < plotWidgets_.size()
            && row < plotWidgets_[column].size()) {
            plotWidgets_[column][row]->applyView(it.value());
        }
    }
    refresh_->attemptedSignals.clear();
    refresh_->streamedOk = 0;
    refresh_->streamedFailed = 0;
    refresh_->activeFullRateRefreshViews = std::move(refresh_->queuedFullRateRefreshViews);
    refresh_->queuedFullRateRefreshViews.clear();
    launchDataFetch(displayConfig_, readMode, key);
}

void MainWindow::launchDataFetch(const LayoutConfig& snapshot, DataReadMode readMode, const QString& key)
{
    const int preparationGeneration = refresh_->activeDataFetchGeneration;
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        refresh_->activeRefreshKey.clear();
        return;
    }
    if (preparationGeneration != refresh_->activeDataFetchGeneration) {
        // A newer Rate/shot/config request arrived while the persistent tunnel
        // was being prepared. Do not start this obsolete data fetch.
        refresh_->activeRefreshKey.clear();
        return;
    }
    const int generation = refresh_->beginDataFetch(snapshot, readMode, key);
    const auto cancel = refresh_->dataCancel;
    setStatus(QString("Fetching MDS data (%1)...")
                  .arg(layoutReadModeKey(snapshot, readMode)));
    auto* watcher = new QFutureWatcher<QVector<LoadedSignal>>(this);
    connect(watcher, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this, watcher, key, generation] {
        refresh_->finishDataFetch();
        if (refresh_->acceptsDataResult(generation, key)) {
            applyLoadedSignals(watcher->result());
        }
        watcher->deleteLater();
        startPendingFetchIfIdle();
    });
    watcher->setFuture(QtConcurrent::run([this, fetchSnapshot, readMode, key, generation, cancel] {
        auto loaded = fetchMdsSignals(fetchSnapshot, readMode, [this, key, generation, cancel](const LoadedSignal& item) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return;
            }
            LoadedSignal prepared = item;
            rebuildMinMaxIndex(prepared.series);
            QMetaObject::invokeMethod(this, [this, key, generation, item = std::move(prepared), cancel]() mutable {
                if ((!cancel || !cancel->load(std::memory_order_relaxed))
                    && refresh_->acceptsDataResult(generation, key)) {
                    queueLoadedSignal(std::move(item));
                }
            }, Qt::QueuedConnection);
        }, cancel, true);
        return loaded;
    }));
}

void MainWindow::onStopOrContinue()
{
    if (refresh_->dataRefreshPaused) {
        resumeDataRefresh();
    } else {
        stopDataRefresh();
    }
}

int MainWindow::countRemainingSignals(const LayoutConfig& snapshot) const
{
    int remaining = 0;
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            const PlotSpec& plot = snapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (plot.signalSpecs[s].hidden) {
                    continue;
                }
                if (!refresh_->attemptedSignals.contains(signalKey(c, r, s))) {
                    ++remaining;
                }
            }
        }
    }
    return remaining;
}

void MainWindow::stopDataRefresh()
{
    const bool wasRunning = refresh_->runningDataFetches > 0;
    cancelDataFetch();
    cancelPanelFetch();
    // Configuration or panel Rate edits can occur while the original global
    // fetch is still winding down. Continue must resume the current sources,
    // not the obsolete snapshot that launched that fetch.
    const LayoutConfig resumeSnapshot = displayConfig_;
    const int remaining = countRemainingSignals(resumeSnapshot);
    const QString activeKey = refresh_->activeRefreshKey;
    const DataReadMode activeMode = refresh_->activeFetchReadMode;
    refresh_->activeRefreshKey.clear();
    refresh_->queuedRefreshKey.clear();
    refresh_->pendingRefresh = false;
    refresh_->pendingResume = false;
    refresh_->clearActivePanel();
    refresh_->pendingPanelRefreshes.clear();
    refresh_->clearQueuedLoadedSignals();
    if (wasRunning && remaining > 0) {
        refresh_->pausedSnapshot = resumeSnapshot;
        refresh_->pausedReadMode = activeMode;
        refresh_->pausedKey = activeKey.isEmpty() ? refreshKey(activeMode) : activeKey;
        setStopButtonPaused(true);
        setStatus(QString("Data refresh paused: %1 signals remaining").arg(remaining));
    } else {
        clearDataPause();
        setStatus(wasRunning ? "Data refresh stopped" : "No data refresh running");
    }
}

void MainWindow::resumeDataRefresh()
{
    if (!refresh_->dataRefreshPaused) {
        return;
    }
    // Fetch only signals that were never attempted; mark the already-attempted
    // ones hidden so fetchMdsSignals skips them while keeping signal indices
    // stable (item.signal is the raw slot index).
    LayoutConfig remainingSnapshot = refresh_->pausedSnapshot;
    for (int c = 0; c < remainingSnapshot.columns.size(); ++c) {
        for (int r = 0; r < remainingSnapshot.columns[c].size(); ++r) {
            PlotSpec& plot = remainingSnapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (refresh_->attemptedSignals.contains(signalKey(c, r, s))) {
                    plot.signalSpecs[s].hidden = true;
                }
            }
        }
    }
    const DataReadMode readMode = refresh_->pausedReadMode;
    const QString key = refresh_->pausedKey;
    clearDataPause();
    if (refresh_->anyWorkerRunning()) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        cancelPanelFetch();
        cancelPrewarmConnections();
        refresh_->clearActivePanel();
        refresh_->pendingPanelRefreshes.clear();
        refresh_->pendingResume = true;
        refresh_->pendingResumeSnapshot = remainingSnapshot;
        refresh_->pausedReadMode = readMode;
        refresh_->pausedKey = key;
        refresh_->activeRefreshKey.clear();
        refresh_->clearQueuedLoadedSignals();
        setStatus("Resuming after current fetch stops...");
        return;
    }
    setStatus("Resuming data fetch...");
    launchDataFetch(remainingSnapshot, readMode, key);
}

void MainWindow::clearDataPause()
{
    refresh_->pendingResume = false;
    if (refresh_->dataRefreshPaused) {
        setStopButtonPaused(false);
    }
}

void MainWindow::setStopButtonPaused(bool paused)
{
    refresh_->dataRefreshPaused = paused;
    if (stopButton_) {
        stopButton_->setText(paused ? "Continue" : "Stop");
    }
}

void MainWindow::cancelDataFetch()
{
    refresh_->cancelData();
}

void MainWindow::cancelPanelFetch()
{
    refresh_->cancelPanel();
}

void MainWindow::cancelPrewarmConnections()
{
    refresh_->cancelPrewarm();
}

void MainWindow::startPendingFetchIfIdle()
{
    if (refresh_->anyWorkerRunning()) {
        return;
    }
    if (refresh_->pendingResume) {
        refresh_->pendingResume = false;
        launchDataFetch(refresh_->pendingResumeSnapshot, refresh_->pausedReadMode, refresh_->pausedKey);
        return;
    }
    if (refresh_->pendingRefresh) {
        refresh_->pendingRefresh = false;
        refresh_->queuedRefreshKey.clear();
        refreshData();
        return;
    }
    if (refresh_->hasPendingPanel()) {
        PanelRefreshRequest request = refresh_->takePendingPanel();
        refreshSignals(request.column,
                       request.row,
                       std::move(request.signalIndices),
                       request.readMode,
                       request.rateRefreshView);
        return;
    }
    maybeStartDeferredRefresh();
}

bool MainWindow::prewarmConnections()
{
    if (config_.columns.isEmpty()) {
        return false;
    }
    if (sshTunnelManager_ && sshTunnelManager_->preparationInProgress()) {
        // A rapid config switch can arrive while the previous config is still
        // establishing its forwarding process. Coalesce it instead of starting
        // or rejecting a second tunnel preparation.
        refresh_->sshPrewarmPending = true;
        setStatus("MDS prewarm queued until SSH tunnel is ready...");
        return true;
    }
    if (refresh_->anyWorkerRunning()) {
        return false;
    }

    const int preparationGeneration = refresh_->activeDataFetchGeneration;
    const LayoutConfig source = expandedShotLayout(config_);
    LayoutConfig snapshot;
    if (!prepareSshLayout(source, &snapshot)) {
        return false;
    }
    if (preparationGeneration != refresh_->activeDataFetchGeneration
        || refresh_->sshFullRefreshPending
        || refresh_->sshPanelRefreshPending
        || refresh_->sshPrewarmPending) {
        // The completion notification will start only the newest queued work.
        return true;
    }
    refresh_->warmCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = refresh_->warmCancel;
    refresh_->warmWatcher.setFuture(QtConcurrent::run([snapshot, cancel] {
        warmMdsConnections(snapshot, cancel);
    }));
    return true;
}

bool MainWindow::canStartDeferredRefresh() const
{
    return refresh_->canStartDeferredRefresh(latestShotFetchRunning_);
}

void MainWindow::maybeStartDeferredRefresh()
{
    if (refresh_->pendingPrewarmRefresh && canStartDeferredRefresh()) {
        refresh_->pendingPrewarmRefresh = false;
        refreshData();
    }
}

QString MainWindow::refreshKey(DataReadMode readMode) const
{
    const QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    return QString("%1|%2|%3")
        .arg(shot)
        .arg(readModeKey(readMode))
        .arg(layoutRefreshSignature(config_));
}

QString MainWindow::panelRefreshKey(int column, int row, const QVector<int>& signalIndices, DataReadMode readMode) const
{
    QString panelSignature;
    if (column >= 0 && row >= 0
        && column < config_.columns.size()
        && row < config_.columns[column].size()) {
        panelSignature = plotRefreshSignature(config_.columns[column][row]);
    }
    QStringList signalParts;
    if (signalIndices.isEmpty()) {
        signalParts.push_back(QStringLiteral("all"));
    } else {
        signalParts.reserve(signalIndices.size());
        for (int signal : signalIndices) {
            signalParts.push_back(QString::number(signal));
        }
    }
    return QString("%1|%2|%3|%4|%5")
        .arg(column)
        .arg(row)
        .arg(signalParts.join(','))
        .arg(readModeKey(readMode))
        .arg(panelSignature);
}

void MainWindow::queuePanelRefresh(PanelRefreshRequest request)
{
    refresh_->queuePanel(std::move(request));
}

void MainWindow::refreshSignals(int column,
                                int row,
                                QVector<int> signalIndices,
                                DataReadMode readMode,
                                const QRectF& rateRefreshView)
{
    if (column < 0 || row < 0 || column >= config_.columns.size() || row >= config_.columns[column].size()) {
        return;
    }
    if (column >= plotWidgets_.size() || row >= plotWidgets_[column].size()) {
        return;
    }

    std::sort(signalIndices.begin(), signalIndices.end());
    signalIndices.erase(std::unique(signalIndices.begin(), signalIndices.end()), signalIndices.end());
    signalIndices.erase(std::remove_if(signalIndices.begin(), signalIndices.end(), [this, column, row](int signal) {
        return signal < 0 || signal >= config_.columns[column][row].signalSpecs.size();
    }), signalIndices.end());

    syncDisplayConfig();
    const QString key = panelRefreshKey(column, row, signalIndices, readMode);
    if (signalIndices.isEmpty()) {
        for (int signal = 0;
             signal < displayConfig_.columns[column][row].signalSpecs.size();
             ++signal) {
            refresh_->attemptedSignals.remove(signalKey(column, row, signal));
        }
    } else {
        for (int signal : std::as_const(signalIndices)) {
            refresh_->attemptedSignals.remove(signalKey(column, row, signal));
        }
    }

    if (refresh_->anyWorkerRunning()) {
        const bool alreadyQueued = refresh_->panelRequestQueued(key);
        if (key == refresh_->activePanelRefreshKey || alreadyQueued) {
            setStatus(QString("Panel refresh already running: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        queuePanelRefresh({column, row, signalIndices, readMode, key, rateRefreshView});
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    }
    if (sshTunnelManager_ && sshTunnelManager_->preparationInProgress()) {
        queuePanelRefresh({column, row, signalIndices, readMode, key, rateRefreshView});
        refresh_->sshPanelRefreshPending = true;
        setStatus(QString("Panel refresh queued until SSH tunnel is ready: col %1 row %2")
                      .arg(column + 1)
                      .arg(row + 1));
        return;
    }

    LayoutConfig snapshot = displayConfig_;
    const bool partialSignalRefresh = !signalIndices.isEmpty();
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (c == column && r == row) {
                if (partialSignalRefresh) {
                    for (int s = 0; s < snapshot.columns[c][r].signalSpecs.size(); ++s) {
                        if (!signalIndices.contains(s)) {
                            snapshot.columns[c][r].signalSpecs[s].hidden = true;
                        }
                    }
                }
                continue;
            }
            snapshot.columns[c][r].signalSpecs.clear();
        }
    }
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        refresh_->clearActivePanel();
        QTimer::singleShot(0, this, [this] { startPendingFetchIfIdle(); });
        return;
    }
    if (refresh_->sshPanelRefreshPending || refresh_->sshFullRefreshPending) {
        // A newer request was queued during tunnel preparation. The completion
        // notification will start it without launching this obsolete panel read.
        refresh_->clearActivePanel();
        return;
    }
    if (!partialSignalRefresh) {
        plotWidgets_[column][row]->clearSeries();
    }
    if (rateRefreshView.isValid() && rateRefreshView.width() > 0.0 && rateRefreshView.height() > 0.0) {
        plotWidgets_[column][row]->applyView(rateRefreshView);
    }
    refresh_->activePanelRefreshKey = key;
    refresh_->activePanelColumn = column;
    refresh_->activePanelRow = row;
    refresh_->activePanelSignals = signalIndices;
    refresh_->activePanelRateRefreshView = rateRefreshView;
    refresh_->panelCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = refresh_->panelCancel;
    const QString rate = layoutReadModeKey(snapshot, readMode);
    setStatus(partialSignalRefresh
                  ? QString("Fetching signal data (%1): col %2 row %3, %4 source(s)")
                        .arg(rate).arg(column + 1).arg(row + 1).arg(signalIndices.size())
                  : QString("Fetching panel data (%1): col %2 row %3")
                        .arg(rate).arg(column + 1).arg(row + 1));
    refresh_->panelWatcher.setFuture(QtConcurrent::run([fetchSnapshot, readMode, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(fetchSnapshot, readMode, {}, cancel, true);
        for (LoadedSignal& item : loaded) {
            rebuildMinMaxIndex(item.series);
        }
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(LoadedSignal item)
{
    refresh_->queuedLoadedSignals.push_back(std::move(item));
    if (refresh_->queuedLoadedSignalApply) {
        return;
    }
    refresh_->queuedLoadedSignalApply = true;
    QTimer::singleShot(16, this, [this] {
        flushQueuedLoadedSignals();
    });
}

void MainWindow::flushQueuedLoadedSignals()
{
    if (refresh_->queuedLoadedSignals.isEmpty()) {
        refresh_->queuedLoadedSignalApply = false;
        return;
    }
    QVector<LoadedSignal> batch;
    batch.swap(refresh_->queuedLoadedSignals);
    refresh_->queuedLoadedSignalApply = false;
    for (LoadedSignal& item : batch) {
        applyLoadedSignal(std::move(item));
    }
}

void MainWindow::applyLoadedSignal(LoadedSignal item)
{
    if (item.column < 0 || item.row < 0 || item.column >= plotWidgets_.size() || item.row >= plotWidgets_[item.column].size()) {
        return;
    }
    if (item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || !loadedSignalMatchesConfig(displayConfig_, item)
        || !loadedSignalSourceStillCurrent(refresh_->activeFetchSnapshot,
                                           displayConfig_,
                                           item)) {
        return;
    }

    // Remember the source signal and capture hasData before the series is moved
    // into setSeries (both read item.series, which move leaves empty).
    const bool hasData = item.series.hasData();
    if (hasData) {
        rememberLoadedSourceSignal(item);
    }
    plotWidgets_[item.column][item.row]->setSeries(item.signal, std::move(item.series));
    // Record every delivered slot (loaded or failed) so a later Continue only
    // re-fetches signals that were never attempted.
    refresh_->attemptedSignals.insert(signalKey(item.column, item.row, item.signal));
    fitRateRefreshPanelIfComplete(item.column, item.row);
    if (!hasData) {
        ++refresh_->streamedFailed;
    } else {
        ++refresh_->streamedOk;
    }
    const int streamedTotal = refresh_->streamedOk + refresh_->streamedFailed;
    if (streamedTotal == 1 || streamedTotal % 8 == 0) {
        setStatus(QString("Data refresh: %1 signals loaded, %2 failed...").arg(refresh_->streamedOk).arg(refresh_->streamedFailed));
    }
}

void MainWindow::applyLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    flushQueuedLoadedSignals();
    if (refresh_->activeRefreshKey.isEmpty()) {
        return;
    }
    refresh_->activeRefreshKey.clear();
    if (refresh_->streamedOk + refresh_->streamedFailed >= loaded.size()) {
        // Normally each panel settles as soon as its last signal streams in.
        // The final result is nevertheless authoritative: a final batch can
        // race the queued streaming callbacks, so settle any views that remain.
        fitRemainingRateRefreshPanels();
        setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(refresh_->streamedOk).arg(refresh_->streamedFailed));
        return;
    }

    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        const QString key = signalKey(item.column, item.row, item.signal);
        if (refresh_->attemptedSignals.contains(key)) {
            continue;
        }
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)
                || !loadedSignalSourceStillCurrent(refresh_->activeFetchSnapshot,
                                                   displayConfig_,
                                                   item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            refresh_->attemptedSignals.insert(key);
            fitRateRefreshPanelIfComplete(item.column, item.row);
            if (!item.series.hasData()) {
                ++failed;
            } else {
                ++ok;
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed")
                  .arg(refresh_->streamedOk + ok)
                  .arg(refresh_->streamedFailed + failed));
    fitRemainingRateRefreshPanels();
}

void MainWindow::fitRateRefreshPanelIfComplete(int column, int row)
{
    auto viewIt = refresh_->activeFullRateRefreshViews.find({column, row});
    if (viewIt == refresh_->activeFullRateRefreshViews.end()
        || column < 0 || row < 0
        || column >= refresh_->activeFetchSnapshot.columns.size()
        || row >= refresh_->activeFetchSnapshot.columns[column].size()
        || column >= plotWidgets_.size()
        || row >= plotWidgets_[column].size()) {
        return;
    }

    const PlotSpec& panel = refresh_->activeFetchSnapshot.columns[column][row];
    for (int signal = 0; signal < panel.signalSpecs.size(); ++signal) {
        if (!panel.signalSpecs[signal].hidden
            && !refresh_->attemptedSignals.contains(signalKey(column, row, signal))) {
            return;
        }
    }

    const QRectF view = viewIt.value();
    refresh_->activeFullRateRefreshViews.erase(viewIt);
    if (view.isValid() && view.width() > 0.0) {
        plotWidgets_[column][row]->applyXRangeAutoY(view.left(), view.right());
    }
}

void MainWindow::fitRemainingRateRefreshPanels()
{
    QHash<PanelId, QRectF> remainingViews;
    remainingViews.swap(refresh_->activeFullRateRefreshViews);
    for (auto it = remainingViews.cbegin(); it != remainingViews.cend(); ++it) {
        const int column = it.key().column;
        const int row = it.key().row;
        if (column < 0 || row < 0
            || column >= plotWidgets_.size()
            || row >= plotWidgets_[column].size()
            || !it.value().isValid()
            || it.value().width() <= 0.0) {
            continue;
        }

        const QVector<SignalSeries> series = plotWidgets_[column][row]->seriesSnapshot();
        const bool hasData = std::any_of(series.cbegin(), series.cend(), [](const SignalSeries& item) {
            return item.hasData();
        });
        if (hasData) {
            plotWidgets_[column][row]->applyXRangeAutoY(it.value().left(), it.value().right());
        }
    }
}

void MainWindow::applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < 0 || item.row < 0
            || item.column >= plotWidgets_.size()
            || item.row >= plotWidgets_[item.column].size()
            || !loadedSignalMatchesConfig(displayConfig_, item)) {
            continue;
        }
        plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
        if (item.series.hasData()) {
            ++ok;
            rememberLoadedSourceSignal(item);
        } else {
            ++failed;
        }
    }
    if (refresh_->activePanelColumn >= 0 && refresh_->activePanelRow >= 0
        && refresh_->activePanelColumn < plotWidgets_.size()
        && refresh_->activePanelRow < plotWidgets_[refresh_->activePanelColumn].size()
        && refresh_->activePanelRateRefreshView.isValid()
        && refresh_->activePanelRateRefreshView.width() > 0.0) {
        plotWidgets_[refresh_->activePanelColumn][refresh_->activePanelRow]->applyXRangeAutoY(
            refresh_->activePanelRateRefreshView.left(), refresh_->activePanelRateRefreshView.right());
    }
    refresh_->activePanelRateRefreshView = {};
    setStatus(QString("Panel refresh done: %1 signals loaded, %2 failed").arg(ok).arg(failed));
}

void MainWindow::rememberLoadedSourceSignal(const LoadedSignal& item)
{
    if (!item.series.hasData()
        || item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || item.signal >= displayConfig_.columns[item.column][item.row].signalSpecs.size()) {
        return;
    }
    const SignalSpec& sig = displayConfig_.columns[item.column][item.row].signalSpecs[item.signal];
    const QString tree = sig.experiment.trimmed().toLower();
    const QStringList nodeNames = sourceIndexSignalNames(sig.yExpr);
    if (tree.isEmpty() || nodeNames.isEmpty()) {
        return;
    }
    QStringList signalKeys;
    signalKeys.reserve(nodeNames.size());
    for (const QString& signal : nodeNames) {
        signalKeys.push_back(signal.trimmed().toLower());
    }
    const QString key = tree + QChar('\n') + signalKeys.join(QChar(0x1f));
    if (rememberedSourceSignals_.contains(key)) {
        return;
    }
    rememberedSourceSignals_.insert(key);
    addSourceIndexSignal(sig.experiment, sig.yExpr);
}

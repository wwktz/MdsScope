// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
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

}


void MainWindow::refreshData()
{
    pendingPrewarmRefresh_ = false;
    // A manual load must not block the UI or interrupt the global prewarm.
    // Queue its refresh; the warm-watcher completion starts it on the same
    // persistent worker connections.
    if (warmWatcher_.isRunning()) {
        clearDataPause();
        syncDisplayConfig();
        pendingRefresh_ = true;
        queuedRefreshKey_.clear();
        setStatus("Configuration loaded; waiting for global MDS prewarm...");
        return;
    }
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    // The startup/global Rate is a non-destructive floor. A higher per-source
    // TOML mode still wins in effectiveSignalReadMode().
    const DataReadMode readMode = globalRateMode_;
    const QString key = refreshKey(readMode);
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning()) {
        if (key == activeRefreshKey_ || key == queuedRefreshKey_) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        cancelDataFetch();
        cancelPanelFetch();
        activeRefreshKey_.clear();
        activePanelRefreshKey_.clear();
        activeFullRateRefreshViews_.clear();
        activePanelRateRefreshView_ = {};
        ++activeDataFetchGeneration_;
        pendingRefresh_ = true;
        queuedRefreshKey_ = key;
        pendingPanelRefreshes_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        for (auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                plot->clearSeries();
            }
        }
        for (auto it = queuedFullRateRefreshViews_.cbegin(); it != queuedFullRateRefreshViews_.cend(); ++it) {
            const QStringList parts = it.key().split(',');
            const int column = parts.value(0).toInt();
            const int row = parts.value(1).toInt();
            if (column < plotWidgets_.size() && row < plotWidgets_[column].size()) {
                plotWidgets_[column][row]->applyView(it.value());
            }
        }
        attemptedSignals_.clear();
        streamedOk_ = 0;
        streamedFailed_ = 0;
        setStatus("Data refresh queued until current work stops...");
        return;
    }
    if (sshTunnelManager_ && sshTunnelManager_->preparationInProgress()) {
        // A Rate/shot/config change does not need another SSH tunnel. Keep only
        // the newest data request and run it when the in-flight preparation has
        // made the persistent forwarding process available.
        ++activeDataFetchGeneration_;
        pendingRefresh_ = true;
        queuedRefreshKey_ = key;
        sshFullRefreshPending_ = true;
        setStatus("Data refresh queued until SSH tunnel is ready...");
        return;
    }
    pendingRefresh_ = false;
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->clearSeries();
        }
    }
    for (auto it = queuedFullRateRefreshViews_.cbegin(); it != queuedFullRateRefreshViews_.cend(); ++it) {
        const QStringList parts = it.key().split(',');
        const int column = parts.value(0).toInt();
        const int row = parts.value(1).toInt();
        if (column < plotWidgets_.size() && row < plotWidgets_[column].size()) {
            plotWidgets_[column][row]->applyView(it.value());
        }
    }
    attemptedSignals_.clear();
    streamedOk_ = 0;
    streamedFailed_ = 0;
    activeFullRateRefreshViews_ = std::move(queuedFullRateRefreshViews_);
    queuedFullRateRefreshViews_.clear();
    launchDataFetch(displayConfig_, readMode, key);
}

void MainWindow::launchDataFetch(const LayoutConfig& snapshot, DataReadMode readMode, const QString& key)
{
    const int preparationGeneration = activeDataFetchGeneration_;
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        activeRefreshKey_.clear();
        return;
    }
    if (preparationGeneration != activeDataFetchGeneration_) {
        // A newer Rate/shot/config request arrived while the persistent tunnel
        // was being prepared. Do not start this obsolete data fetch.
        activeRefreshKey_.clear();
        return;
    }
    activeFetchSnapshot_ = snapshot;
    activeFetchReadMode_ = readMode;
    activeRefreshKey_ = key;
    queuedRefreshKey_.clear();
    const int generation = ++activeDataFetchGeneration_;
    dataCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = dataCancel_;
    setStatus(QString("Fetching MDS data (%1)...")
                  .arg(layoutReadModeKey(snapshot, readMode)));
    auto* watcher = new QFutureWatcher<QVector<LoadedSignal>>(this);
    ++runningDataFetches_;
    connect(watcher, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this, watcher, key, generation] {
        runningDataFetches_ = std::max(0, runningDataFetches_ - 1);
        if (generation == activeDataFetchGeneration_ && key == activeRefreshKey_) {
            applyLoadedSignals(watcher->result());
        }
        watcher->deleteLater();
        startPendingFetchIfIdle();
    });
    watcher->setFuture(QtConcurrent::run([this, fetchSnapshot, readMode, key, cancel] {
        auto loaded = fetchMdsSignals(fetchSnapshot, readMode, [this, key, cancel](const LoadedSignal& item) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return;
            }
            LoadedSignal prepared = item;
            rebuildMinMaxIndex(prepared.series);
            QMetaObject::invokeMethod(this, [this, key, item = std::move(prepared), cancel]() mutable {
                if ((!cancel || !cancel->load(std::memory_order_relaxed)) && key == activeRefreshKey_) {
                    queueLoadedSignal(std::move(item));
                }
            }, Qt::QueuedConnection);
        }, cancel, true);
        return loaded;
    }));
}

void MainWindow::onStopOrContinue()
{
    if (dataRefreshPaused_) {
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
                if (!attemptedSignals_.contains(signalKey(c, r, s))) {
                    ++remaining;
                }
            }
        }
    }
    return remaining;
}

void MainWindow::stopDataRefresh()
{
    const bool wasRunning = runningDataFetches_ > 0;
    cancelDataFetch();
    cancelPanelFetch();
    const int remaining = countRemainingSignals(activeFetchSnapshot_);
    const QString activeKey = activeRefreshKey_;
    const DataReadMode activeMode = activeFetchReadMode_;
    activeRefreshKey_.clear();
    queuedRefreshKey_.clear();
    pendingRefresh_ = false;
    pendingResume_ = false;
    activePanelRefreshKey_.clear();
    activePanelColumn_ = -1;
    activePanelRow_ = -1;
    activePanelSignals_.clear();
    activePanelRateRefreshView_ = {};
    pendingPanelRefreshes_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    if (wasRunning && remaining > 0) {
        pausedSnapshot_ = activeFetchSnapshot_;
        pausedReadMode_ = activeMode;
        pausedKey_ = activeKey;
        setStopButtonPaused(true);
        setStatus(QString("Data refresh paused: %1 signals remaining").arg(remaining));
    } else {
        clearDataPause();
        setStatus(wasRunning ? "Data refresh stopped" : "No data refresh running");
    }
}

void MainWindow::resumeDataRefresh()
{
    if (!dataRefreshPaused_) {
        return;
    }
    // Fetch only signals that were never attempted; mark the already-attempted
    // ones hidden so fetchMdsSignals skips them while keeping signal indices
    // stable (item.signal is the raw slot index).
    LayoutConfig remainingSnapshot = pausedSnapshot_;
    for (int c = 0; c < remainingSnapshot.columns.size(); ++c) {
        for (int r = 0; r < remainingSnapshot.columns[c].size(); ++r) {
            PlotSpec& plot = remainingSnapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (attemptedSignals_.contains(signalKey(c, r, s))) {
                    plot.signalSpecs[s].hidden = true;
                }
            }
        }
    }
    const DataReadMode readMode = pausedReadMode_;
    const QString key = pausedKey_;
    clearDataPause();
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        cancelPanelFetch();
        cancelPrewarmConnections();
        activePanelRefreshKey_.clear();
        activePanelColumn_ = -1;
        activePanelRow_ = -1;
        activePanelSignals_.clear();
        activePanelRateRefreshView_ = {};
        pendingPanelRefreshes_.clear();
        pendingResume_ = true;
        pendingResumeSnapshot_ = remainingSnapshot;
        pausedReadMode_ = readMode;
        pausedKey_ = key;
        activeRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        setStatus("Resuming after current fetch stops...");
        return;
    }
    setStatus("Resuming data fetch...");
    launchDataFetch(remainingSnapshot, readMode, key);
}

void MainWindow::clearDataPause()
{
    pendingResume_ = false;
    if (dataRefreshPaused_) {
        setStopButtonPaused(false);
    }
}

void MainWindow::setStopButtonPaused(bool paused)
{
    dataRefreshPaused_ = paused;
    if (stopButton_) {
        stopButton_->setText(paused ? "Continue" : "Stop");
    }
}

void MainWindow::cancelDataFetch()
{
    if (dataCancel_) {
        dataCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPanelFetch()
{
    if (panelCancel_) {
        panelCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPrewarmConnections()
{
    if (warmCancel_) {
        warmCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::startPendingFetchIfIdle()
{
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        return;
    }
    if (pendingResume_) {
        pendingResume_ = false;
        launchDataFetch(pendingResumeSnapshot_, pausedReadMode_, pausedKey_);
        return;
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        queuedRefreshKey_.clear();
        refreshData();
        return;
    }
    if (!pendingPanelRefreshes_.isEmpty()) {
        PanelRefreshRequest request = pendingPanelRefreshes_.takeFirst();
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
        sshPrewarmPending_ = true;
        setStatus("MDS prewarm queued until SSH tunnel is ready...");
        return true;
    }
    if (warmWatcher_.isRunning() || runningDataFetches_ > 0 || panelWatcher_.isRunning()) {
        return false;
    }

    const int preparationGeneration = activeDataFetchGeneration_;
    const LayoutConfig source = expandedShotLayout(config_);
    LayoutConfig snapshot;
    if (!prepareSshLayout(source, &snapshot)) {
        return false;
    }
    if (preparationGeneration != activeDataFetchGeneration_
        || sshFullRefreshPending_
        || sshPanelRefreshPending_
        || sshPrewarmPending_) {
        // The completion notification will start only the newest queued work.
        return true;
    }
    warmCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = warmCancel_;
    warmWatcher_.setFuture(QtConcurrent::run([snapshot, cancel] {
        warmMdsConnections(snapshot, cancel);
    }));
    return true;
}

bool MainWindow::canStartDeferredRefresh() const
{
    return runningDataFetches_ <= 0
        && activeRefreshKey_.isEmpty()
        && !latestShotFetchRunning_
        && !panelWatcher_.isRunning()
        && activePanelRefreshKey_.isEmpty()
        && pendingPanelRefreshes_.isEmpty();
}

void MainWindow::maybeStartDeferredRefresh()
{
    if (pendingPrewarmRefresh_ && canStartDeferredRefresh()) {
        pendingPrewarmRefresh_ = false;
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
    const auto overlaps = [](const QVector<int>& lhs, const QVector<int>& rhs) {
        if (lhs.isEmpty() || rhs.isEmpty()) {
            return true;
        }
        for (int signal : lhs) {
            if (rhs.contains(signal)) {
                return true;
            }
        }
        return false;
    };

    // Only an obsolete refresh of the same panel/source may be cancelled.
    // Unrelated global and panel work is allowed to finish normally.
    if (panelWatcher_.isRunning()
        && request.column == activePanelColumn_
        && request.row == activePanelRow_
        && overlaps(request.signalIndices, activePanelSignals_)) {
        cancelPanelFetch();
        activePanelRefreshKey_.clear();
    }

    if (request.signalIndices.isEmpty()) {
        for (int i = pendingPanelRefreshes_.size() - 1; i >= 0; --i) {
            if (pendingPanelRefreshes_[i].column == request.column
                && pendingPanelRefreshes_[i].row == request.row) {
                pendingPanelRefreshes_.removeAt(i);
            }
        }
    } else {
        for (PanelRefreshRequest& pending : pendingPanelRefreshes_) {
            if (pending.column == request.column && pending.row == request.row
                && pending.signalIndices.isEmpty()) {
                // A whole-panel fetch takes its snapshot when it starts, so it
                // already includes the latest source configuration.
                pending.readMode = request.readMode;
                pending.key = request.key;
                if (request.rateRefreshView.isValid()) {
                    pending.rateRefreshView = request.rateRefreshView;
                }
                return;
            }
        }
        for (int i = pendingPanelRefreshes_.size() - 1; i >= 0; --i) {
            const PanelRefreshRequest& pending = pendingPanelRefreshes_[i];
            if (pending.column == request.column && pending.row == request.row
                && pending.signalIndices == request.signalIndices) {
                pendingPanelRefreshes_.removeAt(i);
            }
        }
    }
    pendingPanelRefreshes_.push_back(std::move(request));
}

void MainWindow::refreshOne(int column, int row, int signal)
{
    refreshOne(column, row, signal, globalRateMode_);
}

void MainWindow::refreshOne(int column, int row, int signal, DataReadMode readMode)
{
    refreshSignals(column, row, signal >= 0 ? QVector<int>{signal} : QVector<int>{}, readMode);
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

    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        const bool alreadyQueued = std::any_of(pendingPanelRefreshes_.cbegin(), pendingPanelRefreshes_.cend(),
                                                [&key](const PanelRefreshRequest& request) {
                                                    return request.key == key;
                                                });
        if (key == activePanelRefreshKey_ || alreadyQueued) {
            setStatus(QString("Panel refresh already running: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        queuePanelRefresh({column, row, signalIndices, readMode, key, rateRefreshView});
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    }
    if (sshTunnelManager_ && sshTunnelManager_->preparationInProgress()) {
        queuePanelRefresh({column, row, signalIndices, readMode, key, rateRefreshView});
        sshPanelRefreshPending_ = true;
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
        activePanelRefreshKey_.clear();
        activePanelColumn_ = -1;
        activePanelRow_ = -1;
        activePanelSignals_.clear();
        QTimer::singleShot(0, this, [this] { startPendingFetchIfIdle(); });
        return;
    }
    if (sshPanelRefreshPending_ || sshFullRefreshPending_) {
        // A newer request was queued during tunnel preparation. The completion
        // notification will start it without launching this obsolete panel read.
        activePanelRefreshKey_.clear();
        activePanelColumn_ = -1;
        activePanelRow_ = -1;
        activePanelSignals_.clear();
        return;
    }
    if (!partialSignalRefresh) {
        plotWidgets_[column][row]->clearSeries();
    }
    if (rateRefreshView.isValid() && rateRefreshView.width() > 0.0 && rateRefreshView.height() > 0.0) {
        plotWidgets_[column][row]->applyView(rateRefreshView);
    }
    activePanelRefreshKey_ = key;
    activePanelColumn_ = column;
    activePanelRow_ = row;
    activePanelSignals_ = signalIndices;
    activePanelRateRefreshView_ = rateRefreshView;
    panelCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = panelCancel_;
    const QString rate = layoutReadModeKey(snapshot, readMode);
    setStatus(partialSignalRefresh
                  ? QString("Fetching signal data (%1): col %2 row %3, %4 source(s)")
                        .arg(rate).arg(column + 1).arg(row + 1).arg(signalIndices.size())
                  : QString("Fetching panel data (%1): col %2 row %3")
                        .arg(rate).arg(column + 1).arg(row + 1));
    panelWatcher_.setFuture(QtConcurrent::run([fetchSnapshot, readMode, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(fetchSnapshot, readMode, {}, cancel, true);
        for (LoadedSignal& item : loaded) {
            rebuildMinMaxIndex(item.series);
        }
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(LoadedSignal item)
{
    queuedLoadedSignals_.push_back(std::move(item));
    if (queuedLoadedSignalApply_) {
        return;
    }
    queuedLoadedSignalApply_ = true;
    QTimer::singleShot(16, this, [this] {
        flushQueuedLoadedSignals();
    });
}

void MainWindow::flushQueuedLoadedSignals()
{
    if (queuedLoadedSignals_.isEmpty()) {
        queuedLoadedSignalApply_ = false;
        return;
    }
    QVector<LoadedSignal> batch;
    batch.swap(queuedLoadedSignals_);
    queuedLoadedSignalApply_ = false;
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
        || !loadedSignalMatchesConfig(displayConfig_, item)) {
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
    attemptedSignals_.insert(signalKey(item.column, item.row, item.signal));
    if (!hasData) {
        ++streamedFailed_;
    } else {
        ++streamedOk_;
    }
    const int streamedTotal = streamedOk_ + streamedFailed_;
    if (streamedTotal == 1 || streamedTotal % 8 == 0) {
        setStatus(QString("Data refresh: %1 signals loaded, %2 failed...").arg(streamedOk_).arg(streamedFailed_));
    }
}

void MainWindow::applyLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    flushQueuedLoadedSignals();
    if (activeRefreshKey_.isEmpty()) {
        return;
    }
    activeRefreshKey_.clear();
    const auto fitRateViews = [this] {
        for (auto it = activeFullRateRefreshViews_.cbegin(); it != activeFullRateRefreshViews_.cend(); ++it) {
            const QStringList parts = it.key().split(',');
            const int column = parts.value(0).toInt();
            const int row = parts.value(1).toInt();
            if (column < plotWidgets_.size() && row < plotWidgets_[column].size()) {
                plotWidgets_[column][row]->applyXRangeAutoY(it.value().left(), it.value().right());
            }
        }
        activeFullRateRefreshViews_.clear();
    };
    if (streamedOk_ + streamedFailed_ >= loaded.size()) {
        fitRateViews();
        setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(streamedOk_).arg(streamedFailed_));
        return;
    }

    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        const QString key = signalKey(item.column, item.row, item.signal);
        if (attemptedSignals_.contains(key)) {
            continue;
        }
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            attemptedSignals_.insert(key);
            if (!item.series.hasData()) {
                ++failed;
            } else {
                ++ok;
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed")
                  .arg(streamedOk_ + ok)
                  .arg(streamedFailed_ + failed));
    fitRateViews();
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
    if (activePanelColumn_ >= 0 && activePanelRow_ >= 0
        && activePanelColumn_ < plotWidgets_.size()
        && activePanelRow_ < plotWidgets_[activePanelColumn_].size()
        && activePanelRateRefreshView_.isValid()
        && activePanelRateRefreshView_.width() > 0.0) {
        plotWidgets_[activePanelColumn_][activePanelRow_]->applyXRangeAutoY(
            activePanelRateRefreshView_.left(), activePanelRateRefreshView_.right());
    }
    activePanelRateRefreshView_ = {};
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

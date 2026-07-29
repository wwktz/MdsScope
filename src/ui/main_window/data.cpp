// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/app_paths.hpp"
#include "core/mds_helpers.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "shot_workflow.hpp"
#include "ui/plot/plot_widget.hpp"
#include "shared.hpp"
#include "mds/mds_client.hpp"
#include "ssh/ssh_tunnel_manager.hpp"
#include "ui/plot/helpers.hpp"

#include <QLineEdit>
#include <QtConcurrent>

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
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    // Rates were resolved once when the configuration opened. The global value
    // now identifies the last global choice; each signal carries its current
    // freely editable Rate.
    const DataReadMode readMode = globalRateMode_;
    const QString key = refreshKey(readMode);
    const bool sshPreparationRunning =
        sshTunnelManager_ && sshTunnelManager_->preparationInProgress();
    switch (refresh_->requestDataRefresh(key, sshPreparationRunning)) {
    case RefreshCoordinator::DataRefreshAction::AlreadyScheduled:
        setStatus("Data refresh already running for current shot");
        return;
    case RefreshCoordinator::DataRefreshAction::WaitForPrewarm:
        setStatus("Configuration loaded; waiting for global MDS prewarm...");
        return;
    case RefreshCoordinator::DataRefreshAction::WaitForWorkers:
        setStatus("Data refresh queued until current work stops...");
        return;
    case RefreshCoordinator::DataRefreshAction::WaitForSsh:
        setStatus("Data refresh queued until SSH tunnel is ready...");
        return;
    case RefreshCoordinator::DataRefreshAction::Start:
        break;
    }

    // Release the previous curves before a Full fetch can allocate their
    // replacements, but freeze the old axes until every signal in that panel
    // settles. This avoids both the temporary 0..1.01 range and retaining two
    // generations of large Full datasets at once.
    for (const auto& column : plotWidgets_) {
        for (PlotWidget* plot : column) {
            if (plot) {
                plot->clearSeriesPreservingView();
            }
        }
    }
    launchDataFetch(displayConfig_, readMode, key);
}

void MainWindow::launchDataFetch(const LayoutConfig& snapshot, DataReadMode readMode, const QString& key)
{
    const int preparationGeneration = refresh_->dataGeneration();
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        refresh_->takeActiveRateViews();
        return;
    }
    if (preparationGeneration != refresh_->dataGeneration()) {
        // A newer Rate/shot/config request arrived while the persistent tunnel
        // was being prepared. Do not start this obsolete data fetch.
        return;
    }
    const RefreshCoordinator::DataFetchHandle handle =
        refresh_->beginDataFetch(snapshot, readMode, key);
    setStatus(QString("Fetching MDS data (%1)...")
                  .arg(layoutReadModeKey(snapshot, readMode)));
    auto* watcher = new QFutureWatcher<QVector<LoadedSignal>>(this);
    connect(watcher, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this, watcher, handle] {
        refresh_->finishDataFetch();
        if (refresh_->completeDataFetch(handle)) {
            applyLoadedSignals(watcher->result());
        }
        watcher->deleteLater();
        startPendingFetchIfIdle();
    });
    watcher->setFuture(QtConcurrent::run([this, fetchSnapshot, readMode, handle] {
        auto loaded = fetchMdsSignals(fetchSnapshot, readMode, [this, handle](const LoadedSignal& item) {
            if (handle.cancel
                && handle.cancel->load(std::memory_order_relaxed)) {
                return;
            }
            LoadedSignal prepared = item;
            rebuildMinMaxIndex(prepared.series);
            QMetaObject::invokeMethod(this, [this, handle, item = std::move(prepared)]() mutable {
                if (refresh_->acceptsDataResult(handle)) {
                    queueLoadedSignal(std::move(item));
                }
            }, Qt::QueuedConnection);
        }, handle.cancel, true);
        return loaded;
    }));
}

void MainWindow::onStopOrContinue()
{
    if (refresh_->dataPaused()) {
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
                if (!refresh_->signalAttempted(signalKey(c, r, s))) {
                    ++remaining;
                }
            }
        }
    }
    return remaining;
}

void MainWindow::stopDataRefresh()
{
    const bool wasRunning = refresh_->dataWorkerRunning();
    const QString activeKey = refresh_->activeDataKey();
    const DataReadMode activeMode = refresh_->activeDataReadMode();
    cancelDataFetch();
    cancelPanelFetch();
    refresh_->suppressDataResults();
    refresh_->takePendingDataRefresh();
    refresh_->suppressPanelResults();
    refresh_->clearPendingPanels();
    refresh_->clearQueuedLoadedSignals();
    // Configuration or panel Rate edits can occur while the original global
    // fetch is still winding down. Continue must resume the current sources,
    // not the obsolete snapshot that launched that fetch.
    const LayoutConfig resumeSnapshot = displayConfig_;
    const int remaining = countRemainingSignals(resumeSnapshot);
    if (wasRunning && remaining > 0) {
        refresh_->pauseData(
            {resumeSnapshot,
             activeMode,
             activeKey.isEmpty() ? refreshKey(activeMode) : activeKey});
        setStopButtonPaused(true);
        setStatus(QString("Data refresh paused: %1 signals remaining").arg(remaining));
    } else {
        clearDataPause();
        setStatus(wasRunning ? "Data refresh stopped" : "No data refresh running");
    }
}

void MainWindow::resumeDataRefresh()
{
    const std::optional<RefreshCoordinator::PausedData> paused =
        refresh_->pausedData();
    if (!paused) {
        return;
    }
    // Fetch only signals that were never attempted; mark the already-attempted
    // ones hidden so fetchMdsSignals skips them while keeping signal indices
    // stable (item.signal is the raw slot index).
    LayoutConfig remainingSnapshot = paused->snapshot;
    for (int c = 0; c < remainingSnapshot.columns.size(); ++c) {
        for (int r = 0; r < remainingSnapshot.columns[c].size(); ++r) {
            PlotSpec& plot = remainingSnapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (refresh_->signalAttempted(signalKey(c, r, s))) {
                    plot.signalSpecs[s].hidden = true;
                }
            }
        }
    }
    const DataReadMode readMode = paused->readMode;
    const QString key = paused->key;
    clearDataPause();
    if (refresh_->anyWorkerRunning()) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        cancelPanelFetch();
        cancelPrewarmConnections();
        refresh_->suppressPanelResults();
        refresh_->clearPendingPanels();
        refresh_->queueResume({remainingSnapshot, readMode, key});
        refresh_->suppressDataResults();
        refresh_->clearQueuedLoadedSignals();
        setStatus("Resuming after current fetch stops...");
        return;
    }
    setStatus("Resuming data fetch...");
    launchDataFetch(remainingSnapshot, readMode, key);
}

void MainWindow::clearDataPause()
{
    const bool wasPaused = refresh_->dataPaused();
    refresh_->clearPausedData();
    if (wasPaused) {
        setStopButtonPaused(false);
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
    if (const std::optional<RefreshCoordinator::PausedData> resume =
            refresh_->takePendingResume()) {
        launchDataFetch(resume->snapshot, resume->readMode, resume->key);
        return;
    }
    if (refresh_->takePendingDataRefresh()) {
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
        refresh_->deferForSsh(RefreshCoordinator::SshWork::Prewarm);
        setStatus("MDS prewarm queued until SSH tunnel is ready...");
        return true;
    }
    if (refresh_->anyWorkerRunning()) {
        return false;
    }

    const int preparationGeneration = refresh_->dataGeneration();
    const LayoutConfig source = expandedShotLayout(config_);
    LayoutConfig snapshot;
    if (!prepareSshLayout(source, &snapshot)) {
        return false;
    }
    if (preparationGeneration != refresh_->dataGeneration()
        || refresh_->hasSshPendingWork()) {
        // The completion notification will start only the newest queued work.
        return true;
    }
    const auto cancel = refresh_->beginPrewarm();
    refresh_->warmWatcher().setFuture(QtConcurrent::run([snapshot, cancel] {
        warmMdsConnections(snapshot, cancel);
    }));
    return true;
}

bool MainWindow::canStartDeferredRefresh() const
{
    return refresh_->canStartDeferredRefresh(
        shotWorkflow_->latestFetchRunning());
}

void MainWindow::maybeStartDeferredRefresh()
{
    if (refresh_->initialRefreshDeferred()
        && canStartDeferredRefresh()
        && refresh_->takeDeferredInitialRefresh()) {
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
            refresh_->unmarkSignalAttempted(signalKey(column, row, signal));
        }
    } else {
        for (int signal : std::as_const(signalIndices)) {
            refresh_->unmarkSignalAttempted(signalKey(column, row, signal));
        }
    }

    const PanelRefreshRequest request{
        column, row, signalIndices, readMode, key, rateRefreshView};
    const bool sshPreparationRunning =
        sshTunnelManager_ && sshTunnelManager_->preparationInProgress();
    switch (refresh_->requestPanelRefresh(request, sshPreparationRunning)) {
    case RefreshCoordinator::PanelRefreshAction::AlreadyScheduled:
        setStatus(QString("Panel refresh already running: col %1 row %2")
                      .arg(column + 1)
                      .arg(row + 1));
        return;
    case RefreshCoordinator::PanelRefreshAction::Queued:
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    case RefreshCoordinator::PanelRefreshAction::WaitForSsh:
        setStatus(QString("Panel refresh queued until SSH tunnel is ready: col %1 row %2")
                      .arg(column + 1)
                      .arg(row + 1));
        return;
    case RefreshCoordinator::PanelRefreshAction::Start:
        break;
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
        refresh_->suppressPanelResults();
        QTimer::singleShot(0, this, [this] { startPendingFetchIfIdle(); });
        return;
    }
    if (refresh_->hasSshPendingWork()) {
        // A newer request was queued during tunnel preparation. The completion
        // notification will start it without launching this obsolete panel read.
        refresh_->suppressPanelResults();
        return;
    }
    // A whole-panel refresh releases the old data but keeps its axes while the
    // replacement is in flight. A partial signal refresh retains the untouched
    // series exactly as before.
    if (!partialSignalRefresh) {
        plotWidgets_[column][row]->clearSeriesPreservingView();
    }
    if (rateRefreshView.isValid() && rateRefreshView.width() > 0.0 && rateRefreshView.height() > 0.0) {
        plotWidgets_[column][row]->applyView(rateRefreshView);
    }
    const auto cancel = refresh_->beginPanelRefresh(request);
    const QString rate = layoutReadModeKey(snapshot, readMode);
    setStatus(partialSignalRefresh
                  ? QString("Fetching signal data (%1): col %2 row %3, %4 source(s)")
                        .arg(rate).arg(column + 1).arg(row + 1).arg(signalIndices.size())
                  : QString("Fetching panel data (%1): col %2 row %3")
                        .arg(rate).arg(column + 1).arg(row + 1));
    refresh_->panelWatcher().setFuture(QtConcurrent::run([fetchSnapshot, readMode, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(fetchSnapshot, readMode, {}, cancel, true);
        for (LoadedSignal& item : loaded) {
            rebuildMinMaxIndex(item.series);
        }
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(LoadedSignal item)
{
    if (!refresh_->queueLoadedSignal(std::move(item))) {
        return;
    }
    QTimer::singleShot(16, this, [this] {
        flushQueuedLoadedSignals();
    });
}

void MainWindow::flushQueuedLoadedSignals()
{
    QVector<LoadedSignal> batch = refresh_->takeQueuedLoadedSignals();
    if (batch.isEmpty()) {
        return;
    }
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
        || !loadedSignalSourceStillCurrent(refresh_->activeDataSnapshot(),
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
    refresh_->markSignalAttempted(
        signalKey(item.column, item.row, item.signal));
    settleDataRefreshPanelIfComplete(item.column, item.row);
    const RefreshCoordinator::DataProgress progress =
        refresh_->recordStreamedSignal(hasData);
    if (progress.total() == 1 || progress.total() % 8 == 0) {
        setStatus(QString("Data refresh: %1 signals loaded, %2 failed...")
                      .arg(progress.succeeded)
                      .arg(progress.failed));
    }
}

void MainWindow::applyLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    flushQueuedLoadedSignals();
    RefreshCoordinator::DataProgress progress = refresh_->dataProgress();
    if (progress.total() >= loaded.size()) {
        // Normally each panel settles as soon as its last signal streams in.
        // The final result is nevertheless authoritative: a final batch can
        // race the queued streaming callbacks, so settle any views that remain.
        fitRemainingRateRefreshPanels();
        setStatus(QString("Data refresh done: %1 signals loaded, %2 failed")
                      .arg(progress.succeeded)
                      .arg(progress.failed));
        return;
    }

    for (const LoadedSignal& item : loaded) {
        const QString key = signalKey(item.column, item.row, item.signal);
        if (refresh_->signalAttempted(key)) {
            continue;
        }
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)
                || !loadedSignalSourceStillCurrent(refresh_->activeDataSnapshot(),
                                                   displayConfig_,
                                                   item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            refresh_->markSignalAttempted(key);
            settleDataRefreshPanelIfComplete(item.column, item.row);
            const bool hasData = item.series.hasData();
            progress = refresh_->recordStreamedSignal(hasData);
            if (hasData) {
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed")
                  .arg(progress.succeeded)
                  .arg(progress.failed));
    fitRemainingRateRefreshPanels();
}

void MainWindow::settleDataRefreshPanelIfComplete(int column, int row)
{
    const PanelId panelId{column, row};
    const LayoutConfig& snapshot = refresh_->activeDataSnapshot();
    if (column < 0 || row < 0
        || column >= snapshot.columns.size()
        || row >= snapshot.columns[column].size()
        || column >= plotWidgets_.size()
        || row >= plotWidgets_[column].size()) {
        return;
    }

    const PlotSpec& panel = snapshot.columns[column][row];
    for (int signal = 0; signal < panel.signalSpecs.size(); ++signal) {
        if (!panel.signalSpecs[signal].hidden
            && !refresh_->signalAttempted(signalKey(column, row, signal))) {
            return;
        }
    }

    const std::optional<QRectF> view =
        refresh_->takeActiveRateView(panelId);
    if (view && view->isValid() && view->width() > 0.0) {
        plotWidgets_[column][row]->applyXRangeAutoY(view->left(),
                                                   view->right());
    } else {
        plotWidgets_[column][row]->resetScale();
    }
}

void MainWindow::fitRemainingRateRefreshPanels()
{
    const QHash<PanelId, QRectF> remainingViews =
        refresh_->takeActiveRateViews();
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

void MainWindow::applyPanelLoadedSignals(
    const QVector<LoadedSignal>& loaded,
    const PanelRefreshRequest& request)
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
    if (request.column >= 0 && request.row >= 0
        && request.column < plotWidgets_.size()
        && request.row < plotWidgets_[request.column].size()
        && request.rateRefreshView.isValid()
        && request.rateRefreshView.width() > 0.0) {
        plotWidgets_[request.column][request.row]->applyXRangeAutoY(
            request.rateRefreshView.left(),
            request.rateRefreshView.right());
    } else if (request.signalIndices.isEmpty()
               && request.column >= 0
               && request.row >= 0
               && request.column < plotWidgets_.size()
               && request.row < plotWidgets_[request.column].size()) {
        plotWidgets_[request.column][request.row]->resetScale();
    }
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

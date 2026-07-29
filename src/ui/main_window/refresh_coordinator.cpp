// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "refresh_coordinator.hpp"

#include <algorithm>

QFutureWatcher<QVector<LoadedSignal>>& RefreshCoordinator::panelWatcher()
{
    return panelWatcher_;
}

QFutureWatcher<void>& RefreshCoordinator::warmWatcher()
{
    return warmWatcher_;
}

QTimer& RefreshCoordinator::shotDebounceTimer()
{
    return fullShotDebounceTimer_;
}

RefreshCoordinator::DataRefreshAction
RefreshCoordinator::requestDataRefresh(const QString& key,
                                       bool sshPreparationRunning)
{
    stopFullShotDebounce();
    cancelDeferredInitialRefresh();

    if (warmWatcher_.isRunning()) {
        data_.fullRefreshPending = true;
        data_.queuedKey.clear();
        return DataRefreshAction::WaitForPrewarm;
    }

    if (dataOrPanelRunning()) {
        if (key == data_.activeKey || key == data_.queuedKey) {
            return DataRefreshAction::AlreadyScheduled;
        }
        cancelData();
        cancelPanel();
        suppressDataResults();
        data_.activeRateViews.clear();
        clearActivePanel();
        data_.fullRefreshPending = true;
        data_.queuedKey = key;
        panel_.pending.clear();
        clearQueuedLoadedSignals();
        resetDataProgress();
        return DataRefreshAction::WaitForWorkers;
    }

    if (sshPreparationRunning) {
        invalidateDataFetch();
        data_.fullRefreshPending = true;
        data_.queuedKey = key;
        deferForSsh(SshWork::Global);
        return DataRefreshAction::WaitForSsh;
    }

    data_.fullRefreshPending = false;
    data_.queuedKey.clear();
    clearQueuedLoadedSignals();
    resetDataProgress();
    data_.activeRateViews = std::move(data_.pendingRateViews);
    data_.pendingRateViews.clear();
    return DataRefreshAction::Start;
}

bool RefreshCoordinator::hasPendingDataRefresh() const
{
    return data_.fullRefreshPending;
}

bool RefreshCoordinator::takePendingDataRefresh()
{
    if (!data_.fullRefreshPending) {
        return false;
    }
    data_.fullRefreshPending = false;
    data_.queuedKey.clear();
    return true;
}

RefreshCoordinator::DataFetchHandle
RefreshCoordinator::beginDataFetch(const LayoutConfig& snapshot,
                                   DataReadMode readMode,
                                   const QString& key)
{
    data_.activeSnapshot = snapshot;
    data_.activeReadMode = readMode;
    data_.activeKey = key;
    data_.queuedKey.clear();
    data_.cancel = std::make_shared<std::atomic_bool>(false);
    ++data_.runningWorkers;
    const int generation = ++data_.generation;
    return {generation, key, data_.cancel};
}

bool RefreshCoordinator::acceptsDataResult(
    const DataFetchHandle& handle) const
{
    return handle.generation == data_.generation
           && handle.key == data_.activeKey
           && handle.cancel
           && !handle.cancel->load(std::memory_order_relaxed);
}

bool RefreshCoordinator::completeDataFetch(const DataFetchHandle& handle)
{
    if (!acceptsDataResult(handle)) {
        return false;
    }
    data_.activeKey.clear();
    return true;
}

void RefreshCoordinator::finishDataFetch()
{
    data_.runningWorkers = std::max(0, data_.runningWorkers - 1);
}

void RefreshCoordinator::cancelData()
{
    if (data_.cancel) {
        data_.cancel->store(true, std::memory_order_relaxed);
    }
}

int RefreshCoordinator::invalidateDataFetch()
{
    return ++data_.generation;
}

void RefreshCoordinator::suppressDataResults()
{
    data_.activeKey.clear();
    invalidateDataFetch();
}

bool RefreshCoordinator::dataOrPanelRunning() const
{
    return data_.runningWorkers > 0
           || panelWatcher_.isRunning()
           || panel_.active.has_value();
}

bool RefreshCoordinator::anyWorkerRunning() const
{
    return dataOrPanelRunning() || warmWatcher_.isRunning();
}

bool RefreshCoordinator::dataWorkerRunning() const
{
    return data_.runningWorkers > 0;
}

bool RefreshCoordinator::canStartDeferredRefresh(
    bool latestShotFetchRunning) const
{
    return data_.runningWorkers <= 0
           && data_.activeKey.isEmpty()
           && !latestShotFetchRunning
           && !panelWatcher_.isRunning()
           && !panel_.active
           && panel_.pending.isEmpty();
}

bool RefreshCoordinator::hasScheduledWork() const
{
    return anyWorkerRunning()
           || data_.fullRefreshPending
           || pause_.pendingResume
           || !panel_.pending.isEmpty();
}

int RefreshCoordinator::dataGeneration() const
{
    return data_.generation;
}

QString RefreshCoordinator::activeDataKey() const
{
    return data_.activeKey;
}

DataReadMode RefreshCoordinator::activeDataReadMode() const
{
    return data_.activeReadMode;
}

const LayoutConfig& RefreshCoordinator::activeDataSnapshot() const
{
    return data_.activeSnapshot;
}

void RefreshCoordinator::setPendingRateViews(
    QHash<PanelId, QRectF> views)
{
    data_.pendingRateViews = std::move(views);
}

void RefreshCoordinator::clearPendingRateViews()
{
    data_.pendingRateViews.clear();
}

const QHash<PanelId, QRectF>& RefreshCoordinator::pendingRateViews() const
{
    return data_.pendingRateViews;
}

std::optional<QRectF> RefreshCoordinator::takeActiveRateView(PanelId panel)
{
    const auto it = data_.activeRateViews.find(panel);
    if (it == data_.activeRateViews.end()) {
        return std::nullopt;
    }
    const QRectF view = it.value();
    data_.activeRateViews.erase(it);
    return view;
}

QHash<PanelId, QRectF> RefreshCoordinator::takeActiveRateViews()
{
    QHash<PanelId, QRectF> views;
    views.swap(data_.activeRateViews);
    return views;
}

QRectF RefreshCoordinator::preservedRateView(bool hasExplicitView,
                                             const QRectF& currentView)
{
    if (!hasExplicitView
        || !currentView.isValid()
        || currentView.width() <= 0.0
        || currentView.height() <= 0.0) {
        return {};
    }
    return currentView;
}

void RefreshCoordinator::resetDataProgress()
{
    data_.attemptedSignals.clear();
    data_.progress = {};
}

bool RefreshCoordinator::signalAttempted(const QString& key) const
{
    return data_.attemptedSignals.contains(key);
}

void RefreshCoordinator::markSignalAttempted(QString key)
{
    data_.attemptedSignals.insert(std::move(key));
}

void RefreshCoordinator::unmarkSignalAttempted(const QString& key)
{
    data_.attemptedSignals.remove(key);
}

RefreshCoordinator::DataProgress RefreshCoordinator::dataProgress() const
{
    return data_.progress;
}

RefreshCoordinator::DataProgress
RefreshCoordinator::recordStreamedSignal(bool succeeded)
{
    if (succeeded) {
        ++data_.progress.succeeded;
    } else {
        ++data_.progress.failed;
    }
    return data_.progress;
}

bool RefreshCoordinator::queueLoadedSignal(LoadedSignal item)
{
    const bool scheduleFlush = !data_.loadedSignalFlushScheduled;
    data_.queuedLoadedSignals.push_back(std::move(item));
    data_.loadedSignalFlushScheduled = true;
    return scheduleFlush;
}

QVector<LoadedSignal> RefreshCoordinator::takeQueuedLoadedSignals()
{
    QVector<LoadedSignal> queued;
    queued.swap(data_.queuedLoadedSignals);
    data_.loadedSignalFlushScheduled = false;
    return queued;
}

void RefreshCoordinator::clearQueuedLoadedSignals()
{
    data_.queuedLoadedSignals.clear();
    data_.loadedSignalFlushScheduled = false;
}

bool RefreshCoordinator::dataPaused() const
{
    return pause_.paused.has_value();
}

std::optional<RefreshCoordinator::PausedData>
RefreshCoordinator::pausedData() const
{
    return pause_.paused;
}

void RefreshCoordinator::pauseData(PausedData data)
{
    pause_.pendingResume.reset();
    pause_.paused = std::move(data);
}

void RefreshCoordinator::clearPausedData()
{
    pause_.paused.reset();
    pause_.pendingResume.reset();
}

void RefreshCoordinator::queueResume(PausedData data)
{
    pause_.paused.reset();
    pause_.pendingResume = std::move(data);
}

std::optional<RefreshCoordinator::PausedData>
RefreshCoordinator::takePendingResume()
{
    std::optional<PausedData> pending = std::move(pause_.pendingResume);
    pause_.pendingResume.reset();
    return pending;
}

void RefreshCoordinator::resetForEnvironmentLoad()
{
    invalidateDataFetch();
    resetFullShotDebounce();
    data_.activeKey.clear();
    data_.queuedKey.clear();
    data_.activeSnapshot = {};
    data_.activeReadMode = DataReadMode::Thin;
    data_.fullRefreshPending = false;
    clearActivePanel();
    panel_.pending.clear();
    clearQueuedLoadedSignals();
    data_.pendingRateViews.clear();
    data_.activeRateViews.clear();
    resetDataProgress();
    clearPausedData();
    cancelDeferredInitialRefresh();
    sshPending_ = {};
}

void RefreshCoordinator::discardPreservedRateViews()
{
    data_.pendingRateViews.clear();
    data_.activeRateViews.clear();
    if (panel_.active) {
        panel_.active->rateRefreshView = {};
    }
    for (PanelRefreshRequest& request : panel_.pending) {
        request.rateRefreshView = {};
    }
}

void RefreshCoordinator::clearActivePanel()
{
    panel_.active.reset();
    panel_.cancel.reset();
}

bool RefreshCoordinator::panelRequestQueued(const QString& key) const
{
    return std::any_of(panel_.pending.cbegin(),
                       panel_.pending.cend(),
                       [&key](const PanelRefreshRequest& request) {
        return request.key == key;
    });
}

void RefreshCoordinator::queuePanel(PanelRefreshRequest request)
{
    const auto overlaps = [](const QVector<int>& lhs, const QVector<int>& rhs) {
        if (lhs.isEmpty() || rhs.isEmpty()) {
            return true;
        }
        return std::any_of(lhs.cbegin(), lhs.cend(), [&rhs](int signal) {
            return rhs.contains(signal);
        });
    };

    if (panelWatcher_.isRunning()
        && panel_.active
        && request.column == panel_.active->column
        && request.row == panel_.active->row
        && overlaps(request.signalIndices, panel_.active->signalIndices)) {
        cancelPanel();
        suppressPanelResults();
    }

    if (request.signalIndices.isEmpty()) {
        for (qsizetype i = panel_.pending.size(); i-- > 0;) {
            if (panel_.pending[i].column == request.column
                && panel_.pending[i].row == request.row) {
                panel_.pending.removeAt(i);
            }
        }
    } else {
        for (PanelRefreshRequest& pending : panel_.pending) {
            if (pending.column == request.column
                && pending.row == request.row
                && pending.signalIndices.isEmpty()) {
                pending.readMode = request.readMode;
                pending.key = request.key;
                if (request.rateRefreshView.isValid()) {
                    pending.rateRefreshView = request.rateRefreshView;
                }
                return;
            }
        }
        for (qsizetype i = panel_.pending.size(); i-- > 0;) {
            const PanelRefreshRequest& pending = panel_.pending[i];
            if (pending.column == request.column
                && pending.row == request.row
                && pending.signalIndices == request.signalIndices) {
                panel_.pending.removeAt(i);
            }
        }
    }
    panel_.pending.push_back(std::move(request));
}

RefreshCoordinator::PanelRefreshAction
RefreshCoordinator::requestPanelRefresh(PanelRefreshRequest request,
                                        bool sshPreparationRunning)
{
    if (dataOrPanelRunning() || warmWatcher_.isRunning()) {
        if ((panel_.active && panel_.active->key == request.key)
            || panelRequestQueued(request.key)) {
            return PanelRefreshAction::AlreadyScheduled;
        }
        queuePanel(std::move(request));
        return PanelRefreshAction::Queued;
    }
    if (sshPreparationRunning) {
        queuePanel(std::move(request));
        deferForSsh(SshWork::Panel);
        return PanelRefreshAction::WaitForSsh;
    }
    return PanelRefreshAction::Start;
}

RefreshCoordinator::CancelFlag
RefreshCoordinator::beginPanelRefresh(PanelRefreshRequest request)
{
    panel_.active = std::move(request);
    panel_.cancel = std::make_shared<std::atomic_bool>(false);
    return panel_.cancel;
}

std::optional<PanelRefreshRequest>
RefreshCoordinator::completePanelRefresh()
{
    std::optional<PanelRefreshRequest> completed = std::move(panel_.active);
    clearActivePanel();
    return completed;
}

void RefreshCoordinator::suppressPanelResults()
{
    // Keep the cancellation handle until the watcher drains. A shot change can
    // suppress publication first and deliberately cancel the old transfer only
    // when the debounced replacement starts.
    panel_.active.reset();
}

void RefreshCoordinator::cancelPanel()
{
    if (panel_.cancel) {
        panel_.cancel->store(true, std::memory_order_relaxed);
    }
}

bool RefreshCoordinator::hasPendingPanel() const
{
    return !panel_.pending.isEmpty();
}

PanelRefreshRequest RefreshCoordinator::takePendingPanel()
{
    if (panel_.pending.isEmpty()) {
        return {};
    }
    return panel_.pending.takeFirst();
}

void RefreshCoordinator::clearPendingPanels()
{
    panel_.pending.clear();
}

int RefreshCoordinator::fullShotDebounceDelay(bool workInFlight)
{
    const qint64 sincePreviousMs =
        fullShotCadenceTimer_.isValid() ? fullShotCadenceTimer_.elapsed() : -1;
    fullShotCadenceTimer_.restart();
    if (sincePreviousMs >= 0 && sincePreviousMs <= 450) {
        rapidFullShotChanges_ = std::min(rapidFullShotChanges_ + 1, 4);
    } else {
        rapidFullShotChanges_ = 0;
    }
    const int baseMs = workInFlight ? 220 : 120;
    return std::min(380, baseMs + rapidFullShotChanges_ * 40);
}

void RefreshCoordinator::startFullShotDebounce(int delayMs)
{
    fullShotDebounceTimer_.start(delayMs);
}

void RefreshCoordinator::stopFullShotDebounce()
{
    fullShotDebounceTimer_.stop();
}

void RefreshCoordinator::resetFullShotDebounce()
{
    stopFullShotDebounce();
    rapidFullShotChanges_ = 0;
}

RefreshCoordinator::CancelFlag RefreshCoordinator::beginPrewarm()
{
    prewarm_.cancel = std::make_shared<std::atomic_bool>(false);
    return prewarm_.cancel;
}

void RefreshCoordinator::cancelPrewarm()
{
    if (prewarm_.cancel) {
        prewarm_.cancel->store(true, std::memory_order_relaxed);
    }
}

void RefreshCoordinator::deferInitialRefresh()
{
    prewarm_.initialRefreshDeferred = true;
}

void RefreshCoordinator::cancelDeferredInitialRefresh()
{
    prewarm_.initialRefreshDeferred = false;
}

bool RefreshCoordinator::initialRefreshDeferred() const
{
    return prewarm_.initialRefreshDeferred;
}

bool RefreshCoordinator::takeDeferredInitialRefresh()
{
    if (!prewarm_.initialRefreshDeferred) {
        return false;
    }
    prewarm_.initialRefreshDeferred = false;
    return true;
}

void RefreshCoordinator::deferForSsh(SshWork work)
{
    switch (work) {
    case SshWork::Global:
        sshPending_.global = true;
        break;
    case SshWork::Panel:
        sshPending_.panel = true;
        break;
    case SshWork::Prewarm:
        sshPending_.prewarm = true;
        break;
    }
}

bool RefreshCoordinator::hasSshPendingWork() const
{
    return sshPending_.global
           || sshPending_.panel
           || sshPending_.prewarm;
}

RefreshCoordinator::SshPendingWork RefreshCoordinator::takeSshPendingWork()
{
    const SshPendingWork work = sshPending_;
    sshPending_ = {};
    return work;
}

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "refresh_coordinator.hpp"

#include <algorithm>

void RefreshCoordinator::cancelData()
{
    if (dataCancel) {
        dataCancel->store(true, std::memory_order_relaxed);
    }
}

void RefreshCoordinator::cancelPanel()
{
    if (panelCancel) {
        panelCancel->store(true, std::memory_order_relaxed);
    }
}

void RefreshCoordinator::cancelPrewarm()
{
    if (warmCancel) {
        warmCancel->store(true, std::memory_order_relaxed);
    }
}

int RefreshCoordinator::invalidateDataFetch()
{
    return ++activeDataFetchGeneration;
}

int RefreshCoordinator::beginDataFetch(const LayoutConfig& snapshot,
                                       DataReadMode readMode,
                                       const QString& key)
{
    activeFetchSnapshot = snapshot;
    activeFetchReadMode = readMode;
    activeRefreshKey = key;
    queuedRefreshKey.clear();
    dataCancel = std::make_shared<std::atomic_bool>(false);
    ++runningDataFetches;
    return ++activeDataFetchGeneration;
}

bool RefreshCoordinator::acceptsDataResult(int generation,
                                           const QString& key) const
{
    return generation == activeDataFetchGeneration
           && key == activeRefreshKey;
}

void RefreshCoordinator::finishDataFetch()
{
    runningDataFetches = std::max(0, runningDataFetches - 1);
}

bool RefreshCoordinator::dataOrPanelRunning() const
{
    return runningDataFetches > 0 || panelWatcher.isRunning();
}

bool RefreshCoordinator::anyWorkerRunning() const
{
    return dataOrPanelRunning() || warmWatcher.isRunning();
}

bool RefreshCoordinator::canStartDeferredRefresh(
    bool latestShotFetchRunning) const
{
    return runningDataFetches <= 0
           && activeRefreshKey.isEmpty()
           && !latestShotFetchRunning
           && !panelWatcher.isRunning()
           && activePanelRefreshKey.isEmpty()
           && pendingPanelRefreshes.isEmpty();
}

bool RefreshCoordinator::hasScheduledWork() const
{
    return anyWorkerRunning() || !pendingPanelRefreshes.isEmpty();
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

void RefreshCoordinator::resetForEnvironmentLoad()
{
    invalidateDataFetch();
    resetFullShotDebounce();
    activeRefreshKey.clear();
    clearActivePanel();
    pendingRefresh = false;
    pendingPanelRefreshes.clear();
    queuedRefreshKey.clear();
    clearQueuedLoadedSignals();
    queuedFullRateRefreshViews.clear();
    activeFullRateRefreshViews.clear();
    attemptedSignals.clear();
    streamedOk = 0;
    streamedFailed = 0;
}

void RefreshCoordinator::discardPreservedRateViews()
{
    queuedFullRateRefreshViews.clear();
    activeFullRateRefreshViews.clear();
    activePanelRateRefreshView = {};
    for (PanelRefreshRequest& request : pendingPanelRefreshes) {
        request.rateRefreshView = {};
    }
}

void RefreshCoordinator::clearActivePanel()
{
    activePanelRefreshKey.clear();
    activePanelColumn = -1;
    activePanelRow = -1;
    activePanelSignals.clear();
    activePanelRateRefreshView = {};
}

void RefreshCoordinator::clearQueuedLoadedSignals()
{
    queuedLoadedSignals.clear();
    queuedLoadedSignalApply = false;
}

bool RefreshCoordinator::panelRequestQueued(const QString& key) const
{
    return std::any_of(pendingPanelRefreshes.cbegin(),
                       pendingPanelRefreshes.cend(),
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

    if (panelWatcher.isRunning()
        && request.column == activePanelColumn
        && request.row == activePanelRow
        && overlaps(request.signalIndices, activePanelSignals)) {
        cancelPanel();
        activePanelRefreshKey.clear();
    }

    if (request.signalIndices.isEmpty()) {
        for (int i = pendingPanelRefreshes.size() - 1; i >= 0; --i) {
            if (pendingPanelRefreshes[i].column == request.column
                && pendingPanelRefreshes[i].row == request.row) {
                pendingPanelRefreshes.removeAt(i);
            }
        }
    } else {
        for (PanelRefreshRequest& pending : pendingPanelRefreshes) {
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
        for (int i = pendingPanelRefreshes.size() - 1; i >= 0; --i) {
            const PanelRefreshRequest& pending = pendingPanelRefreshes[i];
            if (pending.column == request.column
                && pending.row == request.row
                && pending.signalIndices == request.signalIndices) {
                pendingPanelRefreshes.removeAt(i);
            }
        }
    }
    pendingPanelRefreshes.push_back(std::move(request));
}

bool RefreshCoordinator::hasPendingPanel() const
{
    return !pendingPanelRefreshes.isEmpty();
}

PanelRefreshRequest RefreshCoordinator::takePendingPanel()
{
    if (pendingPanelRefreshes.isEmpty()) {
        return {};
    }
    return pendingPanelRefreshes.takeFirst();
}

int RefreshCoordinator::fullShotDebounceDelay(bool workInFlight)
{
    const qint64 sincePreviousMs =
        fullShotCadenceTimer.isValid() ? fullShotCadenceTimer.elapsed() : -1;
    fullShotCadenceTimer.restart();
    if (sincePreviousMs >= 0 && sincePreviousMs <= 450) {
        rapidFullShotChanges = std::min(rapidFullShotChanges + 1, 4);
    } else {
        rapidFullShotChanges = 0;
    }
    const int baseMs = workInFlight ? 220 : 120;
    return std::min(380, baseMs + rapidFullShotChanges * 40);
}

void RefreshCoordinator::resetFullShotDebounce()
{
    fullShotDebounceTimer.stop();
    rapidFullShotChanges = 0;
}

RefreshCoordinator::SshPendingWork RefreshCoordinator::takeSshPendingWork()
{
    const SshPendingWork work {
        sshFullRefreshPending,
        sshPanelRefreshPending,
        sshPrewarmPending,
    };
    sshFullRefreshPending = false;
    sshPanelRefreshPending = false;
    sshPrewarmPending = false;
    return work;
}

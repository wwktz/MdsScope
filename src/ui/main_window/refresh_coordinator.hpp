// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <memory>

struct PanelRefreshRequest {
    int column = -1;
    int row = -1;
    QVector<int> signalIndices; // Empty means every signal in the panel.
    DataReadMode readMode = DataReadMode::Thin;
    QString key;
    QRectF rateRefreshView;
};

// Owns refresh lifecycle state and the pure scheduling rules shared by the
// MainWindow entry points. UI updates and MDS execution stay outside this class.
class RefreshCoordinator final {
public:
    struct SshPendingWork {
        bool global = false;
        bool panel = false;
        bool prewarm = false;
    };

    void cancelData();
    void cancelPanel();
    void cancelPrewarm();
    int invalidateDataFetch();
    int beginDataFetch(const LayoutConfig& snapshot,
                       DataReadMode readMode,
                       const QString& key);
    bool acceptsDataResult(int generation, const QString& key) const;
    void finishDataFetch();
    bool dataOrPanelRunning() const;
    bool anyWorkerRunning() const;
    bool canStartDeferredRefresh(bool latestShotFetchRunning) const;
    bool hasScheduledWork() const;
    static QRectF preservedRateView(bool hasExplicitView,
                                    const QRectF& currentView);

    void resetForEnvironmentLoad();
    void clearActivePanel();
    void clearQueuedLoadedSignals();

    bool panelRequestQueued(const QString& key) const;
    void queuePanel(PanelRefreshRequest request);
    bool hasPendingPanel() const;
    PanelRefreshRequest takePendingPanel();

    int fullShotDebounceDelay(bool workInFlight);
    void resetFullShotDebounce();

    SshPendingWork takeSshPendingWork();

    QFutureWatcher<QVector<LoadedSignal>> panelWatcher;
    QFutureWatcher<void> warmWatcher;
    QTimer fullShotDebounceTimer;
    QElapsedTimer fullShotCadenceTimer;

    int rapidFullShotChanges = 0;
    bool pendingRefresh = false;
    bool sshFullRefreshPending = false;
    bool sshPanelRefreshPending = false;
    bool sshPrewarmPending = false;
    QVector<PanelRefreshRequest> pendingPanelRefreshes;
    QString activeRefreshKey;
    QString queuedRefreshKey;
    bool pendingPrewarmRefresh = false;
    int runningDataFetches = 0;
    int activeDataFetchGeneration = 0;
    QString activePanelRefreshKey;
    int activePanelColumn = -1;
    int activePanelRow = -1;
    QVector<int> activePanelSignals;
    QRectF activePanelRateRefreshView;
    QHash<QString, QRectF> queuedFullRateRefreshViews;
    QHash<QString, QRectF> activeFullRateRefreshViews;
    int streamedOk = 0;
    int streamedFailed = 0;
    QVector<LoadedSignal> queuedLoadedSignals;
    bool queuedLoadedSignalApply = false;
    std::shared_ptr<std::atomic_bool> dataCancel;
    std::shared_ptr<std::atomic_bool> panelCancel;
    std::shared_ptr<std::atomic_bool> warmCancel;
    bool dataRefreshPaused = false;
    bool pendingResume = false;
    LayoutConfig activeFetchSnapshot;
    DataReadMode activeFetchReadMode = DataReadMode::Thin;
    LayoutConfig pausedSnapshot;
    LayoutConfig pendingResumeSnapshot;
    DataReadMode pausedReadMode = DataReadMode::Thin;
    QString pausedKey;
    QSet<QString> attemptedSignals;
};

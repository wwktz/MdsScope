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
#include <optional>

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
    using CancelFlag = std::shared_ptr<std::atomic_bool>;

    struct DataFetchHandle {
        int generation = 0;
        QString key;
        CancelFlag cancel;
    };

    struct DataProgress {
        int succeeded = 0;
        int failed = 0;

        int total() const { return succeeded + failed; }
    };

    struct PausedData {
        LayoutConfig snapshot;
        DataReadMode readMode = DataReadMode::Thin;
        QString key;
    };

    struct SshPendingWork {
        bool global = false;
        bool panel = false;
        bool prewarm = false;
    };

    enum class DataRefreshAction {
        Start,
        AlreadyScheduled,
        WaitForPrewarm,
        WaitForWorkers,
        WaitForSsh,
    };

    enum class PanelRefreshAction {
        Start,
        AlreadyScheduled,
        Queued,
        WaitForSsh,
    };

    enum class SshWork {
        Global,
        Panel,
        Prewarm,
    };

    QFutureWatcher<QVector<LoadedSignal>>& panelWatcher();
    QFutureWatcher<void>& warmWatcher();
    QTimer& shotDebounceTimer();

    DataRefreshAction requestDataRefresh(const QString& key,
                                         bool sshPreparationRunning);
    bool hasPendingDataRefresh() const;
    bool takePendingDataRefresh();
    DataFetchHandle beginDataFetch(const LayoutConfig& snapshot,
                                   DataReadMode readMode,
                                   const QString& key);
    bool acceptsDataResult(const DataFetchHandle& handle) const;
    bool completeDataFetch(const DataFetchHandle& handle);
    void finishDataFetch();
    void cancelData();
    int invalidateDataFetch();
    void suppressDataResults();
    bool dataOrPanelRunning() const;
    bool anyWorkerRunning() const;
    bool dataWorkerRunning() const;
    bool canStartDeferredRefresh(bool latestShotFetchRunning) const;
    bool hasScheduledWork() const;
    int dataGeneration() const;
    QString activeDataKey() const;
    DataReadMode activeDataReadMode() const;
    const LayoutConfig& activeDataSnapshot() const;

    void setPendingRateViews(QHash<PanelId, QRectF> views);
    void clearPendingRateViews();
    const QHash<PanelId, QRectF>& pendingRateViews() const;
    std::optional<QRectF> takeActiveRateView(PanelId panel);
    QHash<PanelId, QRectF> takeActiveRateViews();
    static QRectF preservedRateView(bool hasExplicitView,
                                    const QRectF& currentView);

    void resetDataProgress();
    bool signalAttempted(const QString& key) const;
    void markSignalAttempted(QString key);
    void unmarkSignalAttempted(const QString& key);
    DataProgress dataProgress() const;
    DataProgress recordStreamedSignal(bool succeeded);
    bool queueLoadedSignal(LoadedSignal item);
    QVector<LoadedSignal> takeQueuedLoadedSignals();
    void clearQueuedLoadedSignals();

    bool dataPaused() const;
    std::optional<PausedData> pausedData() const;
    void pauseData(PausedData data);
    void clearPausedData();
    void queueResume(PausedData data);
    std::optional<PausedData> takePendingResume();

    void resetForEnvironmentLoad();
    void discardPreservedRateViews();

    PanelRefreshAction requestPanelRefresh(PanelRefreshRequest request,
                                           bool sshPreparationRunning);
    CancelFlag beginPanelRefresh(PanelRefreshRequest request);
    std::optional<PanelRefreshRequest> completePanelRefresh();
    void suppressPanelResults();
    void cancelPanel();
    bool hasPendingPanel() const;
    PanelRefreshRequest takePendingPanel();
    void clearPendingPanels();

    int fullShotDebounceDelay(bool workInFlight);
    void startFullShotDebounce(int delayMs);
    void stopFullShotDebounce();
    void resetFullShotDebounce();

    CancelFlag beginPrewarm();
    void cancelPrewarm();
    void deferInitialRefresh();
    void cancelDeferredInitialRefresh();
    bool initialRefreshDeferred() const;
    bool takeDeferredInitialRefresh();

    void deferForSsh(SshWork work);
    bool hasSshPendingWork() const;
    SshPendingWork takeSshPendingWork();

private:
    struct DataState {
        int generation = 0;
        int runningWorkers = 0;
        QString activeKey;
        QString queuedKey;
        LayoutConfig activeSnapshot;
        DataReadMode activeReadMode = DataReadMode::Thin;
        CancelFlag cancel;
        QSet<QString> attemptedSignals;
        DataProgress progress;
        QVector<LoadedSignal> queuedLoadedSignals;
        bool loadedSignalFlushScheduled = false;
        QHash<PanelId, QRectF> pendingRateViews;
        QHash<PanelId, QRectF> activeRateViews;
        bool fullRefreshPending = false;
    };

    struct PanelState {
        std::optional<PanelRefreshRequest> active;
        QVector<PanelRefreshRequest> pending;
        CancelFlag cancel;
    };

    struct PauseState {
        std::optional<PausedData> paused;
        std::optional<PausedData> pendingResume;
    };

    struct PrewarmState {
        CancelFlag cancel;
        bool initialRefreshDeferred = false;
    };

    void queuePanel(PanelRefreshRequest request);
    bool panelRequestQueued(const QString& key) const;
    void clearActivePanel();

    DataState data_;
    PanelState panel_;
    PauseState pause_;
    PrewarmState prewarm_;
    SshPendingWork sshPending_;

    QFutureWatcher<QVector<LoadedSignal>> panelWatcher_;
    QFutureWatcher<void> warmWatcher_;
    QTimer fullShotDebounceTimer_;
    QElapsedTimer fullShotCadenceTimer_;
    int rapidFullShotChanges_ = 0;
};

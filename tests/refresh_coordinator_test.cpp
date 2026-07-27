// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/main_window/refresh_coordinator.hpp"

#include <QCoreApplication>

namespace {
int require(bool condition, int code)
{
    return condition ? 0 : code;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    RefreshCoordinator refresh;
    LayoutConfig snapshot;

    QHash<PanelId, QRectF> rateViews;
    rateViews.insert(PanelId {0, 1}, QRectF(2.0, -1.0, 3.0, 2.0));
    refresh.setPendingRateViews(rateViews);
    if (const int error = require(
            refresh.requestDataRefresh(QStringLiteral("shot-a"), false)
                    == RefreshCoordinator::DataRefreshAction::Start,
            1)) {
        return error;
    }

    const RefreshCoordinator::DataFetchHandle first =
        refresh.beginDataFetch(snapshot,
                               DataReadMode::Full,
                               QStringLiteral("shot-a"));
    if (const int error = require(first.generation == 1
                                      && first.cancel
                                      && refresh.dataWorkerRunning()
                                      && refresh.activeDataKey()
                                             == QStringLiteral("shot-a")
                                      && refresh.acceptsDataResult(first),
                                  2)) {
        return error;
    }

    const std::optional<QRectF> activeRateView =
        refresh.takeActiveRateView(PanelId {0, 1});
    if (const int error = require(activeRateView
                                      && *activeRateView
                                             == QRectF(2.0, -1.0, 3.0, 2.0)
                                      && !refresh.takeActiveRateView(
                                          PanelId {1, 0}),
                                  3)) {
        return error;
    }

    LoadedSignal queuedSignal;
    queuedSignal.column = 0;
    if (const int error = require(refresh.queueLoadedSignal(queuedSignal)
                                      && !refresh.queueLoadedSignal(
                                          queuedSignal)
                                      && refresh.takeQueuedLoadedSignals().size()
                                             == 2
                                      && refresh
                                             .takeQueuedLoadedSignals()
                                             .isEmpty(),
                                  4)) {
        return error;
    }

    refresh.markSignalAttempted(QStringLiteral("0,0,0"));
    if (const int error = require(
            refresh.signalAttempted(QStringLiteral("0,0,0"))
                && refresh.recordStreamedSignal(true).succeeded == 1
                && refresh.recordStreamedSignal(false).failed == 1
                && refresh.dataProgress().total() == 2,
            5)) {
        return error;
    }
    if (const int error = require(
            refresh.requestDataRefresh(QStringLiteral("shot-a"), false)
                    == RefreshCoordinator::DataRefreshAction::AlreadyScheduled,
            6)) {
        return error;
    }

    QHash<PanelId, QRectF> replacementViews;
    replacementViews.insert(PanelId {1, 0},
                            QRectF(5.0, -2.0, 4.0, 4.0));
    refresh.setPendingRateViews(replacementViews);
    if (const int error = require(
            refresh.requestDataRefresh(QStringLiteral("shot-b"), false)
                    == RefreshCoordinator::DataRefreshAction::WaitForWorkers
                && refresh.hasPendingDataRefresh()
                && first.cancel->load()
                && !refresh.acceptsDataResult(first)
                && refresh.pendingRateViews().contains(PanelId {1, 0})
                && refresh.dataProgress().total() == 0,
            7)) {
        return error;
    }
    refresh.finishDataFetch();
    if (const int error = require(!refresh.dataWorkerRunning()
                                      && refresh.takePendingDataRefresh()
                                      && !refresh.hasPendingDataRefresh(),
                                  8)) {
        return error;
    }

    if (const int error = require(
            refresh.requestDataRefresh(QStringLiteral("shot-b"), false)
                    == RefreshCoordinator::DataRefreshAction::Start,
            9)) {
        return error;
    }
    const RefreshCoordinator::DataFetchHandle second =
        refresh.beginDataFetch(snapshot,
                               DataReadMode::Medium,
                               QStringLiteral("shot-b"));
    if (const int error = require(
            second.generation > first.generation
                && refresh.completeDataFetch(second)
                && refresh.activeDataKey().isEmpty()
                && !refresh.acceptsDataResult(second),
            10)) {
        return error;
    }
    refresh.finishDataFetch();

    refresh.pauseData(
        {snapshot, DataReadMode::Medium, QStringLiteral("paused")});
    const std::optional<RefreshCoordinator::PausedData> paused =
        refresh.pausedData();
    if (const int error = require(paused
                                      && refresh.dataPaused()
                                      && paused->key
                                             == QStringLiteral("paused"),
                                  11)) {
        return error;
    }
    refresh.queueResume(
        {snapshot, DataReadMode::Full, QStringLiteral("resume")});
    const std::optional<RefreshCoordinator::PausedData> resume =
        refresh.takePendingResume();
    if (const int error = require(!refresh.dataPaused()
                                      && resume
                                      && resume->readMode
                                             == DataReadMode::Full
                                      && resume->key
                                             == QStringLiteral("resume")
                                      && !refresh.takePendingResume(),
                                  12)) {
        return error;
    }

    PanelRefreshRequest firstPanel;
    firstPanel.column = 0;
    firstPanel.row = 1;
    firstPanel.signalIndices = {2};
    firstPanel.key = QStringLiteral("first");
    if (const int error = require(
            refresh.requestPanelRefresh(firstPanel, false)
                    == RefreshCoordinator::PanelRefreshAction::Start,
            13)) {
        return error;
    }
    const RefreshCoordinator::CancelFlag panelCancel =
        refresh.beginPanelRefresh(firstPanel);

    PanelRefreshRequest replacement = firstPanel;
    replacement.key = QStringLiteral("replacement");
    if (const int error = require(
            refresh.requestPanelRefresh(replacement, false)
                    == RefreshCoordinator::PanelRefreshAction::Queued
                && refresh.hasPendingPanel(),
            14)) {
        return error;
    }
    if (const int error = require(
            refresh.requestPanelRefresh(firstPanel, false)
                    == RefreshCoordinator::PanelRefreshAction::AlreadyScheduled,
            15)) {
        return error;
    }

    const std::optional<PanelRefreshRequest> completedPanel =
        refresh.completePanelRefresh();
    if (const int error = require(completedPanel
                                      && completedPanel->key
                                             == QStringLiteral("first")
                                      && !panelCancel->load(),
                                  16)) {
        return error;
    }
    const PanelRefreshRequest queuedPanel = refresh.takePendingPanel();
    if (const int error = require(queuedPanel.key
                                          == QStringLiteral("replacement")
                                      && !refresh.hasPendingPanel(),
                                  17)) {
        return error;
    }

    PanelRefreshRequest suppressedPanel;
    suppressedPanel.key = QStringLiteral("suppressed");
    const RefreshCoordinator::CancelFlag suppressedCancel =
        refresh.beginPanelRefresh(suppressedPanel);
    refresh.suppressPanelResults();
    refresh.cancelPanel();
    if (const int error = require(suppressedCancel->load()
                                      && !refresh.completePanelRefresh(),
                                  18)) {
        return error;
    }

    if (const int error = require(
            refresh.requestDataRefresh(
                        QStringLiteral("panel-queue-blocker"), false)
                    == RefreshCoordinator::DataRefreshAction::Start,
            19)) {
        return error;
    }
    const RefreshCoordinator::DataFetchHandle queueBlocker =
        refresh.beginDataFetch(snapshot,
                               DataReadMode::Thin,
                               QStringLiteral("panel-queue-blocker"));
    PanelRefreshRequest queuedSignalRequest;
    queuedSignalRequest.column = 1;
    queuedSignalRequest.row = 2;
    queuedSignalRequest.signalIndices = {3};
    queuedSignalRequest.key = QStringLiteral("old-signal");
    if (const int error = require(
            refresh.requestPanelRefresh(queuedSignalRequest, false)
                    == RefreshCoordinator::PanelRefreshAction::Queued,
            20)) {
        return error;
    }
    queuedSignalRequest.key = QStringLiteral("replacement-signal");
    refresh.requestPanelRefresh(queuedSignalRequest, false);

    PanelRefreshRequest wholePanel = queuedSignalRequest;
    wholePanel.signalIndices.clear();
    wholePanel.key = QStringLiteral("whole-panel");
    refresh.requestPanelRefresh(wholePanel, false);

    PanelRefreshRequest newerSignal = queuedSignalRequest;
    newerSignal.readMode = DataReadMode::Full;
    newerSignal.key = QStringLiteral("newer-signal");
    refresh.requestPanelRefresh(newerSignal, false);
    const PanelRefreshRequest coalesced = refresh.takePendingPanel();
    if (const int error = require(coalesced.signalIndices.isEmpty()
                                      && coalesced.key
                                             == QStringLiteral("newer-signal")
                                      && coalesced.readMode
                                             == DataReadMode::Full
                                      && !refresh.hasPendingPanel(),
                                  21)) {
        return error;
    }
    refresh.cancelData();
    refresh.suppressDataResults();
    refresh.finishDataFetch();
    if (const int error = require(
            !refresh.acceptsDataResult(queueBlocker), 22)) {
        return error;
    }

    if (const int error = require(refresh.fullShotDebounceDelay(false) == 120
                                      && refresh.fullShotDebounceDelay(false)
                                             == 160,
                                  23)) {
        return error;
    }
    refresh.resetFullShotDebounce();

    const QRectF automaticView(0.0, -1.0, 1.0, 2.0);
    if (const int error = require(
            !RefreshCoordinator::preservedRateView(false, automaticView)
                 .isValid()
                && RefreshCoordinator::preservedRateView(true, automaticView)
                       == automaticView
                && !RefreshCoordinator::preservedRateView(
                        true, QRectF()).isValid(),
            24)) {
        return error;
    }

    if (const int error = require(
            refresh.requestDataRefresh(QStringLiteral("ssh-shot"), true)
                    == RefreshCoordinator::DataRefreshAction::WaitForSsh
                && refresh.hasPendingDataRefresh(),
            25)) {
        return error;
    }
    refresh.deferForSsh(RefreshCoordinator::SshWork::Panel);
    const RefreshCoordinator::SshPendingWork work =
        refresh.takeSshPendingWork();
    if (const int error = require(work.global && work.panel && !work.prewarm
                                      && !refresh.hasSshPendingWork()
                                      && refresh.takePendingDataRefresh(),
                                  26)) {
        return error;
    }

    refresh.deferInitialRefresh();
    refresh.setPendingRateViews(rateViews);
    refresh.markSignalAttempted(QStringLiteral("0,0,0"));
    refresh.recordStreamedSignal(true);
    refresh.resetForEnvironmentLoad();
    if (const int error = require(!refresh.hasPendingDataRefresh()
                                      && refresh.activeDataKey().isEmpty()
                                      && refresh.pendingRateViews().isEmpty()
                                      && refresh.dataProgress().total() == 0
                                      && !refresh.signalAttempted(
                                          QStringLiteral("0,0,0"))
                                      && !refresh.initialRefreshDeferred(),
                                  27)) {
        return error;
    }

    PanelRefreshRequest pendingRateRefresh;
    pendingRateRefresh.rateRefreshView =
        QRectF(0.0, -1.0, 1.0, 2.0);
    refresh.requestPanelRefresh(pendingRateRefresh, true);
    refresh.setPendingRateViews(rateViews);
    refresh.discardPreservedRateViews();
    const PanelRefreshRequest pending = refresh.takePendingPanel();
    return require(refresh.pendingRateViews().isEmpty()
                       && !pending.rateRefreshView.isValid(),
                   28);
}

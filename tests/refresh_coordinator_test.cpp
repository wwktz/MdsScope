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
    const int generation =
        refresh.beginDataFetch(snapshot, DataReadMode::Full, QStringLiteral("shot-a"));
    if (const int error = require(generation == 1
                                      && refresh.runningDataFetches == 1
                                      && refresh.acceptsDataResult(generation,
                                                                   QStringLiteral("shot-a")),
                                  1)) {
        return error;
    }
    refresh.cancelData();
    if (const int error = require(refresh.dataCancel && refresh.dataCancel->load(), 2)) {
        return error;
    }
    refresh.invalidateDataFetch();
    if (const int error = require(!refresh.acceptsDataResult(generation,
                                                             QStringLiteral("shot-a")),
                                  3)) {
        return error;
    }
    refresh.finishDataFetch();
    if (const int error = require(refresh.runningDataFetches == 0, 4)) {
        return error;
    }

    PanelRefreshRequest first;
    first.column = 0;
    first.row = 1;
    first.signalIndices = {2};
    first.key = QStringLiteral("first");
    refresh.queuePanel(first);

    PanelRefreshRequest replacement = first;
    replacement.key = QStringLiteral("replacement");
    refresh.queuePanel(replacement);
    if (const int error = require(refresh.pendingPanelRefreshes.size() == 1
                                      && refresh.panelRequestQueued(
                                          QStringLiteral("replacement")),
                                  5)) {
        return error;
    }

    PanelRefreshRequest wholePanel;
    wholePanel.column = 0;
    wholePanel.row = 1;
    wholePanel.readMode = DataReadMode::Medium;
    wholePanel.key = QStringLiteral("whole");
    refresh.queuePanel(wholePanel);

    PanelRefreshRequest newerSignal = first;
    newerSignal.readMode = DataReadMode::Full;
    newerSignal.key = QStringLiteral("newer");
    refresh.queuePanel(newerSignal);
    if (const int error = require(refresh.pendingPanelRefreshes.size() == 1
                                      && refresh.pendingPanelRefreshes.front()
                                             .signalIndices.isEmpty()
                                      && refresh.pendingPanelRefreshes.front().key
                                             == QStringLiteral("newer")
                                      && refresh.pendingPanelRefreshes.front().readMode
                                             == DataReadMode::Full,
                                  6)) {
        return error;
    }

    const PanelRefreshRequest queued = refresh.takePendingPanel();
    if (const int error = require(queued.key == QStringLiteral("newer")
                                      && !refresh.hasPendingPanel(),
                                  7)) {
        return error;
    }

    if (const int error = require(refresh.fullShotDebounceDelay(false) == 120
                                      && refresh.fullShotDebounceDelay(false) == 160,
                                  8)) {
        return error;
    }
    refresh.resetFullShotDebounce();

    const QRectF automaticView(0.0, -1.0, 1.0, 2.0);
    if (const int error = require(
            !RefreshCoordinator::preservedRateView(false, automaticView).isValid()
                && RefreshCoordinator::preservedRateView(true, automaticView)
                       == automaticView
                && !RefreshCoordinator::preservedRateView(
                        true, QRectF()).isValid(),
            11)) {
        return error;
    }

    refresh.sshFullRefreshPending = true;
    refresh.sshPanelRefreshPending = true;
    const RefreshCoordinator::SshPendingWork work =
        refresh.takeSshPendingWork();
    if (const int error = require(work.global && work.panel && !work.prewarm
                                      && !refresh.sshFullRefreshPending
                                      && !refresh.sshPanelRefreshPending,
                                  9)) {
        return error;
    }

    refresh.pendingRefresh = true;
    refresh.activeRefreshKey = QStringLiteral("old");
    refresh.queuedFullRateRefreshViews.insert(QStringLiteral("0,0"),
                                              QRectF(0.0, -1.0, 1.0, 2.0));
    refresh.activeFullRateRefreshViews.insert(QStringLiteral("0,1"),
                                              QRectF(0.0, -1.0, 1.0, 2.0));
    refresh.attemptedSignals.insert(QStringLiteral("0,0,0"));
    refresh.streamedOk = 2;
    refresh.streamedFailed = 1;
    refresh.resetForEnvironmentLoad();
    return require(!refresh.pendingRefresh
                       && refresh.activeRefreshKey.isEmpty()
                       && refresh.activeDataFetchGeneration == 3
                       && refresh.queuedFullRateRefreshViews.isEmpty()
                       && refresh.activeFullRateRefreshViews.isEmpty()
                       && refresh.attemptedSignals.isEmpty()
                       && refresh.streamedOk == 0
                       && refresh.streamedFailed == 0,
                   10);
}

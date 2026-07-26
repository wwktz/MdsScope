// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "mds_client.hpp"
#include "ssh_tunnel_manager.hpp"

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent), rootPath_(std::move(rootPath))
{
    setWindowIcon(appIcon());
    sshTunnelManager_ = new SshTunnelManager(this);
    environmentPath_ = appEnvironmentDir(rootPath_);
    // Merging the bundled source index (~22k lines) is only needed once the
    // user opens the Data Source dialog or a fetch records a signal, both of
    // which happen well after startup and re-run the merge themselves. Defer it
    // off the constructor's critical path so it does not block the first frame.
    QTimer::singleShot(0, this, [this] { ensureSourceIndexCache(rootPath_); });
    exportBasePath_ = QSettings(uiSettingsPath(rootPath_), QSettings::IniFormat)
                          .value("export/base_dir", defaultExportBaseDir())
                          .toString();
    const int storedDefaultRate =
        QSettings(uiSettingsPath(rootPath_), QSettings::IniFormat)
            .value("rate/default_mode", static_cast<int>(DataReadMode::Thin))
            .toInt();
    switch (static_cast<DataReadMode>(storedDefaultRate)) {
    case DataReadMode::Thin:
    case DataReadMode::Medium:
    case DataReadMode::Full:
        defaultRateMode_ = static_cast<DataReadMode>(storedDefaultRate);
        break;
    default:
        defaultRateMode_ = DataReadMode::Thin;
        break;
    }
    globalRateMode_ = defaultRateMode_;
    loadFontSettings(rootPath_);
    buildUi();
    applyUiFont();
    connect(sshTunnelManager_,
            &SshTunnelManager::preparationFinished,
            this,
            [this] {
        // Queueing lets the original preparation return before the latest data
        // request resumes. Obsolete reads are discarded by their generation;
        // the persistent SSH forwarding process remains untouched.
        bool resumeFullRefresh = sshFullRefreshPending_;
        const bool resumePanelRefresh = sshPanelRefreshPending_;
        const bool resumePrewarm = sshPrewarmPending_;
        sshFullRefreshPending_ = false;
        sshPanelRefreshPending_ = false;
        sshPrewarmPending_ = false;

        if (resumePrewarm && pendingPrewarmRefresh_
            && !warmWatcher_.isRunning()) {
            if (prewarmConnections()) {
                return;
            }
            pendingPrewarmRefresh_ = false;
            resumeFullRefresh = true;
        }
        if (resumeFullRefresh) {
            pendingRefresh_ = false;
            queuedRefreshKey_.clear();
            refreshData();
            return;
        }
        if (resumePanelRefresh) {
            if (panelWatcher_.isRunning()) {
                cancelPanelFetch();
                activePanelRefreshKey_.clear();
            }
            startPendingFetchIfIdle();
        }
    },
            Qt::QueuedConnection);
    latestShotPollTimer_.setInterval(20'000);
    connect(&latestShotPollTimer_, &QTimer::timeout, this, [this] {
        fetchLatestShotAsync(false);
    });
    latestShotPollTimer_.start();
    connect(&panelWatcher_, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this] {
        if (!activePanelRefreshKey_.isEmpty()) {
            applyPanelLoadedSignals(panelWatcher_.result());
            activePanelRefreshKey_.clear();
        }
        activePanelColumn_ = -1;
        activePanelRow_ = -1;
        activePanelSignals_.clear();
        activePanelRateRefreshView_ = {};
        startPendingFetchIfIdle();
    });
    connect(&warmWatcher_, &QFutureWatcher<void>::finished, this, [this] {
        if (pendingRefresh_ || !pendingPanelRefreshes_.isEmpty()) {
            startPendingFetchIfIdle();
            return;
        }
        const bool idle = canStartDeferredRefresh();
        if (pendingPrewarmRefresh_ && idle) {
            pendingPrewarmRefresh_ = false;
            refreshData();
            return;
        }
        // If a fetch is still in flight, keep pendingPrewarmRefresh_ set so the
        // fetch-completion handler triggers the deferred initial refresh once it
        // drains, rather than dropping it here.
        if (idle) {
            setStatus("MDS connections ready");
        }
    });
    QTimer::singleShot(16, this, [this] {
        loadDefaultEnvironment(true);
    });
}

MainWindow::~MainWindow()
{
    cancelDataFetch();
    cancelPanelFetch();
    cancelPrewarmConnections();
    ++activeDataFetchGeneration_;
    ++latestShotGeneration_;
    ++topSummaryGeneration_;
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
}

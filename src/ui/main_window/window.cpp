// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "mds_client.hpp"
#include "ssh_tunnel_manager.hpp"

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent)
    , rootPath_(std::move(rootPath))
    , refresh_(std::make_unique<RefreshCoordinator>())
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
    shortcutBindings_ = loadShortcutBindings(rootPath_);
    shortcutSequenceTimer_.setSingleShot(true);
    shortcutSequenceTimer_.setInterval(500);
    connect(&shortcutSequenceTimer_,
            &QTimer::timeout,
            this,
            [this] { pendingShortcutKeys_.clear(); });
    shotEditExitTimer_.setSingleShot(true);
    shotEditExitTimer_.setInterval(1000);
    connect(&shotEditExitTimer_,
            &QTimer::timeout,
            this,
            [this] { pendingShotEditExitKeys_.clear(); });
    buildUi();
    qApp->installEventFilter(this);
    applyUiFont();
    connect(sshTunnelManager_,
            &SshTunnelManager::preparationFinished,
            this,
            [this] {
        // Queueing lets the original preparation return before the latest data
        // request resumes. Obsolete reads are discarded by their generation;
        // the persistent SSH forwarding process remains untouched.
        const RefreshCoordinator::SshPendingWork work =
            refresh_->takeSshPendingWork();
        bool resumeFullRefresh = work.global;
        const bool resumePanelRefresh = work.panel;
        const bool resumePrewarm = work.prewarm;

        if (resumePrewarm && refresh_->initialRefreshDeferred()
            && !refresh_->warmWatcher().isRunning()) {
            if (prewarmConnections()) {
                return;
            }
            refresh_->cancelDeferredInitialRefresh();
            resumeFullRefresh = true;
        }
        if (resumeFullRefresh) {
            refresh_->takePendingDataRefresh();
            refreshData();
            return;
        }
        if (resumePanelRefresh) {
            if (refresh_->panelWatcher().isRunning()) {
                cancelPanelFetch();
                refresh_->suppressPanelResults();
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
    refresh_->shotDebounceTimer().setSingleShot(true);
    connect(&refresh_->shotDebounceTimer(), &QTimer::timeout, this, [this] {
        refreshData();
    });
    connect(&refresh_->panelWatcher(), &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this] {
        const std::optional<PanelRefreshRequest> completed =
            refresh_->completePanelRefresh();
        if (completed) {
            applyPanelLoadedSignals(refresh_->panelWatcher().result(),
                                    *completed);
        }
        startPendingFetchIfIdle();
    });
    connect(&refresh_->warmWatcher(), &QFutureWatcher<void>::finished, this, [this] {
        if (refresh_->hasPendingDataRefresh()
            || refresh_->hasPendingPanel()) {
            startPendingFetchIfIdle();
            return;
        }
        const bool idle = canStartDeferredRefresh();
        if (refresh_->initialRefreshDeferred() && idle) {
            refresh_->takeDeferredInitialRefresh();
            refreshData();
            return;
        }
        // If a fetch is still in flight, keep the deferred flag set so the
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
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    cancelDataFetch();
    cancelPanelFetch();
    cancelPrewarmConnections();
    refresh_->invalidateDataFetch();
    ++latestShotGeneration_;
    ++topSummaryGeneration_;
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
}

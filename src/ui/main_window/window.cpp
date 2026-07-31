// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/app_paths.hpp"
#include "ui/visuals.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "shot_workflow.hpp"
#include "user_preferences.hpp"
#include "mds/mds_client.hpp"
#include "ssh/ssh_tunnel_manager.hpp"

#include <QApplication>

#include <utility>

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent)
    , rootPath_(std::move(rootPath))
    , preferences_(
          std::make_unique<UserPreferences>(uiSettingsPath(rootPath_)))
    , fontSettings_(loadFontSettings(rootPath_))
    , shotWorkflow_(std::make_unique<ShotWorkflow>(
          rootPath_, this, [this] { fetchLatestShotAsync(false); }))
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
    exportBasePath_ =
        preferences_->exportBasePath(defaultExportBaseDir());
    defaultRateMode_ = preferences_->defaultReadMode();
    globalRateMode_ = defaultRateMode_;
    shortcutBindings_ = loadShortcutBindings(rootPath_);
    shortcutSequenceTimer_.setSingleShot(true);
    shortcutSequenceTimer_.setInterval(500);
    connect(&shortcutSequenceTimer_,
            &QTimer::timeout,
            this,
            [this] {
        pendingShortcutKeys_.clear();
        const std::optional<ShortcutCommand> command =
            std::exchange(pendingExactShortcut_, std::nullopt);
        if (command && shortcutCommandEnabled(*command)) {
            triggerShortcutCommand(*command);
        }
    });
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
    shotWorkflow_->invalidateLatest();
    shotWorkflow_->invalidateSummary();
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
}

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/api_auth.hpp"
#include "core/mds_helpers.hpp"
#include "ui/visuals.hpp"
#include "main_window.hpp"
#include "shot_workflow.hpp"
#include "user_preferences.hpp"
#include "refresh_coordinator.hpp"
#include "about_dialog.hpp"
#include "shared.hpp"
#include "ui/login_dialog.hpp"

#include <QLineEdit>

namespace {
bool layoutUsesFullRate(const LayoutConfig& config, DataReadMode globalMode)
{
    for (const QVector<PlotSpec>& column : config.columns) {
        for (const PlotSpec& plot : column) {
            for (const SignalSpec& sig : plot.signalSpecs) {
                if (!sig.hidden
                    && effectiveSignalReadMode(globalMode, sig) == DataReadMode::Full) {
                    return true;
                }
            }
        }
    }
    return false;
}
}

void MainWindow::applyShot()
{
    if (!shotEdit_) {
        return;
    }
    const QString shot = shotEdit_->text().trimmed();
    if (shot.isEmpty()) {
        return;
    }
    if (shotEdit_->text() != shot) {
        shotEdit_->setText(shot);
    }
    shotEditSessionText_ = shot;
    shotEditSessionActive_ = false;
    pendingShotEditExitKeys_.clear();
    shotEditExitTimer_.stop();
    preferences_->rememberShotExpression(shot);
    refreshShotHistory();
    shotWorkflow_->invalidateLatest();
    setAllPlotShots(shot);
    scheduleShotRefresh();
}

void MainWindow::scheduleShotRefresh()
{
    // A preserved view belongs only to a Rate refresh of the same shot.
    // Never let a queued or in-flight Rate change apply its old X range after
    // Prev/Next has selected a different shot.
    refresh_->discardPreservedRateViews();

    if (!layoutUsesFullRate(config_, globalRateMode_)) {
        refresh_->resetFullShotDebounce();
        refreshData();
        return;
    }

    // Suppress any late old-shot result immediately, but let its transfer run
    // during the short debounce window. It may reach a clean socket boundary
    // before the final shot is known, avoiding an unnecessary abort/reconnect.
    if (refresh_->dataWorkerRunning()) {
        refresh_->suppressDataResults();
    }
    if (refresh_->panelWatcher().isRunning()) {
        refresh_->suppressPanelResults();
    }

    const int debounceMs = refresh_->fullShotDebounceDelay(
        refresh_->dataOrPanelRunning());
    refresh_->startFullShotDebounce(debounceMs);
    setStatus(QString("Shot selected; refreshing after %1 ms pause").arg(debounceMs));
}

void MainWindow::stepShot(int delta)
{
    if (!shotEdit_) {
        return;
    }
    if (shotEditSessionActive_) {
        cancelShotEditSession();
    }
    bool ok = false;
    const int shot = shotEdit_->text().trimmed().toInt(&ok);
    if (!ok) {
        return;
    }
    int next = std::max(0, shot + delta);
    if (delta > 0) {
        bool latestOk = false;
        const QString latestShot = shotWorkflow_->latestShot();
        const int latest = latestShot.trimmed().toInt(&latestOk);
        if (latestOk && next > latest) {
            if (shot != latest && shotEdit_->text() != latestShot) {
                shotEdit_->setText(latestShot);
                applyShot();
            } else {
                setStatus(QString("Already at latest shot %1").arg(latestShot));
            }
            return;
        }
    }
    const QString nextShot = QString::number(next);
    if (shotEdit_->text() != nextShot) {
        shotEdit_->setText(nextShot);
    }
    applyShot();
}

void MainWindow::latestShot()
{
    if (shotEditSessionActive_) {
        cancelShotEditSession();
    }
    // Latest is an active refresh, not a jump to a possibly stale cached value.
    fetchLatestShotAsync(true);
}

void MainWindow::openLoginDialog()
{
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        return;
    }
    LoginDialog dialog(rootPath_, this, apiUrl);
    const int result = dialog.exec();
    if (result == LoginDialog::LoggedOut) {
        applyLogoutStatus();
        return;
    }
    if (result != QDialog::Accepted) {
        return;
    }

    applyLoginSuccessStatus("Login token saved");
}

void MainWindow::openAboutDialog()
{
    showAboutDialog(fontSettings_, this);
}

void MainWindow::applyLoginSuccessStatus(const QString& statusText)
{
    updateLoginActionIcon();
    shotWorkflow_->clearLatest();
    shotWorkflow_->clearSummary();
    shotWorkflow_->invalidateSummary();
    updateTopInfoLabels();
    setStatus(statusText);

    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    if (currentShot.isEmpty()) {
        // A skipped first login leaves the init layout without a shot. Once
        // login succeeds, obtain the latest shot and let applyShot() refresh
        // the init data through the newly configured SSH connection.
        fetchLatestShotAsync(true);
    } else {
        // Re-login should preserve the shot the user is viewing while
        // reconnecting its MDS sources and metadata with the new credentials.
        scheduleTopInfoUpdate(currentShot);
        refreshData();
    }
}

void MainWindow::applyLogoutStatus()
{
    updateLoginActionIcon();
    shotWorkflow_->clearLatest();
    shotWorkflow_->clearSummary();
    shotWorkflow_->invalidateLatest();
    shotWorkflow_->invalidateSummary();
    updateTopInfoLabels();
    setStatus("Logged out");
}

void MainWindow::updateLoginActionIcon()
{
    if (!loginAction_) {
        return;
    }

    CachedAuth auth;
    const bool loggedIn = loadCachedAuth(&auth)
        && !auth.token.trimmed().isEmpty()
        && !tokenExpiresSoon(auth.token);
    loginAction_->setIcon(loginIcon(loggedIn));
}

void MainWindow::fetchLatestShotAsync(bool applyLatest)
{
    if (applyLatest) {
        setStatus("Fetching latest shot...");
    }
    const ShotWorkflow::LatestRequest request =
        shotWorkflow_->beginLatestFetch(applyLatest);
    if (!request.started) {
        return;
    }
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        if (shotWorkflow_->failLatestFetchStart()) {
            setStatus("Latest shot unavailable through SSH");
        }
        return;
    }
    QThreadPool::globalInstance()->start([this, generation = request.generation, apiUrl] {
        const QString latest = shotWorkflow_->fetchLatest(apiUrl);
        QMetaObject::invokeMethod(this, [this, latest, generation] {
            const ShotWorkflow::LatestCompletion completion =
                shotWorkflow_->completeLatestFetch(generation, latest);
            if (!completion.current) {
                return;
            }
            if (latest.isEmpty()) {
                cachedApiSourceUrl_.clear();
                cachedPreparedApiUrl_.clear();
                if (completion.shouldApply) {
                    refresh_->cancelDeferredInitialRefresh();
                    setStatus("Latest shot unavailable");
                }
                return;
            }
            if (!completion.shouldApply) {
                return;
            }
            const QString latestShot = shotWorkflow_->latestShot();
            if (shotEdit_ && shotEdit_->text() != latestShot) {
                shotEdit_->setText(latestShot);
            }
            if (refresh_->initialRefreshDeferred()) {
                preferences_->rememberShotExpression(latestShot);
                refreshShotHistory();
                setAllPlotShots(latestShot);
                setStatus(QString("Preparing MDS connections for shot %1...").arg(latestShot));
                if (prewarmConnections()) {
                    return;
                }
                refresh_->cancelDeferredInitialRefresh();
            }
            applyShot();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateShotControlsFromConfig(const QString& preferredShot)
{
    QString shot = preferredShot.trimmed();
    if (shot.isEmpty()) {
        shot = maxShotInConfig();
    }
    if (!shot.isEmpty()) {
        if (shotEdit_) {
            shotEdit_->setText(shot);
        }
        shotWorkflow_->invalidateLatest();
        setAllPlotShots(shot);
    }
}

void MainWindow::setAllPlotShots(const QString& shot)
{
    bool changed = false;
    for (auto& col : config_.columns) {
        for (PlotSpec& plot : col) {
            if (plot.shot != shot) {
                plot.shot = shot;
                changed = true;
            }
        }
    }
    if (changed) {
        syncDisplayConfig();
    }
    updateTopInfoLabels();
    setStatus(QString("ShotNo:%1").arg(shot));
}

QString MainWindow::maxShotInConfig() const
{
    int best = -1;
    QString bestText;
    for (const auto& col : config_.columns) {
        for (const PlotSpec& plot : col) {
            bool ok = false;
            const int shot = plot.shot.trimmed().toInt(&ok);
            if (ok && shot > best) {
                best = shot;
                bestText = plot.shot.trimmed();
            }
        }
    }
    return bestText;
}

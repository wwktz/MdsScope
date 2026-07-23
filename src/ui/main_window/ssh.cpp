// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "ssh_dialog.hpp"
#include "ssh_tunnel_manager.hpp"

#include <QMessageBox>

void MainWindow::openSshDialog()
{
    SshDialog dialog(sshTunnelManager_, this);
    dialog.setWindowIcon(appIcon());
    dialog.exec();
    // Saving or testing SSH settings rebuilds the tunnel manager. Do not reuse
    // an API URL prepared with the previous settings when the user logs in.
    cachedApiSourceUrl_.clear();
    cachedPreparedApiUrl_.clear();
    updateSshActionIcon();
}

bool MainWindow::prepareSshLayout(const LayoutConfig& source, LayoutConfig* prepared)
{
    if (!sshTunnelManager_) {
        *prepared = source;
        return true;
    }
    QString error;
    if (sshTunnelManager_->prepareLayout(source, prepared, &error)) {
        updateSshActionIcon();
        return true;
    }
    updateSshActionIcon();
    QMessageBox::warning(this,
                         QStringLiteral("SSH Remote Access"),
                         error.isEmpty() ? QStringLiteral("Could not establish the SSH tunnel.") : error);
    setStatus(QStringLiteral("SSH connection failed"));
    return false;
}

bool MainWindow::prepareSshUrl(const QString& source, QString* prepared)
{
    if (!sshTunnelManager_) {
        *prepared = source;
        return true;
    }
    // A direct URL remains valid independently of SSH state. For a forwarded
    // URL, let SshTunnelManager verify the endpoint-specific QProcess before
    // reusing it: another live tunnel can keep the manager's aggregate state
    // Connected even after this API tunnel has exited.
    if (!cachedPreparedApiUrl_.isEmpty()
        && cachedApiSourceUrl_ == source
        && cachedPreparedApiUrl_ == source) {
        *prepared = cachedPreparedApiUrl_;
        return true;
    }
    cachedApiSourceUrl_.clear();
    cachedPreparedApiUrl_.clear();
    QString error;
    if (sshTunnelManager_->prepareUrl(source, prepared, &error)) {
        cachedApiSourceUrl_ = source;
        cachedPreparedApiUrl_ = *prepared;
        updateSshActionIcon();
        return true;
    }
    cachedApiSourceUrl_.clear();
    cachedPreparedApiUrl_.clear();
    updateSshActionIcon();
    setStatus(error.isEmpty() ? QStringLiteral("SSH API tunnel failed") : error);
    return false;
}

void MainWindow::updateSshActionIcon()
{
    if (!sshAction_ || !sshTunnelManager_) {
        return;
    }
    const auto state = sshTunnelManager_->state();
    sshAction_->setIcon(sshIcon(static_cast<int>(state)));
    QString tooltip = QStringLiteral("SSH remote access");
    switch (state) {
    case SshTunnelManager::State::Unconfigured:
        tooltip += QStringLiteral(": disabled or not configured");
        break;
    case SshTunnelManager::State::Ready:
        tooltip += QStringLiteral(": configured");
        break;
    case SshTunnelManager::State::Connecting:
        tooltip += QStringLiteral(": connecting");
        break;
    case SshTunnelManager::State::Connected:
        tooltip += QStringLiteral(": tunnel connected");
        break;
    case SshTunnelManager::State::Error:
        tooltip += QStringLiteral(": connection failed");
        break;
    }
    sshAction_->setToolTip(tooltip);
}

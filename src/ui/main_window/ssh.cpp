// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/visuals.hpp"
#include "main_window.hpp"
#include "ssh/ssh_tunnel_manager.hpp"
#include "ui/ssh_dialog.hpp"

#include <QMessageBox>

void MainWindow::openSshDialog()
{
    SshDialog dialog(sshTunnelManager_, this);
    dialog.setWindowIcon(appIcon());
    dialog.exec();
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
    QString error;
    if (sshTunnelManager_->prepareUrl(source, prepared, &error)) {
        updateSshActionIcon();
        return true;
    }
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
        tooltip += QStringLiteral(": configured; connects when needed");
        break;
    case SshTunnelManager::State::Connecting:
        tooltip += QStringLiteral(": connecting");
        break;
    case SshTunnelManager::State::Connected:
        tooltip += QStringLiteral(": tunnel connected");
        break;
    case SshTunnelManager::State::Error:
        tooltip += QStringLiteral(": connection failed");
        if (!sshTunnelManager_->lastError().isEmpty()) {
            tooltip += QStringLiteral(" — ")
                       + sshTunnelManager_->lastError().simplified().left(160);
        }
        break;
    }
    sshAction_->setToolTip(tooltip);
}

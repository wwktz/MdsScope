// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh/ssh_tunnel_manager.hpp"

#include <QCoreApplication>
#include <QProcess>

#include <iostream>

class SshTunnelManagerTestAccess {
public:
    static QProcess* addTunnel(SshTunnelManager& manager,
                               const QString& endpoint)
    {
        auto* process = new QProcess(&manager);
        SshTunnelManager::Tunnel tunnel;
        tunnel.endpoint = endpoint;
        tunnel.process = process;
        manager.tunnels_.insert(endpoint, tunnel);
        manager.state_ = SshTunnelManager::State::Connected;
        return process;
    }

    static void finish(SshTunnelManager& manager,
                       const QString& endpoint,
                       QProcess* process)
    {
        manager.handleTunnelFinished(endpoint, process);
    }

    static int tunnelCount(const SshTunnelManager& manager)
    {
        return manager.tunnels_.size();
    }

    static QStringList arguments(const SshSettings& settings,
                                 int connectTimeoutSeconds)
    {
        return SshTunnelManager::commonArguments(
            settings, true, connectTimeoutSeconds);
    }
};

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    {
        for (const int timeout : {2, 4, 6}) {
            const QStringList arguments =
                SshTunnelManagerTestAccess::arguments(SshSettings{}, timeout);
            ok &= expect(
                arguments.contains(
                    QStringLiteral("ConnectTimeout=%1").arg(timeout)),
                "SSH progressive connection timeout is missing");
            ok &= expect(arguments.contains(QStringLiteral("ConnectionAttempts=1")),
                         "OpenSSH internal retry conflicts with staged retries");
        }
    }

    {
        SshTunnelManager manager;
        QProcess* process = SshTunnelManagerTestAccess::addTunnel(
            manager, QStringLiteral("mds.example:8000"));
        SshTunnelManagerTestAccess::finish(
            manager, QStringLiteral("mds.example:8000"), process);
        ok &= expect(manager.state() == SshTunnelManager::State::Ready,
                     "last tunnel exit did not return SSH to Ready");
        ok &= expect(manager.lastError().isEmpty(),
                     "normal tunnel exit left a stale SSH error");
        ok &= expect(SshTunnelManagerTestAccess::tunnelCount(manager) == 0,
                     "finished tunnel was not removed");
    }

    {
        SshTunnelManager manager;
        QProcess* first = SshTunnelManagerTestAccess::addTunnel(
            manager, QStringLiteral("first.example:8000"));
        SshTunnelManagerTestAccess::addTunnel(
            manager, QStringLiteral("second.example:8000"));
        SshTunnelManagerTestAccess::finish(
            manager, QStringLiteral("first.example:8000"), first);
        ok &= expect(manager.state() == SshTunnelManager::State::Connected,
                     "one tunnel exit hid another connected tunnel");
        ok &= expect(SshTunnelManagerTestAccess::tunnelCount(manager) == 1,
                     "wrong tunnel count after one of two tunnels exited");
    }

    {
        SshTunnelManager manager;
        QProcess* oldProcess = SshTunnelManagerTestAccess::addTunnel(
            manager, QStringLiteral("replacement.example:8000"));
        SshTunnelManagerTestAccess::addTunnel(
            manager, QStringLiteral("replacement.example:8000"));
        SshTunnelManagerTestAccess::finish(
            manager, QStringLiteral("replacement.example:8000"), oldProcess);
        ok &= expect(manager.state() == SshTunnelManager::State::Connected,
                     "stale process callback changed connected state");
        ok &= expect(SshTunnelManagerTestAccess::tunnelCount(manager) == 1,
                     "stale process callback removed its replacement");
    }

    return ok ? 0 : 1;
}

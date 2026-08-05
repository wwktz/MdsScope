// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh_tunnel_manager.hpp"
#include "core/api_auth.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <array>

namespace {
constexpr int kDirectReachabilityTimeoutMs = 450;
constexpr int kSshAttemptGraceMs = 3000;
constexpr std::array<int, 3> kSshConnectTimeoutSeconds{2, 4, 6};

bool isTransientSshFailure(const QString& message)
{
    const QString lower = message.toLower();
    static const std::array<const char*, 10> markers{
        "connection timed out",
        "operation timed out",
        "connection refused",
        "connection reset",
        "connection closed",
        "no route to host",
        "network is unreachable",
        "temporary failure in name resolution",
        "kex_exchange_identification",
        "banner exchange",
    };
    for (const char* marker : markers) {
        if (lower.contains(QString::fromLatin1(marker))) {
            return true;
        }
    }
    return false;
}

bool waitForProcessStarted(QProcess& process, int timeoutMs)
{
    if (process.state() == QProcess::Running) {
        return true;
    }
    if (process.state() == QProcess::NotRunning) {
        return false;
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&process, &QProcess::started, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return process.state() == QProcess::Running;
}

bool waitForProcessFinished(QProcess& process, int timeoutMs)
{
    if (process.state() == QProcess::NotRunning) {
        return true;
    }
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return process.state() == QProcess::NotRunning;
}
}

SshTunnelManager::SshTunnelManager(QObject* parent)
    : QObject(parent)
{
    reloadSettings();
}

SshTunnelManager::~SshTunnelManager()
{
    disconnectAll();
}

void SshTunnelManager::reloadSettings()
{
    CachedAuth auth;
    settings_ = loadCachedAuth(&auth) ? auth.ssh : SshSettings{};
    if (settings_.mode == SshMode::Disabled || settings_.host.trimmed().isEmpty()) {
        setState(State::Unconfigured);
    } else if (tunnels_.isEmpty()) {
        setState(State::Ready);
    }
}

bool SshTunnelManager::splitEndpoint(const QString& endpoint, QString* host, int* port)
{
    QString value = endpoint.trimmed();
    if (value.isEmpty()) {
        return false;
    }
    *port = 8000;
    if (value.startsWith('[')) {
        const qsizetype close = value.indexOf(']');
        if (close <= 1) {
            return false;
        }
        *host = value.mid(1, close - 1);
        if (close + 1 < value.size() && value.at(close + 1) == ':') {
            bool ok = false;
            const int parsed = value.mid(close + 2).toInt(&ok);
            if (!ok || parsed < 1 || parsed > 65535) {
                return false;
            }
            *port = parsed;
        }
        return true;
    }
    const qsizetype colon = value.lastIndexOf(':');
    if (colon > 0 && value.indexOf(':') == colon) {
        bool ok = false;
        const int parsed = value.mid(colon + 1).toInt(&ok);
        if (ok && parsed >= 1 && parsed <= 65535) {
            value = value.left(colon);
            *port = parsed;
        }
    }
    *host = value;
    return !host->isEmpty();
}

bool SshTunnelManager::tcpReachable(const QString& host, int port, int timeoutMs)
{
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(host, static_cast<quint16>(port));
    if (socket.state() != QAbstractSocket::ConnectedState
        && socket.state() != QAbstractSocket::UnconnectedState) {
        timer.start(timeoutMs);
        loop.exec();
    }
    const bool connected = socket.state() == QAbstractSocket::ConnectedState;
    socket.abort();
    return connected;
}

int SshTunnelManager::reserveLocalPort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return static_cast<int>(server.serverPort());
}

QString SshTunnelManager::sshTarget(const SshSettings& settings)
{
    const QString host = settings.host.trimmed();
    return settings.user.trimmed().isEmpty() ? host : settings.user.trimmed() + '@' + host;
}

QStringList SshTunnelManager::commonArguments(const SshSettings& settings,
                                              bool tunnel,
                                              int connectTimeoutSeconds)
{
    QStringList args{
        QStringLiteral("-C"),
        QStringLiteral("-T"),
        QStringLiteral("-p"), QString::number(settings.port),
        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=%1").arg(connectTimeoutSeconds),
        // Retries are managed here so each attempt can use the progressive
        // 2/4/6-second timeout schedule.
        QStringLiteral("-o"), QStringLiteral("ConnectionAttempts=1"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=30"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveCountMax=3"),
        QStringLiteral("-o"), QStringLiteral("NumberOfPasswordPrompts=1"),
    };
    if (tunnel) {
        args << QStringLiteral("-N")
             << QStringLiteral("-o") << QStringLiteral("ExitOnForwardFailure=yes");
    }
    if (settings.password.isEmpty() && !settings.identityFile.trimmed().isEmpty()) {
        QString identity = settings.identityFile.trimmed();
        if (identity == QStringLiteral("~")) {
            identity = QDir::homePath();
        } else if (identity.startsWith(QStringLiteral("~/"))) {
            identity = QDir::home().filePath(identity.mid(2));
        }
        args << QStringLiteral("-i") << identity;
    }
    if (settings.password.isEmpty()) {
        args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    } else {
        args << QStringLiteral("-o") << QStringLiteral("PreferredAuthentications=password,keyboard-interactive");
    }
    return args;
}

void SshTunnelManager::configureAskPass(QProcess* process, const SshSettings& settings)
{
    if (!process) {
        return;
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (environment.contains(QStringLiteral("MDSSCOPE_PORTABLE_ROOT"))) {
        environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
        environment.remove(QStringLiteral("QT_PLUGIN_PATH"));
        environment.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
        environment.remove(QStringLiteral("GIO_MODULE_DIR"));
        environment.remove(QStringLiteral("GIO_EXTRA_MODULES"));
        environment.remove(QStringLiteral("GIO_USE_VFS"));
    }
    if (!settings.password.isEmpty()) {
        const QString portableLauncher = environment.value(QStringLiteral("MDSSCOPE_PORTABLE_LAUNCHER"));
        environment.insert(QStringLiteral("SSH_ASKPASS"),
                           portableLauncher.isEmpty() ? QCoreApplication::applicationFilePath()
                                                      : portableLauncher);
        environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        environment.insert(QStringLiteral("MDSSCOPE_SSH_ASKPASS"), QStringLiteral("1"));
        if (!environment.contains(QStringLiteral("DISPLAY"))) {
            environment.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
        }
    }
    process->setProcessEnvironment(environment);
}

bool SshTunnelManager::testConnection(const SshSettings& settings, QString* error)
{
    if (settings.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH host is required.");
        return false;
    }
    setState(State::Connecting, QStringLiteral("Testing SSH connection..."));
    QString lastError;
    for (const int connectTimeoutSeconds : kSshConnectTimeoutSeconds) {
        QProcess process;
        configureAskPass(&process, settings);
        QStringList args = commonArguments(settings, false, connectTimeoutSeconds);
        args << sshTarget(settings) << QStringLiteral("true");
        process.start(QStringLiteral("ssh"), args);
        if (!waitForProcessStarted(process, 3000)) {
            lastError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            if (lastError.isEmpty()) {
                lastError = process.errorString();
            }
            if (process.error() == QProcess::FailedToStart
                || !isTransientSshFailure(lastError)) {
                break;
            }
            continue;
        }
        const int attemptDeadlineMs = connectTimeoutSeconds * 1000
                                      + kSshAttemptGraceMs;
        if (!waitForProcessFinished(process, attemptDeadlineMs)) {
            process.kill();
            process.waitForFinished(1000);
            lastError = QStringLiteral("SSH connection timed out.");
            continue;
        }
        if (process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0) {
            setState(State::Ready, QStringLiteral("SSH login succeeded"));
            return true;
        }
        lastError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (lastError.isEmpty()) {
            lastError = QStringLiteral("SSH exited with code %1.").arg(process.exitCode());
        }
        if (!isTransientSshFailure(lastError)) {
            break;
        }
    }
    *error = lastError.isEmpty() ? QStringLiteral("SSH connection failed.")
                                 : lastError;
    setState(State::Error, *error);
    return false;
}

bool SshTunnelManager::ensureTunnel(const QString& endpoint, QString* localEndpoint, QString* error)
{
    auto existing = tunnels_.find(endpoint);
    if (existing != tunnels_.end() && existing->process
        && existing->process->state() != QProcess::NotRunning) {
        *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
        return true;
    }

    QString remoteHost;
    int remotePort = 8000;
    if (!splitEndpoint(endpoint, &remoteHost, &remotePort)) {
        *error = QStringLiteral("Invalid MDS server address: %1").arg(endpoint);
        return false;
    }
    const int localPort = reserveLocalPort();
    if (localPort <= 0) {
        *error = QStringLiteral("Cannot allocate a local SSH forwarding port.");
        return false;
    }

    const bool keepConnectedState = !tunnels_.isEmpty();
    if (!keepConnectedState) {
        setState(State::Connecting, QStringLiteral("Connecting through SSH..."));
    }
    const QString forwardingHost = remoteHost.contains(':')
                                       ? QStringLiteral("[%1]").arg(remoteHost)
                                       : remoteHost;
    QString lastError;
    for (const int connectTimeoutSeconds : kSshConnectTimeoutSeconds) {
        auto* process = new QProcess(this);
        configureAskPass(process, settings_);
        QStringList args = commonArguments(settings_, true, connectTimeoutSeconds);
        args << QStringLiteral("-L")
             << QStringLiteral("127.0.0.1:%1:%2:%3")
                    .arg(localPort)
                    .arg(forwardingHost)
                    .arg(remotePort)
             << sshTarget(settings_);
        process->start(QStringLiteral("ssh"), args);
        if (!waitForProcessStarted(*process, 3000)) {
            lastError = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            if (lastError.isEmpty()) {
                lastError = process->errorString();
            }
            const bool retry = process->error() != QProcess::FailedToStart
                               && isTransientSshFailure(lastError);
            delete process;
            if (retry) {
                continue;
            }
            break;
        }

        QElapsedTimer timer;
        timer.start();
        const int attemptDeadlineMs = connectTimeoutSeconds * 1000
                                      + kSshAttemptGraceMs;
        while (timer.elapsed() < attemptDeadlineMs
               && process->state() != QProcess::NotRunning) {
            if (tcpReachable(QStringLiteral("127.0.0.1"), localPort, 100)) {
                Tunnel tunnel;
                tunnel.endpoint = endpoint;
                tunnel.host = remoteHost;
                tunnel.remotePort = remotePort;
                tunnel.localPort = localPort;
                tunnel.process = process;
                tunnels_.insert(endpoint, tunnel);
                connect(process, &QProcess::finished, this, [this, endpoint, process](int, QProcess::ExitStatus) {
                    handleTunnelFinished(endpoint, process);
                });
                *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(localPort);
                setState(State::Connected, QStringLiteral("SSH tunnel connected"));
                return true;
            }
            QEventLoop tick;
            QTimer::singleShot(25, &tick, &QEventLoop::quit);
            tick.exec();
        }

        const bool attemptTimedOut = process->state() != QProcess::NotRunning;
        lastError = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (attemptTimedOut) {
            process->kill();
            process->waitForFinished(1000);
            if (lastError.isEmpty()) {
                lastError = QStringLiteral("SSH tunnel setup timed out.");
            }
        }
        const bool retry = attemptTimedOut || isTransientSshFailure(lastError);
        delete process;
        if (!retry) {
            break;
        }
    }

    *error = lastError.isEmpty() ? QStringLiteral("SSH tunnel setup failed.")
                                 : lastError;
    if (!keepConnectedState) {
        setState(State::Error, *error);
    }
    return false;
}

void SshTunnelManager::handleTunnelFinished(const QString& endpoint, QProcess* process)
{
    const auto current = tunnels_.find(endpoint);
    if (current != tunnels_.end() && current->process == process) {
        tunnels_.erase(current);
        if (tunnels_.isEmpty()) {
            // Forwarding processes are disposable and are recreated on demand.
            // Their asynchronous exit is not, by itself, a failed connection
            // attempt and must not leave the toolbar in a stale error state.
            setState(State::Ready,
                     QStringLiteral("SSH tunnel closed; it will reconnect when needed"));
        } else {
            setState(State::Connected, QStringLiteral("SSH tunnel connected"));
        }
    }
    process->deleteLater();
}

bool SshTunnelManager::prepareLayout(const LayoutConfig& source, LayoutConfig* prepared, QString* error)
{
    if (!prepared) {
        return false;
    }
    *prepared = source;
    reloadSettings();
    if (settings_.mode == SshMode::Disabled) {
        return true;
    }
    if (settings_.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH is enabled but no SSH host is configured.");
        setState(State::Error, *error);
        return false;
    }
    if (preparationInProgress_) {
        *error = QStringLiteral("SSH tunnel preparation is already in progress.");
        return false;
    }
    preparationInProgress_ = true;
    const auto preparationGuard = qScopeGuard([this] {
        preparationInProgress_ = false;
        emit preparationFinished();
    });

    QHash<QString, QString> mapped;
    for (auto& column : prepared->columns) {
        for (PlotSpec& plot : column) {
            for (SignalSpec& signal : plot.signalSpecs) {
                const QString endpoint = signal.serverIp.trimmed();
                if (endpoint.isEmpty() || mapped.contains(endpoint)) {
                    if (mapped.contains(endpoint)) {
                        signal.serverIp = mapped.value(endpoint);
                    }
                    continue;
                }
                QString host;
                int port = 8000;
                if (!splitEndpoint(endpoint, &host, &port)) {
                    continue;
                }
                auto existing = tunnels_.find(endpoint);
                if (existing != tunnels_.end() && existing->process
                    && existing->process->state() != QProcess::NotRunning) {
                    const QString localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
                    mapped.insert(endpoint, localEndpoint);
                    signal.serverIp = localEndpoint;
                    continue;
                }
                if (settings_.mode == SshMode::Auto
                    && tcpReachable(host, port, kDirectReachabilityTimeoutMs)) {
                    mapped.insert(endpoint, endpoint);
                    continue;
                }
                QString localEndpoint;
                if (!ensureTunnel(endpoint, &localEndpoint, error)) {
                    return false;
                }
                mapped.insert(endpoint, localEndpoint);
                signal.serverIp = localEndpoint;
            }
        }
    }
    return true;
}

bool SshTunnelManager::prepareUrl(const QString& source, QString* prepared, QString* error)
{
    return prepareUrlImpl(source, prepared, error, true);
}

bool SshTunnelManager::prepareUrlViaSsh(const QString& source, QString* prepared, QString* error)
{
    return prepareUrlImpl(source, prepared, error, false);
}

bool SshTunnelManager::prepareUrlImpl(const QString& source,
                                      QString* prepared,
                                      QString* error,
                                      bool allowDirect)
{
    if (!prepared) {
        return false;
    }
    *prepared = source;
    const QUrl url(source);
    if (!url.isValid() || url.host().isEmpty()) {
        *error = QStringLiteral("Invalid URL.");
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        *error = QStringLiteral("SSH web tunneling supports HTTP and HTTPS URLs only.");
        return false;
    }

    reloadSettings();
    if (settings_.mode == SshMode::Disabled && allowDirect) {
        return true;
    }
    if (settings_.mode == SshMode::Disabled) {
        *error = QStringLiteral("SSH is disabled.");
        return false;
    }
    if (settings_.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH is enabled but no SSH host is configured.");
        return false;
    }
    if (preparationInProgress_) {
        *error = QStringLiteral("SSH tunnel preparation is already in progress.");
        return false;
    }
    preparationInProgress_ = true;
    const auto preparationGuard = qScopeGuard([this] {
        preparationInProgress_ = false;
        emit preparationFinished();
    });

    const int remotePort = url.port(scheme == QStringLiteral("https") ? 443 : 80);
    const QString endpoint = url.host().contains(':')
                                 ? QStringLiteral("[%1]:%2").arg(url.host()).arg(remotePort)
                                 : QStringLiteral("%1:%2").arg(url.host()).arg(remotePort);

    auto existing = tunnels_.find(endpoint);
    QString localEndpoint;
    if (existing != tunnels_.end() && existing->process
        && existing->process->state() != QProcess::NotRunning) {
        localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
    } else {
        if (allowDirect && settings_.mode == SshMode::Auto
            && tcpReachable(url.host(),
                            remotePort,
                            kDirectReachabilityTimeoutMs)) {
            return true;
        }
        if (!ensureTunnel(endpoint, &localEndpoint, error)) {
            return false;
        }
    }

    QString localHost;
    int localPort = 0;
    if (!splitEndpoint(localEndpoint, &localHost, &localPort)) {
        *error = QStringLiteral("Invalid local SSH tunnel endpoint.");
        return false;
    }
    QUrl tunneled = url;
    tunneled.setHost(localHost);
    tunneled.setPort(localPort);
    *prepared = tunneled.toString(QUrl::FullyEncoded);
    return true;
}

void SshTunnelManager::disconnectAll()
{
    const auto tunnels = tunnels_;
    tunnels_.clear();
    for (const Tunnel& tunnel : tunnels) {
        if (!tunnel.process) {
            continue;
        }
        tunnel.process->disconnect(this);
        if (tunnel.process->state() != QProcess::NotRunning) {
            tunnel.process->terminate();
            if (!tunnel.process->waitForFinished(1000)) {
                tunnel.process->kill();
                tunnel.process->waitForFinished(1000);
            }
        }
        delete tunnel.process;
    }
    setState(settings_.mode == SshMode::Disabled || settings_.host.isEmpty() ? State::Unconfigured : State::Ready);
}

void SshTunnelManager::setState(State state, const QString& detail)
{
    state_ = state;
    lastError_ = state == State::Error ? detail : QString();
    emit stateChanged(state_, detail);
}

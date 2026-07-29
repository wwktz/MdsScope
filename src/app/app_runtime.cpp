// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_runtime.hpp"
#include "login_bootstrap.hpp"
#include "platform_integration.hpp"
#include "runtime_paths.hpp"
#include "theme_runtime.hpp"
#include "core/api_auth.hpp"
#include "core/version.hpp"
#include "ui/visuals.hpp"
#include "ssh/ssh_diagnostic.hpp"
#include "ssh/ssh_tunnel_manager.hpp"
#include "ui/main_window/main_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QTextStream>
#include <QThreadPool>

#include <algorithm>

int runMdsScopeApplication(int argc, char* argv[])
{
    if (qEnvironmentVariable("MDSSCOPE_SSH_ASKPASS") == QStringLiteral("1")) {
        QCoreApplication::setApplicationName("mdsscope");
        QCoreApplication::setOrganizationName("MdsScope");
        QCoreApplication helper(argc, argv);
        CachedAuth auth;
        if (loadCachedAuth(&auth) && !auth.ssh.password.isEmpty()) {
            QTextStream(stdout) << auth.ssh.password << Qt::endl;
            return 0;
        }
        return 1;
    }

    preparePlatformProcess();
    QApplication::setApplicationName("mdsscope");
    QApplication::setApplicationDisplayName("MdsScope");
    QApplication::setApplicationVersion(QStringLiteral(MDSSCOPE_VERSION));
    if (desktopFileIsInstalled("mdsscope")) {
        QApplication::setDesktopFileName("mdsscope");
    }
    QApplication::setOrganizationName("MdsScope");

    QApplication app(argc, argv);
    QApplication::setWindowIcon(appIcon());
    installPlatformApplicationIntegration();
    installThemeRuntime(app);
    QThreadPool::globalInstance()->setMaxThreadCount(16);
    QThreadPool::globalInstance()->setExpiryTimeout(300000);

    const QDir workDir = runtimeRootDir();
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--ssh-api-test"))) {
        return runSshApiTest(workDir.absolutePath());
    }

    const qsizetype sshBenchmarkIndex = args.indexOf(QStringLiteral("--ssh-benchmark"));
    if (sshBenchmarkIndex >= 0) {
        QString configPath = QDir(workDir).filePath(QStringLiteral("environment/init.toml"));
        if (sshBenchmarkIndex + 1 < args.size()
            && !args[sshBenchmarkIndex + 1].startsWith(QStringLiteral("--"))) {
            configPath = args[sshBenchmarkIndex + 1];
        }
        QString shotOverride;
        const qsizetype shotIndex = args.indexOf(QStringLiteral("--shot"));
        if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
            shotOverride = args[shotIndex + 1].trimmed();
        }
        return runSshTunnelBenchmark(configPath, shotOverride);
    }

    const qsizetype benchmarkIndex =
        args.indexOf(QStringLiteral("--benchmark")) >= 0
        ? args.indexOf(QStringLiteral("--benchmark"))
        : args.indexOf(QStringLiteral("--bench"));
    if (benchmarkIndex >= 0) {
        QString configPath =
            QDir(workDir).filePath(QStringLiteral("environment/high_desity_impurity_0626.webscp"));
        if (benchmarkIndex + 1 < args.size()
            && !args[benchmarkIndex + 1].startsWith(QStringLiteral("--"))) {
            configPath = args[benchmarkIndex + 1];
        }
        QString shotOverride;
        const qsizetype shotIndex = args.indexOf(QStringLiteral("--shot"));
        if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
            shotOverride = args[shotIndex + 1].trimmed();
        }
        int repeat = 1;
        const qsizetype repeatIndex = args.indexOf(QStringLiteral("--repeat"));
        if (repeatIndex >= 0 && repeatIndex + 1 < args.size()) {
            bool ok = false;
            const int parsed = args[repeatIndex + 1].toInt(&ok);
            if (ok && parsed > 0) {
                repeat = parsed;
            }
        }
        const DataReadMode readMode = args.contains(QStringLiteral("--full"))
                                          ? DataReadMode::Full
                                      : args.contains(QStringLiteral("--medium"))
                                          ? DataReadMode::Medium
                                          : DataReadMode::Thin;
        int code = 0;
        for (int i = 0; i < repeat; ++i) {
            code = std::max(code,
                            runMdsScopeBenchmark(configPath,
                                                 readMode,
                                                 shotOverride,
                                                 args.contains(QStringLiteral("--summary")),
                                                 args.contains(QStringLiteral("--prewarm"))));
        }
        return code;
    }

    if (!ensureApiLoginBeforeMain(workDir.absolutePath())) {
        shutdownMdsScopeWorkers();
        return 1;
    }

    int code = 0;
    {
        MainWindow window(workDir.absolutePath());
        window.resize(1440, 920);
        applyPlatformWindowIntegration(&window);
        window.show();
        code = app.exec();
    }
    shutdownMdsScopeWorkers();
    return code;
}

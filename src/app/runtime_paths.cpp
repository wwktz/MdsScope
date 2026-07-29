// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime_paths.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

#include <utility>

QDir runtimeRootDir()
{
    const auto resourceRootPath = [](const QDir& base) -> QString {
        if (base.exists(QStringLiteral("environment"))) {
            return base.absolutePath();
        }
        const QDir resources(base.filePath(QStringLiteral("resources")));
        if (resources.exists(QStringLiteral("environment"))) {
            return resources.absolutePath();
        }
        return {};
    };

    const QString configuredRoot =
        qEnvironmentVariable("MDSSCOPE_RESOURCE_ROOT");
    if (!configuredRoot.isEmpty()) {
        const QString resourceRoot =
            resourceRootPath(QDir(configuredRoot));
        if (!resourceRoot.isEmpty()) {
            return QDir(resourceRoot);
        }
    }

    QDir directory(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QDir bundleResources(directory);
    if (bundleResources.cdUp()
        && bundleResources.cd(QStringLiteral("Resources"))) {
        const QString resourceRoot =
            resourceRootPath(bundleResources);
        if (!resourceRoot.isEmpty()) {
            return QDir(resourceRoot);
        }
    }
#endif
    for (int i = 0; i < 4; ++i) {
        const QString resourceRoot = resourceRootPath(directory);
        if (!resourceRoot.isEmpty()) {
            return QDir(resourceRoot);
        }
        directory.cdUp();
    }

    const QString currentResourceRoot =
        resourceRootPath(QDir::current());
    if (!currentResourceRoot.isEmpty()) {
        return QDir(currentResourceRoot);
    }

    QDir installDir(QCoreApplication::applicationDirPath());
    if (installDir.cdUp()
        && installDir.cd(QStringLiteral("share"))
        && installDir.cd(QStringLiteral("mdsscope"))
        && installDir.exists(QStringLiteral("environment"))) {
        return installDir;
    }
    return QDir(QCoreApplication::applicationDirPath());
}

bool desktopFileIsInstalled(const QString& desktopFileName)
{
    const QString fileName =
        desktopFileName.endsWith(QStringLiteral(".desktop"))
            ? desktopFileName
            : desktopFileName + QStringLiteral(".desktop");
    QStringList dataDirs;
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    dataDirs.push_back(
        dataHome.isEmpty()
            ? QDir::home().filePath(QStringLiteral(".local/share"))
            : dataHome);

    const QString dataDirsEnvironment =
        qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirsEnvironment.isEmpty()) {
        dataDirs << QStringLiteral("/usr/local/share")
                 << QStringLiteral("/usr/share");
    } else {
        dataDirs +=
            dataDirsEnvironment.split(':', Qt::SkipEmptyParts);
    }

    for (const QString& directory : std::as_const(dataDirs)) {
        const QString applicationsDir =
            QDir(directory).filePath(QStringLiteral("applications"));
        if (QFileInfo::exists(
                QDir(applicationsDir).filePath(fileName))) {
            return true;
        }
    }
    return false;
}

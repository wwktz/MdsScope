// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "user_preferences.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <utility>

namespace {
constexpr int kRecentItemLimit = 10;

QSettings settingsFor(const QString& path)
{
    return QSettings(path, QSettings::IniFormat);
}
}

UserPreferences::UserPreferences(QString settingsPath)
    : settingsPath_(std::move(settingsPath))
{
}

QString UserPreferences::exportBasePath(const QString& fallback) const
{
    return settingsFor(settingsPath_)
        .value(QStringLiteral("export/base_dir"), fallback)
        .toString();
}

void UserPreferences::setExportBasePath(const QString& path)
{
    settingsFor(settingsPath_).setValue(QStringLiteral("export/base_dir"), path);
}

QString UserPreferences::exportFormat() const
{
    return settingsFor(settingsPath_)
        .value(QStringLiteral("export/format"), QStringLiteral("text"))
        .toString();
}

void UserPreferences::setExportFormat(const QString& format)
{
    settingsFor(settingsPath_).setValue(QStringLiteral("export/format"), format);
}

DataReadMode UserPreferences::defaultReadMode() const
{
    const int stored =
        settingsFor(settingsPath_)
            .value(QStringLiteral("rate/default_mode"),
                   static_cast<int>(DataReadMode::Thin))
            .toInt();
    switch (static_cast<DataReadMode>(stored)) {
    case DataReadMode::Thin:
    case DataReadMode::Medium:
    case DataReadMode::Full:
        return static_cast<DataReadMode>(stored);
    }
    return DataReadMode::Thin;
}

void UserPreferences::setDefaultReadMode(DataReadMode mode)
{
    settingsFor(settingsPath_)
        .setValue(QStringLiteral("rate/default_mode"), static_cast<int>(mode));
}

QString UserPreferences::rememberedFileDialogDir(
    const QString& fallback) const
{
    const QString savedPath =
        settingsFor(settingsPath_)
            .value(QStringLiteral("files/last_dir"))
            .toString()
            .trimmed();
    return !savedPath.isEmpty() && QDir(savedPath).exists()
               ? QDir(savedPath).absolutePath()
               : fallback;
}

void UserPreferences::rememberFileDialogDir(const QString& path)
{
    const QFileInfo info(path);
    const QString dirPath =
        info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (dirPath.isEmpty()) {
        return;
    }
    settingsFor(settingsPath_)
        .setValue(QStringLiteral("files/last_dir"),
                  QDir(dirPath).absolutePath());
}

QString UserPreferences::openFileFilter(const QString& fallback) const
{
    return settingsFor(settingsPath_)
        .value(QStringLiteral("files/open_filter"), fallback)
        .toString();
}

void UserPreferences::setOpenFileFilter(const QString& filter)
{
    settingsFor(settingsPath_)
        .setValue(QStringLiteral("files/open_filter"), filter);
}

QStringList UserPreferences::recentEnvironmentFiles() const
{
    const QStringList files =
        settingsFor(settingsPath_)
            .value(QStringLiteral("files/recent"))
            .toStringList();
    QStringList cleaned;
    for (const QString& file : files) {
        const QString path = QFileInfo(file).absoluteFilePath();
        if (!path.isEmpty()
            && QFileInfo::exists(path)
            && !cleaned.contains(path)) {
            cleaned.push_back(path);
        }
        if (cleaned.size() >= kRecentItemLimit) {
            break;
        }
    }
    return cleaned;
}

void UserPreferences::rememberRecentEnvironmentFile(const QString& path)
{
    const QString filePath = QFileInfo(path).absoluteFilePath();
    if (filePath.isEmpty()) {
        return;
    }
    QStringList files = recentEnvironmentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > kRecentItemLimit) {
        files.removeLast();
    }
    settingsFor(settingsPath_)
        .setValue(QStringLiteral("files/recent"), files);
}

void UserPreferences::clearRecentEnvironmentFiles()
{
    settingsFor(settingsPath_).remove(QStringLiteral("files/recent"));
}

QStringList UserPreferences::recentShotExpressions() const
{
    const QStringList shots =
        settingsFor(settingsPath_)
            .value(QStringLiteral("shot/recent"))
            .toStringList();
    QStringList cleaned;
    for (const QString& shot : shots) {
        const QString value = shot.trimmed();
        if (!value.isEmpty() && !cleaned.contains(value)) {
            cleaned.push_back(value);
        }
        if (cleaned.size() >= kRecentItemLimit) {
            break;
        }
    }
    return cleaned;
}

void UserPreferences::rememberShotExpression(const QString& shot)
{
    const QString value = shot.trimmed();
    if (value.isEmpty()) {
        return;
    }
    QStringList shots = recentShotExpressions();
    shots.removeAll(value);
    shots.prepend(value);
    while (shots.size() > kRecentItemLimit) {
        shots.removeLast();
    }
    settingsFor(settingsPath_)
        .setValue(QStringLiteral("shot/recent"), shots);
}

QVector<InternalWebBookmark> UserPreferences::webBookmarks() const
{
    QSettings settings = settingsFor(settingsPath_);
    QVector<InternalWebBookmark> bookmarks;
    const int count =
        settings.beginReadArray(QStringLiteral("web/bookmarks"));
    bookmarks.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        bookmarks.push_back(
            {settings.value(QStringLiteral("alias")).toString(),
             settings.value(QStringLiteral("url")).toString()});
    }
    settings.endArray();

    if (bookmarks.isEmpty()) {
        const QStringList legacy =
            settings.value(QStringLiteral("web/urls")).toStringList();
        bookmarks.reserve(legacy.size());
        for (const QString& url : legacy) {
            bookmarks.push_back({{}, url});
        }
    }
    return bookmarks;
}

void UserPreferences::setWebBookmarks(
    const QVector<InternalWebBookmark>& bookmarks)
{
    QSettings settings = settingsFor(settingsPath_);
    settings.remove(QStringLiteral("web/bookmarks"));
    settings.beginWriteArray(QStringLiteral("web/bookmarks"),
                             static_cast<int>(bookmarks.size()));
    for (int i = 0; i < bookmarks.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("alias"), bookmarks.at(i).alias);
        settings.setValue(QStringLiteral("url"), bookmarks.at(i).url);
    }
    settings.endArray();
    settings.remove(QStringLiteral("web/urls"));
}

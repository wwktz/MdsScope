// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

// Owns persistence and normalization of user-level MainWindow preferences.
// UI code decides when to update menus and widgets; this class only stores and
// retrieves durable state.
class UserPreferences final {
public:
    explicit UserPreferences(QString settingsPath);

    QString exportBasePath(const QString& fallback) const;
    void setExportBasePath(const QString& path);
    QString exportFormat() const;
    void setExportFormat(const QString& format);

    DataReadMode defaultReadMode() const;
    void setDefaultReadMode(DataReadMode mode);

    QString rememberedFileDialogDir(const QString& fallback) const;
    void rememberFileDialogDir(const QString& path);
    QString openFileFilter(const QString& fallback) const;
    void setOpenFileFilter(const QString& filter);

    QStringList recentEnvironmentFiles() const;
    void rememberRecentEnvironmentFile(const QString& path);
    void clearRecentEnvironmentFiles();

    QStringList recentShotExpressions() const;
    void rememberShotExpression(const QString& shot);

    QVector<InternalWebBookmark> webBookmarks() const;
    void setWebBookmarks(const QVector<InternalWebBookmark>& bookmarks);

private:
    QString settingsPath_;
};

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ssh_settings.hpp"

#include <QHash>
#include <QJsonValue>
#include <QString>

struct ApiLoginResult {
    bool ok = false;
    QString token;
    QString error;
};

struct CachedAuth {
    QString userName;
    QString password;
    QString token;
    SshSettings ssh;
};

QString apiUrlPath(const QString& rootPath);
QString readApiUrl(const QString& rootPath);
QHash<QString, QString> readApiSettings(const QString& rootPath);
bool tokenExpiresSoon(const QString& token);
bool loadCachedAuth(CachedAuth* auth);
bool saveCachedAuth(
    const CachedAuth& auth,
    QString* error = nullptr);
bool clearCachedApiAuth();
ApiLoginResult requestApiToken(const QString& api,
                               const QString& charset,
                               const QString& userName,
                               const QString& password);
QString firstShotLikeText(const QString& text);
QString firstShotFromJsonValue(const QJsonValue& value);

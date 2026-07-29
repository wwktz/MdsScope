// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auth_payload.hpp"

#include "core/api_auth.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

QByteArray serializeAuthPayload(const CachedAuth& auth)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("userName"), auth.userName);
    root.insert(QStringLiteral("password"), auth.password);
    root.insert(QStringLiteral("token"), auth.token);

    QJsonObject ssh;
    ssh.insert(
        QStringLiteral("mode"),
        static_cast<int>(auth.ssh.mode));
    ssh.insert(QStringLiteral("host"), auth.ssh.host);
    ssh.insert(QStringLiteral("port"), auth.ssh.port);
    ssh.insert(QStringLiteral("user"), auth.ssh.user);
    ssh.insert(
        QStringLiteral("password"),
        auth.ssh.password);
    ssh.insert(
        QStringLiteral("identityFile"),
        auth.ssh.identityFile);
    root.insert(QStringLiteral("ssh"), ssh);
    return QJsonDocument(root).toJson(
        QJsonDocument::Compact);
}

bool deserializeAuthPayload(
    const QByteArray& payload,
    CachedAuth* auth)
{
    if (!auth) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue versionValue =
        root.value(QStringLiteral("version"));
    if (!versionValue.isUndefined()
        && versionValue.toInt() != 2) {
        return false;
    }

    CachedAuth parsed;
    parsed.userName =
        root.value(QStringLiteral("userName")).toString();
    parsed.password =
        root.value(QStringLiteral("password")).toString();
    parsed.token =
        root.value(QStringLiteral("token")).toString();

    const QJsonObject ssh =
        root.value(QStringLiteral("ssh")).toObject();
    parsed.ssh.mode = static_cast<SshMode>(
        std::clamp(
            ssh.value(QStringLiteral("mode")).toInt(),
            static_cast<int>(SshMode::Disabled),
            static_cast<int>(SshMode::Always)));
    parsed.ssh.host =
        ssh.value(QStringLiteral("host")).toString();
    parsed.ssh.port = std::clamp(
        ssh.value(QStringLiteral("port")).toInt(22),
        1,
        65535);
    parsed.ssh.user =
        ssh.value(QStringLiteral("user")).toString();
    parsed.ssh.password =
        ssh.value(QStringLiteral("password")).toString();
    parsed.ssh.identityFile =
        ssh.value(QStringLiteral("identityFile")).toString();

    const bool hasContent =
        !parsed.token.isEmpty()
        || !parsed.userName.isEmpty()
        || !parsed.password.isEmpty()
        || !parsed.ssh.host.isEmpty()
        || !parsed.ssh.user.isEmpty()
        || !parsed.ssh.password.isEmpty()
        || !parsed.ssh.identityFile.isEmpty();
    if (!hasContent) {
        return false;
    }
    *auth = std::move(parsed);
    return true;
}

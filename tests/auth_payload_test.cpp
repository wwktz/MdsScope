// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/api_auth.hpp"
#include "core/internal/auth_payload.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>

namespace {
bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}
}

int main()
{
    CachedAuth original;
    original.userName = QStringLiteral("api-user");
    original.password = QStringLiteral("api-password");
    original.token = QStringLiteral("api-token");
    original.ssh.mode = SshMode::Always;
    original.ssh.host = QStringLiteral("192.0.2.5");
    original.ssh.port = 2222;
    original.ssh.user = QStringLiteral("ssh-user");
    original.ssh.password = QStringLiteral("ssh-password");
    original.ssh.identityFile =
        QStringLiteral("/tmp/id_test");

    CachedAuth decoded;
    if (!require(
            deserializeAuthPayload(
                serializeAuthPayload(original), &decoded),
            "round trip failed")
        || !require(
            decoded.userName == original.userName,
            "API username changed")
        || !require(
            decoded.password == original.password,
            "API password changed")
        || !require(
            decoded.token == original.token,
            "API token changed")
        || !require(
            decoded.ssh.mode == original.ssh.mode,
            "SSH mode changed")
        || !require(
            decoded.ssh.host == original.ssh.host,
            "SSH host changed")
        || !require(
            decoded.ssh.port == original.ssh.port,
            "SSH port changed")
        || !require(
            decoded.ssh.user == original.ssh.user,
            "SSH username changed")
        || !require(
            decoded.ssh.password
                == original.ssh.password,
            "SSH password changed")
        || !require(
            decoded.ssh.identityFile
                == original.ssh.identityFile,
            "SSH identity path changed")) {
        return 1;
    }

    QJsonObject legacy;
    legacy.insert(QStringLiteral("userName"), "legacy-user");
    QJsonObject legacySsh;
    legacySsh.insert(QStringLiteral("mode"), 99);
    legacySsh.insert(QStringLiteral("host"), "legacy-host");
    legacySsh.insert(QStringLiteral("port"), 99999);
    legacy.insert(QStringLiteral("ssh"), legacySsh);
    if (!require(
            deserializeAuthPayload(
                QJsonDocument(legacy).toJson(
                    QJsonDocument::Compact),
                &decoded),
            "legacy payload was rejected")
        || !require(
            decoded.ssh.mode == SshMode::Always,
            "SSH mode was not clamped")
        || !require(
            decoded.ssh.port == 65535,
            "SSH port was not clamped")) {
        return 1;
    }

    QJsonObject unsupported = legacy;
    unsupported.insert(QStringLiteral("version"), 3);
    if (!require(
            !deserializeAuthPayload(
                QJsonDocument(unsupported).toJson(
                    QJsonDocument::Compact),
                &decoded),
            "unsupported payload version was accepted")
        || !require(
            !deserializeAuthPayload(
                QByteArrayLiteral("{not-json}"), &decoded),
            "invalid JSON was accepted")) {
        return 1;
    }
    return 0;
}

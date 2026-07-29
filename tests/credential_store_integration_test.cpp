// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/credential_store.hpp"

#include <QByteArray>
#include <QString>

#include <iostream>

namespace {
bool fail(const QString& message)
{
    std::cerr << message.toStdString() << '\n';
    return false;
}

bool require(bool condition, const QString& message)
{
    return condition || fail(message);
}
}

int main()
{
    const QString profile =
        qEnvironmentVariable("MDSSCOPE_CREDENTIAL_PROFILE");
    if (!require(
            profile.startsWith(QStringLiteral("mdsscope-test-")),
            QStringLiteral(
                "Refusing to modify a non-test credential profile"))) {
        return 1;
    }

    QString error;
    if (!require(
            removeNativeCredential(&error),
            QStringLiteral("Initial cleanup failed: %1")
                .arg(error))) {
        return 1;
    }

    QByteArray payload;
    if (!require(
            readNativeCredential(&payload, &error)
                == CredentialStoreReadResult::NotFound,
            QStringLiteral("Test profile was not empty: %1")
                .arg(error))) {
        return 1;
    }

    const QByteArray largePayload(
        6000, static_cast<char>('A'));
    if (!require(
            writeNativeCredential(largePayload, &error),
            QStringLiteral("Large write failed: %1")
                .arg(error))
        || !require(
            readNativeCredential(&payload, &error)
                == CredentialStoreReadResult::Found,
            QStringLiteral("Large read failed: %1")
                .arg(error))
        || !require(
            payload == largePayload,
            QStringLiteral("Large payload changed"))) {
        removeNativeCredential();
        return 1;
    }

    const QByteArray replacement =
        QByteArrayLiteral("replacement-credential");
    if (!require(
            writeNativeCredential(replacement, &error),
            QStringLiteral("Replacement write failed: %1")
                .arg(error))
        || !require(
            readNativeCredential(&payload, &error)
                == CredentialStoreReadResult::Found,
            QStringLiteral("Replacement read failed: %1")
                .arg(error))
        || !require(
            payload == replacement,
            QStringLiteral("Replacement payload changed"))
        || !require(
            removeNativeCredential(&error),
            QStringLiteral("Final cleanup failed: %1")
                .arg(error))
        || !require(
            readNativeCredential(&payload, &error)
                == CredentialStoreReadResult::NotFound,
            QStringLiteral("Removed credential still exists: %1")
                .arg(error))) {
        removeNativeCredential();
        return 1;
    }
    return 0;
}

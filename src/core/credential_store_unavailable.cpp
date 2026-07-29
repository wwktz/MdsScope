// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "credential_store.hpp"

QString credentialStoreName()
{
    return QStringLiteral("system credential store");
}

CredentialStoreReadResult readNativeCredential(
    QByteArray*,
    QString* error)
{
    if (error) {
        *error = QStringLiteral(
            "No native credential backend is available");
    }
    return CredentialStoreReadResult::Unavailable;
}

bool writeNativeCredential(
    const QByteArray&,
    QString* error)
{
    if (error) {
        *error = QStringLiteral(
            "No native credential backend is available");
    }
    return false;
}

bool removeNativeCredential(QString* error)
{
    if (error) {
        *error = QStringLiteral(
            "No native credential backend is available");
    }
    return false;
}

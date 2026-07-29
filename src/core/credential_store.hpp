// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

enum class CredentialStoreReadResult {
    Found,
    NotFound,
    Unavailable,
    Error,
};

QString credentialStoreName();
CredentialStoreReadResult readNativeCredential(
    QByteArray* payload,
    QString* error = nullptr);
bool writeNativeCredential(
    const QByteArray& payload,
    QString* error = nullptr);
bool removeNativeCredential(QString* error = nullptr);

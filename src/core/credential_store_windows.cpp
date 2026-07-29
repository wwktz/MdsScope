// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "credential_store.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace {
constexpr qsizetype kChunkSize = 2400;
constexpr int kMaximumChunkCount = 64;

QString profileName()
{
    const QString profile =
        qEnvironmentVariable("MDSSCOPE_CREDENTIAL_PROFILE")
            .trimmed();
    if (profile.isEmpty()) {
        return QStringLiteral("default");
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(
            profile.toUtf8(),
            QCryptographicHash::Sha256)
            .toHex());
}

QString targetBase()
{
    return QStringLiteral("MdsScope/Credentials/")
           + profileName();
}

QString manifestTarget()
{
    return targetBase() + QStringLiteral("/manifest");
}

QString chunkTarget(
    const QString& generation,
    int index)
{
    return targetBase()
           + QStringLiteral("/chunk/")
           + generation
           + QLatin1Char('/')
           + QString::number(index);
}

QString windowsError(
    const QString& operation,
    DWORD code)
{
    return QStringLiteral("%1 failed (Windows error %2)")
        .arg(operation)
        .arg(static_cast<qulonglong>(code));
}

CredentialStoreReadResult readRaw(
    const QString& target,
    QByteArray* payload,
    QString* error)
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(
            reinterpret_cast<LPCWSTR>(target.utf16()),
            CRED_TYPE_GENERIC,
            0,
            &credential)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            return CredentialStoreReadResult::NotFound;
        }
        if (error) {
            *error = windowsError(
                QStringLiteral("Credential Manager lookup"),
                code);
        }
        return CredentialStoreReadResult::Error;
    }

    const DWORD size = credential->CredentialBlobSize;
    if (size > static_cast<DWORD>(
                   std::numeric_limits<int>::max())) {
        CredFree(credential);
        if (error) {
            *error = QStringLiteral(
                "Credential Manager value is too large");
        }
        return CredentialStoreReadResult::Error;
    }
    *payload = QByteArray(
        reinterpret_cast<const char*>(
            credential->CredentialBlob),
        static_cast<qsizetype>(size));
    CredFree(credential);
    return CredentialStoreReadResult::Found;
}

bool writeRaw(
    const QString& target,
    const QByteArray& payload,
    QString* error)
{
    if (payload.size() > kChunkSize) {
        if (error) {
            *error = QStringLiteral(
                "Credential Manager chunk is too large");
        }
        return false;
    }

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(
        reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize =
        static_cast<DWORD>(payload.size());
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(
            const_cast<char*>(payload.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName =
        const_cast<LPWSTR>(L"MdsScope");
    if (!CredWriteW(&credential, 0)) {
        if (error) {
            *error = windowsError(
                QStringLiteral("Credential Manager save"),
                GetLastError());
        }
        return false;
    }
    return true;
}

bool deleteRaw(const QString& target, QString* error)
{
    if (CredDeleteW(
            reinterpret_cast<LPCWSTR>(target.utf16()),
            CRED_TYPE_GENERIC,
            0)) {
        return true;
    }
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) {
        return true;
    }
    if (error) {
        *error = windowsError(
            QStringLiteral("Credential Manager removal"),
            code);
    }
    return false;
}

bool parseManifest(
    const QByteArray& payload,
    QString* generation,
    int* count,
    QByteArray* digest)
{
    const QJsonDocument document =
        QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt()
        != 1) {
        return false;
    }
    const QString parsedGeneration =
        root.value(QStringLiteral("generation")).toString();
    const int parsedCount =
        root.value(QStringLiteral("chunks")).toInt();
    const QByteArray parsedDigest =
        QByteArray::fromHex(
            root.value(QStringLiteral("sha256"))
                .toString()
                .toLatin1());
    if (parsedGeneration.isEmpty()
        || parsedGeneration.contains(QLatin1Char('/'))
        || parsedCount < 1
        || parsedCount > kMaximumChunkCount
        || parsedDigest.size()
               != QCryptographicHash::hashLength(
                   QCryptographicHash::Sha256)) {
        return false;
    }
    *generation = parsedGeneration;
    *count = parsedCount;
    *digest = parsedDigest;
    return true;
}

void removeGeneration(
    const QString& generation,
    int count)
{
    for (int index = 0; index < count; ++index) {
        deleteRaw(chunkTarget(generation, index), nullptr);
    }
}

bool removeProfileCredentials(QString* error)
{
    const QString filter =
        targetBase() + QStringLiteral("/*");
    DWORD count = 0;
    PCREDENTIALW* credentials = nullptr;
    if (!CredEnumerateW(
            reinterpret_cast<LPCWSTR>(filter.utf16()),
            0,
            &count,
            &credentials)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            return true;
        }
        if (error) {
            *error = windowsError(
                QStringLiteral(
                    "Credential Manager enumeration"),
                code);
        }
        return false;
    }

    bool removed = true;
    for (DWORD index = 0; index < count; ++index) {
        const PCREDENTIALW credential = credentials[index];
        if (credential->Type == CRED_TYPE_GENERIC
            && !CredDeleteW(
                credential->TargetName,
                CRED_TYPE_GENERIC,
                0)) {
            const DWORD code = GetLastError();
            if (code != ERROR_NOT_FOUND) {
                removed = false;
                if (error && error->isEmpty()) {
                    *error = windowsError(
                        QStringLiteral(
                            "Credential Manager removal"),
                        code);
                }
            }
        }
    }
    CredFree(credentials);
    return removed;
}
}

QString credentialStoreName()
{
    return QStringLiteral("Windows Credential Manager");
}

CredentialStoreReadResult readNativeCredential(
    QByteArray* payload,
    QString* error)
{
    if (!payload) {
        if (error) {
            *error = QStringLiteral("Missing output buffer");
        }
        return CredentialStoreReadResult::Error;
    }

    QByteArray manifest;
    const CredentialStoreReadResult manifestResult =
        readRaw(manifestTarget(), &manifest, error);
    if (manifestResult != CredentialStoreReadResult::Found) {
        return manifestResult;
    }

    QString generation;
    int count = 0;
    QByteArray expectedDigest;
    if (!parseManifest(
            manifest,
            &generation,
            &count,
            &expectedDigest)) {
        if (error) {
            *error = QStringLiteral(
                "Credential Manager manifest is invalid");
        }
        return CredentialStoreReadResult::Error;
    }

    QByteArray combined;
    for (int index = 0; index < count; ++index) {
        QByteArray chunk;
        const CredentialStoreReadResult chunkResult =
            readRaw(
                chunkTarget(generation, index),
                &chunk,
                error);
        if (chunkResult != CredentialStoreReadResult::Found) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral(
                    "Credential Manager data is incomplete");
            }
            return CredentialStoreReadResult::Error;
        }
        combined += chunk;
    }
    if (QCryptographicHash::hash(
            combined, QCryptographicHash::Sha256)
        != expectedDigest) {
        if (error) {
            *error = QStringLiteral(
                "Credential Manager data failed validation");
        }
        return CredentialStoreReadResult::Error;
    }
    *payload = combined;
    return CredentialStoreReadResult::Found;
}

bool writeNativeCredential(
    const QByteArray& payload,
    QString* error)
{
    if (payload.isEmpty()) {
        if (error) {
            *error =
                QStringLiteral("Cannot store an empty credential");
        }
        return false;
    }
    const qsizetype countValue =
        (payload.size() + kChunkSize - 1) / kChunkSize;
    if (countValue > kMaximumChunkCount) {
        if (error) {
            *error =
                QStringLiteral("Credential data is too large");
        }
        return false;
    }
    const int count = static_cast<int>(countValue);
    const QString generation =
        QUuid::createUuid().toString(
            QUuid::WithoutBraces);

    int written = 0;
    for (int index = 0; index < count; ++index) {
        const qsizetype offset =
            static_cast<qsizetype>(index) * kChunkSize;
        const QByteArray chunk =
            payload.mid(offset, kChunkSize);
        if (!writeRaw(
                chunkTarget(generation, index),
                chunk,
                error)) {
            removeGeneration(generation, written);
            return false;
        }
        ++written;
    }

    QString previousGeneration;
    int previousCount = 0;
    QByteArray previousDigest;
    QByteArray previousManifest;
    if (readRaw(
            manifestTarget(),
            &previousManifest,
            nullptr)
        == CredentialStoreReadResult::Found) {
        parseManifest(
            previousManifest,
            &previousGeneration,
            &previousCount,
            &previousDigest);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(
        QStringLiteral("generation"), generation);
    manifest.insert(QStringLiteral("chunks"), count);
    manifest.insert(
        QStringLiteral("sha256"),
        QString::fromLatin1(
            QCryptographicHash::hash(
                payload, QCryptographicHash::Sha256)
                .toHex()));
    const QByteArray manifestPayload =
        QJsonDocument(manifest).toJson(
            QJsonDocument::Compact);
    if (!writeRaw(
            manifestTarget(), manifestPayload, error)) {
        removeGeneration(generation, count);
        return false;
    }

    if (!previousGeneration.isEmpty()
        && previousGeneration != generation) {
        removeGeneration(
            previousGeneration, previousCount);
    }
    return true;
}

bool removeNativeCredential(QString* error)
{
    return removeProfileCredentials(error);
}

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "legacy_auth_store.hpp"

#include "core/app_paths.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSysInfo>
#include <QtEndian>

#include <algorithm>

namespace {
QByteArray localAuthKey()
{
    QByteArray material;
#ifdef Q_OS_WIN
    material += qgetenv("COMPUTERNAME");
    material += '|';
    material += qgetenv("USERNAME");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    material += QSysInfo::machineUniqueId();
    material += '|';
    material += QSysInfo::machineHostName().toUtf8();
    material += '|';
    material += qgetenv("USER");
#else
    QFile machineId(QStringLiteral("/etc/machine-id"));
    if (machineId.open(QIODevice::ReadOnly | QIODevice::Text)) {
        material += machineId.readAll().trimmed();
    }
    material += '|';
    material += qgetenv("USER");
#endif
    material += '|';
    material += QDir::homePath().toUtf8();
    material += "|MdsScope EAST auth cache";
    return QCryptographicHash::hash(
        material, QCryptographicHash::Sha256);
}

QByteArray cryptAuthPayload(
    const QByteArray& data,
    const QByteArray& salt)
{
    const QByteArray key = localAuthKey();
    QByteArray out;
    out.resize(data.size());
    qsizetype offset = 0;
    quint64 counter = 0;
    while (offset < data.size()) {
        QByteArray counterBytes;
        counterBytes.resize(
            static_cast<qsizetype>(sizeof(counter)));
        qToLittleEndian(
            counter,
            reinterpret_cast<uchar*>(counterBytes.data()));
        const QByteArray stream =
            QCryptographicHash::hash(
                key + salt + counterBytes,
                QCryptographicHash::Sha256);
        const qsizetype count =
            std::min(stream.size(), data.size() - offset);
        for (qsizetype index = 0; index < count; ++index) {
            out[offset + index] =
                data[offset + index] ^ stream[index];
        }
        offset += count;
        ++counter;
    }
    return out;
}
}

QString legacyAuthCachePath()
{
    QDir().mkpath(appCacheDir());
    return QDir(appCacheDir()).filePath(
        QStringLiteral("auth.cache"));
}

bool legacyAuthCacheExists()
{
    return QFileInfo::exists(legacyAuthCachePath());
}

bool readLegacyAuthPayload(QByteArray* payload)
{
    if (!payload) {
        return false;
    }
    QFile file(legacyAuthCachePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QJsonObject wrapper =
        QJsonDocument::fromJson(file.readAll()).object();
    if (wrapper.value(QStringLiteral("version")).toInt() != 1) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(
        wrapper.value(QStringLiteral("salt"))
            .toString()
            .toUtf8());
    const QByteArray encrypted = QByteArray::fromBase64(
        wrapper.value(QStringLiteral("payload"))
            .toString()
            .toUtf8());
    if (salt.isEmpty() || encrypted.isEmpty()) {
        return false;
    }
    *payload = cryptAuthPayload(encrypted, salt);
    return !payload->isEmpty();
}

bool writeLegacyAuthPayload(const QByteArray& payload)
{
    QByteArray salt;
    salt.resize(16);
    for (char& ch : salt) {
        ch = static_cast<char>(
            QRandomGenerator::global()->generate() & 0xffU);
    }

    QJsonObject wrapper;
    wrapper.insert(QStringLiteral("version"), 1);
    wrapper.insert(
        QStringLiteral("salt"),
        QString::fromLatin1(salt.toBase64()));
    wrapper.insert(
        QStringLiteral("payload"),
        QString::fromLatin1(
            cryptAuthPayload(payload, salt).toBase64()));

    QSaveFile file(legacyAuthCachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents =
        QJsonDocument(wrapper).toJson(
            QJsonDocument::Compact)
        + '\n';
    if (file.write(contents) != contents.size()
        || !file.setPermissions(
            QFileDevice::ReadOwner
            | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool removeLegacyAuthPayload()
{
    const QString path = legacyAuthCachePath();
    return !QFileInfo::exists(path) || QFile::remove(path);
}

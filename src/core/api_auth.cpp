// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "api_auth.hpp"
#include "credential_store.hpp"
#include "internal/auth_payload.hpp"
#include "internal/legacy_auth_store.hpp"
#include "text_utils.hpp"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

QHash<QString, QString> defaultApiProperties()
{
    return {
        {"Authorization_Prefix", "Bearer"},
        {"Charset", "UTF-8"},
    };
}

QString apiUrlPath(const QString& rootPath)
{
    const QDir root(rootPath);
    const QString installedPath = root.filePath("APIurl");
    if (QFileInfo::exists(installedPath)) {
        return installedPath;
    }
    return root.filePath("resources/APIurl");
}

QString readApiUrl(const QString& rootPath)
{
    QFile file(apiUrlPath(rootPath));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) {
                return javaUnescape(line);
            }
        }
    }

    return {};
}

QHash<QString, QString> readApiSettings(const QString& rootPath)
{
    auto properties = defaultApiProperties();
    properties.insert("ApiUrl", readApiUrl(rootPath));

    CachedAuth auth;
    if (loadCachedAuth(&auth)) {
        properties.insert("Token", auth.token);
    }

    return properties;
}

bool tokenExpiresSoon(const QString& token)
{
    const QStringList parts = token.split('.');
    if (parts.size() < 2) {
        return false;
    }

    QByteArray payload = parts.at(1).toUtf8();
    payload.replace('-', '+');
    payload.replace('_', '/');
    while (payload.size() % 4 != 0) {
        payload.append('=');
    }

    const QJsonObject json = QJsonDocument::fromJson(QByteArray::fromBase64(payload)).object();
    const QJsonValue expValue = json.value("exp");
    if (!expValue.isDouble()) {
        return false;
    }

    const qint64 exp = static_cast<qint64>(expValue.toDouble());
    return exp <= QDateTime::currentSecsSinceEpoch() + 300;
}

bool loadCachedAuth(CachedAuth* auth)
{
    if (!auth) {
        return false;
    }

    QByteArray payload;
    if (readNativeCredential(&payload)
            == CredentialStoreReadResult::Found
        && deserializeAuthPayload(payload, auth)) {
        return true;
    }

    if (!readLegacyAuthPayload(&payload)
        || !deserializeAuthPayload(payload, auth)) {
        return false;
    }

    if (writeNativeCredential(
            serializeAuthPayload(*auth))) {
        removeLegacyAuthPayload();
    }
    return true;
}

bool saveCachedAuth(
    const CachedAuth& auth,
    QString* error)
{
    const QByteArray payload = serializeAuthPayload(auth);
    QString nativeError;
    if (writeNativeCredential(payload, &nativeError)) {
        removeLegacyAuthPayload();
        return true;
    }
    if (legacyAuthCacheExists()
        && writeLegacyAuthPayload(payload)) {
        return true;
    }
    if (error) {
        *error = nativeError.isEmpty()
                     ? QStringLiteral(
                           "The system credential store "
                           "rejected the update")
                     : nativeError;
    }
    return false;
}

bool clearCachedApiAuth()
{
    CachedAuth auth;
    if (!loadCachedAuth(&auth)) {
        QByteArray payload;
        const CredentialStoreReadResult nativeResult =
            readNativeCredential(&payload);
        const bool nativeRemoved =
            nativeResult == CredentialStoreReadResult::NotFound
            || (nativeResult
                    == CredentialStoreReadResult::Found
                && removeNativeCredential());
        const bool legacyRemoved =
            removeLegacyAuthPayload();
        return nativeRemoved && legacyRemoved;
    }
    auth.userName.clear();
    auth.password.clear();
    auth.token.clear();
    if (auth.ssh.host.isEmpty()
        && auth.ssh.user.isEmpty()
        && auth.ssh.password.isEmpty()
        && auth.ssh.identityFile.isEmpty()) {
        const bool nativeRemoved =
            removeNativeCredential();
        const bool legacyRemoved =
            removeLegacyAuthPayload();
        return nativeRemoved && legacyRemoved;
    }
    return saveCachedAuth(auth);
}

ApiLoginResult requestApiToken(const QString& api,
                               const QString& charset,
                               const QString& userName,
                               const QString& password)
{
    ApiLoginResult result;
    if (api.trimmed().isEmpty()) {
        result.error = QStringLiteral("Missing API URL.");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api.trimmed() + "/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(5000);

    QJsonObject payload;
    payload.insert("userName", userName.trimmed());
    payload.insert("password", password);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        result.error = QStringLiteral("Login request timed out.");
        return result;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        result.error = QStringLiteral("Login request failed.");
        return result;
    }

    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const bool ok = json.value("code").toString() == "20000" || json.value("code").toInt() == 20000;
    const QString token = json.value("data").toObject().value("token").toString();
    if (!ok || token.isEmpty()) {
        result.error = QStringLiteral("Invalid username or password.");
        return result;
    }

    result.ok = true;
    result.token = token;
    return result;
}

QString firstShotLikeText(const QString& text)
{
    static const QRegularExpression re(R"(\b\d{4,8}\b)");
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(0) : QString();
}

QString firstShotFromJsonValue(const QJsonValue& value)
{
    if (value.isDouble()) {
        return QString::number(static_cast<qint64>(value.toDouble()));
    }
    if (value.isString()) {
        return firstShotLikeText(value.toString());
    }
    if (value.isArray()) {
        const auto arr = value.toArray();
        for (const QJsonValue& item : arr) {
            const QString shot = firstShotFromJsonValue(item);
            if (!shot.isEmpty()) {
                return shot;
            }
        }
    }
    if (value.isObject()) {
        const auto obj = value.toObject();
        for (const char* key : {"shot", "shotNo", "shotno", "treeShot", "value", "name"}) {
            const QString shot = firstShotFromJsonValue(obj.value(QString::fromLatin1(key)));
            if (!shot.isEmpty()) {
                return shot;
            }
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString shot = firstShotFromJsonValue(it.value());
            if (!shot.isEmpty()) {
                return shot;
            }
        }
    }
    return {};
}

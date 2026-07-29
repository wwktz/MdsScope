// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shot_metadata_client.hpp"

#include "api_auth.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

ShotMetadataClient::ShotMetadataClient(QString rootPath)
    : rootPath_(std::move(rootPath))
{
}

QString ShotMetadataClient::latestShot(const QString& apiOverride) const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api =
        apiOverride.trimmed().isEmpty()
            ? properties.value(QStringLiteral("ApiUrl"))
            : apiOverride.trimmed();
    const QString token = properties.value(QStringLiteral("Token"));
    const QString prefix =
        properties.value(QStringLiteral("Authorization_Prefix"),
                         properties.value(QStringLiteral("Init_Prefix"),
                                          QStringLiteral("Bearer")));
    const QString charset =
        properties.value(QStringLiteral("Charset"), QStringLiteral("UTF-8"));
    if (api.isEmpty() || token.isEmpty()) {
        return {};
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + QStringLiteral("/treeShot")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=") + charset);
    request.setRawHeader("Authorization", (prefix + ' ' + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(4000);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QByteArray("{}"));
    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout,
                     &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        QString shot = firstShotFromJsonValue(
            object.value(QStringLiteral("data")));
        if (!shot.isEmpty()) {
            return shot;
        }
        shot = firstShotFromJsonValue(object);
        if (!shot.isEmpty()) {
            return shot;
        }
    } else if (document.isArray()) {
        const QString shot = firstShotFromJsonValue(document.array());
        if (!shot.isEmpty()) {
            return shot;
        }
    }
    return firstShotLikeText(QString::fromUtf8(body));
}

bool ShotMetadataClient::loadSummary(const QString& shot,
                                     QString* ip,
                                     QString* pulse,
                                     QString* it,
                                     QString* time,
                                     const QString& apiOverride) const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api =
        apiOverride.trimmed().isEmpty()
            ? properties.value(QStringLiteral("ApiUrl"))
            : apiOverride.trimmed();
    const QString token = properties.value(QStringLiteral("Token"));
    const QString prefix =
        properties.value(QStringLiteral("Authorization_Prefix"),
                         properties.value(QStringLiteral("Init_Prefix"),
                                          QStringLiteral("Bearer")));
    const QString charset =
        properties.value(QStringLiteral("Charset"), QStringLiteral("UTF-8"));
    if (api.isEmpty() || token.isEmpty() || shot.trimmed().isEmpty()) {
        return false;
    }

    bool shotOk = false;
    const int shotNumber = shot.trimmed().toInt(&shotOk);
    if (!shotOk) {
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + QStringLiteral("/pcsEastTree")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=") + charset);
    request.setRawHeader("Authorization", (prefix + ' ' + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(2500);

    QJsonObject payload;
    payload.insert(QStringLiteral("treeshot"), shotNumber);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply =
        manager.post(request,
                     QJsonDocument(payload)
                         .toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout,
                     &loop, &QEventLoop::quit);
    timer.start(2500);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString code =
        root.value(QStringLiteral("code")).isString()
            ? root.value(QStringLiteral("code")).toString()
            : QString::number(
                  root.value(QStringLiteral("code")).toInt());
    if (code != QStringLiteral("20000")) {
        return false;
    }
    const QJsonObject data =
        root.value(QStringLiteral("data")).toObject();
    if (data.isEmpty()) {
        return false;
    }

    const auto scalarText = [](const QJsonValue& value) {
        if (value.isString()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(value.toDouble(), 'g', 8);
        }
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("true")
                                  : QStringLiteral("false");
        }
        return QString();
    };

    if (ip) {
        *ip = scalarText(data.value(QStringLiteral("pcrl01")));
    }
    if (pulse) {
        *pulse = scalarText(data.value(QStringLiteral("shot_len")));
    }
    if (it) {
        *it = scalarText(data.value(QStringLiteral("iv")));
    }
    if (time) {
        *time = scalarText(data.value(QStringLiteral("curr_time")));
    }
    return true;
}

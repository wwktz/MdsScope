// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libsecret/secret.h>

#include "credential_store.hpp"

#include <memory>

namespace {
const SecretSchema* credentialSchema()
{
    using SchemaOwner = std::unique_ptr<
        SecretSchema,
        decltype(&secret_schema_unref)>;
    static const SchemaOwner schema(
        secret_schema_new(
            "org.mdsscope.Credentials",
            SECRET_SCHEMA_NONE,
            "profile",
            SECRET_SCHEMA_ATTRIBUTE_STRING,
            nullptr),
        &secret_schema_unref);
    return schema.get();
}

const char* profileName()
{
    const QByteArray profile =
        qEnvironmentVariable("MDSSCOPE_CREDENTIAL_PROFILE")
            .trimmed()
            .toUtf8();
    static thread_local QByteArray currentProfile;
    currentProfile =
        profile.isEmpty() ? QByteArrayLiteral("default") : profile;
    return currentProfile.constData();
}

void setError(QString* destination, GError* error)
{
    if (destination) {
        *destination =
            error && error->message
                ? QString::fromUtf8(error->message)
                : QStringLiteral(
                      "Secret Service operation failed");
    }
    if (error) {
        g_error_free(error);
    }
}
}

QString credentialStoreName()
{
    return QStringLiteral("Linux Secret Service");
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

    GError* nativeError = nullptr;
    gchar* encoded = secret_password_lookup_sync(
        credentialSchema(),
        nullptr,
        &nativeError,
        "profile",
        profileName(),
        nullptr);
    if (nativeError) {
        setError(error, nativeError);
        return CredentialStoreReadResult::Unavailable;
    }
    if (!encoded) {
        return CredentialStoreReadResult::NotFound;
    }

    const auto decoded = QByteArray::fromBase64Encoding(
        QByteArray(encoded),
        QByteArray::AbortOnBase64DecodingErrors);
    secret_password_free(encoded);
    if (!decoded || decoded.decoded.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Stored Secret Service value is invalid");
        }
        return CredentialStoreReadResult::Error;
    }
    *payload = decoded.decoded;
    return CredentialStoreReadResult::Found;
}

bool writeNativeCredential(
    const QByteArray& payload,
    QString* error)
{
    const QByteArray encoded = payload.toBase64();
    GError* nativeError = nullptr;
    const gboolean stored = secret_password_store_sync(
        credentialSchema(),
        SECRET_COLLECTION_DEFAULT,
        "MdsScope credentials",
        encoded.constData(),
        nullptr,
        &nativeError,
        "profile",
        profileName(),
        nullptr);
    if (!stored) {
        setError(error, nativeError);
        return false;
    }
    return true;
}

bool removeNativeCredential(QString* error)
{
    GError* nativeError = nullptr;
    secret_password_clear_sync(
        credentialSchema(),
        nullptr,
        &nativeError,
        "profile",
        profileName(),
        nullptr);
    if (nativeError) {
        setError(error, nativeError);
        return false;
    }
    return true;
}

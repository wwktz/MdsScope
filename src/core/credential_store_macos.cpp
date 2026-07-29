// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "credential_store.hpp"

#include <QByteArray>

#include <Security/Security.h>

namespace {
class CfOwner {
public:
    explicit CfOwner(CFTypeRef value = nullptr)
        : value_(value)
    {
    }

    ~CfOwner()
    {
        if (value_) {
            CFRelease(value_);
        }
    }

    CfOwner(const CfOwner&) = delete;
    CfOwner& operator=(const CfOwner&) = delete;

    [[nodiscard]] CFTypeRef get() const
    {
        return value_;
    }

private:
    CFTypeRef value_;
};

CFStringRef createString(const QByteArray& value)
{
    return CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.constData()),
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false);
}

QByteArray profileName()
{
    const QByteArray profile =
        qEnvironmentVariable("MDSSCOPE_CREDENTIAL_PROFILE")
            .trimmed()
            .toUtf8();
    return profile.isEmpty()
               ? QByteArrayLiteral("default")
               : profile;
}

CFMutableDictionaryRef createBaseQuery()
{
    auto* query = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!query) {
        return nullptr;
    }

    const CfOwner service(
        createString(
            QByteArrayLiteral("org.mdsscope.credentials")));
    const CfOwner account(createString(profileName()));
    if (!service.get() || !account.get()) {
        CFRelease(query);
        return nullptr;
    }
    CFDictionarySetValue(
        query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(
        query, kSecAttrService, service.get());
    CFDictionarySetValue(
        query, kSecAttrAccount, account.get());
    return query;
}

void setStatusError(
    QString* error,
    OSStatus status,
    const QString& operation)
{
    if (error) {
        *error = QStringLiteral("%1 failed (OSStatus %2)")
                     .arg(operation)
                     .arg(static_cast<qlonglong>(status));
    }
}
}

QString credentialStoreName()
{
    return QStringLiteral("macOS Keychain");
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

    const CfOwner query(createBaseQuery());
    if (!query.get()) {
        if (error) {
            *error =
                QStringLiteral("Could not create Keychain query");
        }
        return CredentialStoreReadResult::Error;
    }
    CFDictionarySetValue(
        static_cast<CFMutableDictionaryRef>(
            const_cast<void*>(query.get())),
        kSecReturnData,
        kCFBooleanTrue);
    CFDictionarySetValue(
        static_cast<CFMutableDictionaryRef>(
            const_cast<void*>(query.get())),
        kSecMatchLimit,
        kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(
        static_cast<CFDictionaryRef>(query.get()),
        &result);
    const CfOwner resultOwner(result);
    if (status == errSecItemNotFound) {
        return CredentialStoreReadResult::NotFound;
    }
    if (status != errSecSuccess
        || !result
        || CFGetTypeID(result) != CFDataGetTypeID()) {
        setStatusError(
            error, status, QStringLiteral("Keychain lookup"));
        return CredentialStoreReadResult::Error;
    }

    const auto* data = static_cast<CFDataRef>(result);
    const CFIndex size = CFDataGetLength(data);
    if (size <= 0) {
        if (error) {
            *error =
                QStringLiteral("Stored Keychain value is empty");
        }
        return CredentialStoreReadResult::Error;
    }
    *payload = QByteArray(
        reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
        static_cast<qsizetype>(size));
    return CredentialStoreReadResult::Found;
}

bool writeNativeCredential(
    const QByteArray& payload,
    QString* error)
{
    const CfOwner query(createBaseQuery());
    const CfOwner data(CFDataCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(payload.constData()),
        static_cast<CFIndex>(payload.size())));
    if (!query.get() || !data.get()) {
        if (error) {
            *error =
                QStringLiteral("Could not create Keychain item");
        }
        return false;
    }

    const void* attributeKeys[] = {kSecValueData};
    const void* attributeValues[] = {data.get()};
    const CfOwner attributes(CFDictionaryCreate(
        kCFAllocatorDefault,
        attributeKeys,
        attributeValues,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks));
    if (!attributes.get()) {
        if (error) {
            *error = QStringLiteral(
                "Could not create Keychain attributes");
        }
        return false;
    }

    OSStatus status = SecItemUpdate(
        static_cast<CFDictionaryRef>(query.get()),
        static_cast<CFDictionaryRef>(attributes.get()));
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(
            static_cast<CFMutableDictionaryRef>(
                const_cast<void*>(query.get())),
            kSecValueData,
            data.get());
        status = SecItemAdd(
            static_cast<CFDictionaryRef>(query.get()),
            nullptr);
    }
    if (status != errSecSuccess) {
        setStatusError(
            error, status, QStringLiteral("Keychain save"));
        return false;
    }
    return true;
}

bool removeNativeCredential(QString* error)
{
    const CfOwner query(createBaseQuery());
    if (!query.get()) {
        if (error) {
            *error =
                QStringLiteral("Could not create Keychain query");
        }
        return false;
    }
    const OSStatus status = SecItemDelete(
        static_cast<CFDictionaryRef>(query.get()));
    if (status != errSecSuccess
        && status != errSecItemNotFound) {
        setStatusError(
            error, status, QStringLiteral("Keychain removal"));
        return false;
    }
    return true;
}

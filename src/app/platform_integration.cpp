// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_integration.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>

#include <string>

namespace {
HRESULT setStringProperty(IPropertyStore* store,
                          REFPROPERTYKEY key,
                          const QString& text)
{
    const std::wstring nativeText = text.toStdWString();
    PROPVARIANT value {};
    value.vt = VT_LPWSTR;
    value.pwszVal =
        const_cast<wchar_t*>(nativeText.c_str());
    return store->SetValue(key, value);
}

HRESULT createWindowsStartMenuShortcut()
{
    PWSTR programsPath = nullptr;
    HRESULT result =
        SHGetKnownFolderPath(
            FOLDERID_Programs,
            KF_FLAG_CREATE,
            nullptr,
            &programsPath);
    if (FAILED(result)) {
        return result;
    }

    const QString executable =
        QDir::toNativeSeparators(
            QCoreApplication::applicationFilePath());
    const QString shortcutPath =
        QDir(QString::fromWCharArray(programsPath))
            .filePath(QStringLiteral("MdsScope.lnk"));
    CoTaskMemFree(programsPath);

    IShellLinkW* link = nullptr;
    result =
        CoCreateInstance(
            CLSID_ShellLink,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&link));
    if (FAILED(result)) {
        return result;
    }

    const std::wstring executablePath =
        executable.toStdWString();
    const std::wstring workingDirectory =
        QDir::toNativeSeparators(
            QCoreApplication::applicationDirPath())
            .toStdWString();
    result = link->SetPath(executablePath.c_str());
    if (SUCCEEDED(result)) {
        result =
            link->SetWorkingDirectory(
                workingDirectory.c_str());
    }
    if (SUCCEEDED(result)) {
        result = link->SetDescription(L"MdsScope");
    }
    if (SUCCEEDED(result)) {
        const QString bundledIcon =
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("mdsscope.ico"));
        const std::wstring iconPath =
            QDir::toNativeSeparators(
                QFileInfo::exists(bundledIcon)
                    ? bundledIcon
                    : executable)
                .toStdWString();
        result = link->SetIconLocation(iconPath.c_str(), 0);
    }

    IPropertyStore* properties = nullptr;
    if (SUCCEEDED(result)) {
        result =
            link->QueryInterface(
                IID_PPV_ARGS(&properties));
    }
    if (SUCCEEDED(result)) {
        result =
            setStringProperty(
                properties,
                PKEY_AppUserModel_ID,
                QStringLiteral("MdsScope.MdsScope"));
    }
    if (SUCCEEDED(result)) {
        result = properties->Commit();
    }
    if (properties) {
        properties->Release();
    }

    IPersistFile* persistFile = nullptr;
    if (SUCCEEDED(result)) {
        result =
            link->QueryInterface(
                IID_PPV_ARGS(&persistFile));
    }
    if (SUCCEEDED(result)) {
        const std::wstring nativeShortcutPath =
            QDir::toNativeSeparators(shortcutPath)
                .toStdWString();
        result =
            persistFile->Save(
                nativeShortcutPath.c_str(),
                TRUE);
        if (SUCCEEDED(result)) {
            SHChangeNotify(
                SHCNE_UPDATEITEM,
                SHCNF_PATHW,
                nativeShortcutPath.c_str(),
                nullptr);
        }
    }
    if (persistFile) {
        persistFile->Release();
    }
    link->Release();
    return result;
}

void ensureWindowsStartMenuShortcut()
{
    const HRESULT initializeResult =
        CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED
                | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initializeResult)
        && initializeResult != RPC_E_CHANGED_MODE) {
        return;
    }
    createWindowsStartMenuShortcut();
    if (SUCCEEDED(initializeResult)) {
        CoUninitialize();
    }
}

void configureWindowsWindowIcon(HWND window)
{
    if (!window) {
        return;
    }
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) {
        return;
    }

    const auto loadIcon = [module](int width, int height) {
        return static_cast<HICON>(
            LoadImageW(
                module,
                L"IDI_MDSSCOPE_ICON",
                IMAGE_ICON,
                width,
                height,
                LR_SHARED));
    };
    if (HICON largeIcon =
            loadIcon(
                GetSystemMetrics(SM_CXICON),
                GetSystemMetrics(SM_CYICON))) {
        SendMessageW(
            window,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(largeIcon));
    }
    if (HICON smallIcon =
            loadIcon(
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(
            window,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(smallIcon));
    }
}
}
#endif

void preparePlatformProcess()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(
        L"MdsScope.MdsScope");
#endif
}

void installPlatformApplicationIntegration()
{
#ifdef Q_OS_WIN
    ensureWindowsStartMenuShortcut();
#endif
}

void applyPlatformWindowIntegration(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window) {
        configureWindowsWindowIcon(
            reinterpret_cast<HWND>(window->winId()));
    }
#else
    Q_UNUSED(window);
#endif
}

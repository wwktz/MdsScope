// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app_types.hpp"

#include <QString>

struct EnvironmentSaveResult {
    QString primaryPath;
    QString tomlPath;
    QString webscpPath;
    QString tomlError;
    QString webscpError;
    bool tomlSaved = false;
    bool webscpSaved = false;

    [[nodiscard]] bool complete() const
    {
        return tomlSaved && webscpSaved;
    }
};

LayoutConfig parseEnvironment(const QString& path, QString* error = nullptr);
bool writeEnvironmentToml(const LayoutConfig& config,
                          const QString& path,
                          QString* error = nullptr);
bool writeEnvironmentWebscp(const LayoutConfig& config,
                            const QString& path,
                            const QString& dataPath,
                            QString* error = nullptr);
EnvironmentSaveResult saveEnvironmentBundle(const LayoutConfig& config,
                                            const QString& requestedPath,
                                            const QString& dataPath);

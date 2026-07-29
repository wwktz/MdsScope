// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "font_settings.hpp"

#include <QString>

QString appConfigDir();
QString appCacheDir();
QString appEnvironmentDir(const QString& rootPath);
QString appSourceIndexDir(const QString& rootPath);
bool ensureSourceIndexCache(const QString& rootPath);
bool addSourceIndexSignal(const QString& tree, const QString& signal);
QString defaultExportBaseDir();
QString uiSettingsPath(const QString& rootPath);
FontSettings loadFontSettings(const QString& rootPath);
void saveFontSettings(const QString& rootPath, const FontSettings& fonts);

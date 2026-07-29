// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QString>

QDir runtimeRootDir();
bool desktopFileIsInstalled(const QString& desktopFileName);

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

constexpr int kMdsPort = 8000;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr int kNetworkTimeoutMs = 8000;
#else
constexpr int kNetworkTimeoutMs = 2500;
#endif

void traceMdsLine(const QString& line);

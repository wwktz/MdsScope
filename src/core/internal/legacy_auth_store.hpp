// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

QString legacyAuthCachePath();
bool legacyAuthCacheExists();
bool readLegacyAuthPayload(QByteArray* payload);
bool writeLegacyAuthPayload(const QByteArray& payload);
bool removeLegacyAuthPayload();

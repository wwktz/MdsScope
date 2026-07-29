// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

struct CachedAuth;

QByteArray serializeAuthPayload(const CachedAuth& auth);
bool deserializeAuthPayload(
    const QByteArray& payload,
    CachedAuth* auth);

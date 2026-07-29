// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app_types.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

QString colorForIndex(int index);
bool isDefaultSeriesColor(const QString& colorName, int index);
int colorIndexForName(const QString& colorName, int fallback);
void normalizePresetColors(QVector<SignalSpec>& specs);
QStringList uniformAxisValues(const QVector<double>& values);

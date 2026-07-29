// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QIcon>
#include <QString>

class QLabel;

void setLabelTextIfChanged(QLabel* label, const QString& text);
void clearCustomRanges(PlotSpec* plot);
bool loadedSignalMatchesConfig(const LayoutConfig& config, const LoadedSignal& item);
QString plotRefreshSignature(const PlotSpec& plot);
QString layoutRefreshSignature(const LayoutConfig& config);
bool signalDataSourceEqual(const SignalSpec& lhs, const SignalSpec& rhs);
bool signalDataSourcesEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs);
bool signalSpecsEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs);
bool optionalDoubleFromText(const QString& text, double* value);
QIcon infoIcon();
QIcon recentArrowIcon();

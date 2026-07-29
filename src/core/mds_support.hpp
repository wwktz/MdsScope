// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app_types.hpp"

#include <QString>
#include <QStringList>

constexpr int kMdsPort = 8000;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr int kNetworkTimeoutMs = 8000;
#else
constexpr int kNetworkTimeoutMs = 2500;
#endif

void traceMdsLine(const QString& line);
QString normalizedMdsSignal(QString expr);
QStringList sourceIndexSignalNames(const QString& expression);
QString scaledSiUnit(QString unit, double numericScale);
QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig);
DataReadMode higherDataReadMode(DataReadMode lhs, DataReadMode rhs);
DataReadMode effectiveSignalReadMode(DataReadMode globalMode,
                                     const SignalSpec& sig);

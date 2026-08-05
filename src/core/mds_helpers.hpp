// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app_types.hpp"

#include <QString>
#include <QStringList>

class QTextStream;

void writeLine(QTextStream& out, const QString& key, const QString& value);
QString escapedMdsExpr(QString expr);
QString normalizedMdsSignal(QString expr);
QStringList sourceIndexSignalNames(const QString& expression);
QString scaledSiUnit(QString unit, double numericScale);
QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig);
void deduplicatePlotSignals(PlotSpec* plot);
void deduplicateLayoutSignals(LayoutConfig* config);
DataReadMode higherDataReadMode(DataReadMode lhs, DataReadMode rhs);
DataReadMode effectiveSignalReadMode(DataReadMode globalMode,
                                     const SignalSpec& sig);
QStringList expandedShotList(const QString& expression);
LayoutConfig expandedShotLayout(const LayoutConfig& config);

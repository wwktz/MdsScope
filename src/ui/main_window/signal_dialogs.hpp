// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QString>

#include <optional>

class QWidget;

std::optional<QVector<SignalSpec>> editDataSources(
    const PlotSpec& base,
    const QString& currentShot,
    const QString& sourceIndexDir,
    DataReadMode inheritedReadMode,
    QWidget* parent = nullptr);

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/font_settings.hpp"

class QWidget;

void showAboutDialog(const FontSettings& fonts, QWidget* parent = nullptr);

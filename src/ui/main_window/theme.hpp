// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_runtime.hpp"

#include <QWidget>

class ThemeModeButton final : public QWidget {
public:
    explicit ThemeModeButton(QWidget* parent = nullptr);

    void setUiScale(qreal scale);
    void setMode(ThemeMode mode, bool animated = true);

private:
    class Impl;
    Impl* impl_ = nullptr;
};

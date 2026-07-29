// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/theme_mode.hpp"

#include <QWidget>

#include <memory>

class ThemeModeButton final : public QWidget {
public:
    explicit ThemeModeButton(QWidget* parent = nullptr);
    ~ThemeModeButton() override;

    void setUiScale(qreal scale);
    void setMode(ThemeMode mode, bool animated = true);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

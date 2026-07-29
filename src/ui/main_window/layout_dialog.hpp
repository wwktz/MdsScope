// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QPair>
#include <QVector>
#include <QWidget>

#include <optional>
#include <memory>

struct LayoutItem {
    int originalColumn = -1;
    int originalRow = -1;
    bool isNew = false;
    bool selected = false;
    quint64 id = 0;
};

class LayoutCanvas final : public QWidget {
public:
    using Item = LayoutItem;

    explicit LayoutCanvas(const LayoutConfig& config,
                          QWidget* parent = nullptr,
                          bool editable = true);
    ~LayoutCanvas() override;

    void createPendingPanelAtRight();
    void deleteSelected();
    void reset();
    QVector<QVector<Item>> layoutItems() const;
    QVector<QPair<int, int>> selectedOriginalPanels() const;
    void selectAllOriginalPanels();
    void clearSelectedPanels();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::optional<PlotSpec> editPanelSetup(
    const PlotSpec& plot,
    QWidget* parent = nullptr);
std::optional<QVector<QVector<LayoutCanvas::Item>>> editLayoutSetup(
    const LayoutConfig& config,
    QWidget* parent = nullptr);
bool layoutItemsMatchConfig(
    const QVector<QVector<LayoutCanvas::Item>>& layout,
    const LayoutConfig& config);

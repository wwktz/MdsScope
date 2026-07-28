// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shortcut_settings.hpp"

#include <QDialog>
#include <QVector>

class QKeySequenceEdit;

class ShortcutDialog final : public QDialog {
public:
    explicit ShortcutDialog(QVector<ShortcutBinding> bindings,
                            QWidget* parent = nullptr);

    QVector<ShortcutBinding> bindings() const;

protected:
    void accept() override;

private:
    struct EditorRow {
        ShortcutBinding binding;
        QKeySequenceEdit* editor = nullptr;
        QKeySequenceEdit* alternativeEditor = nullptr;
    };

    void restoreDefaults();
    bool validateShortcuts() const;

    QVector<EditorRow> rows_;
};

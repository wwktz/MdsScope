// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcut_dialog.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
bool isPanelNavigation(ShortcutCommand command)
{
    return command == ShortcutCommand::PanelLeft
           || command == ShortcutCommand::PanelDown
           || command == ShortcutCommand::PanelUp
           || command == ShortcutCommand::PanelRight;
}

bool isPointTrackingCommand(ShortcutCommand command)
{
    return command == ShortcutCommand::PointPrevious
           || command == ShortcutCommand::PointNext
           || command == ShortcutCommand::ExitPoint;
}

bool isPopupMenuNavigation(ShortcutCommand command)
{
    return command == ShortcutCommand::MenuLeft
           || command == ShortcutCommand::MenuDown
           || command == ShortcutCommand::MenuUp
           || command == ShortcutCommand::MenuRight
           || command == ShortcutCommand::MenuActivate;
}

bool contextsAreExclusive(ShortcutCommand first, ShortcutCommand second)
{
    if (isPopupMenuNavigation(first)
        != isPopupMenuNavigation(second)) {
        return true;
    }
    return (isPointTrackingCommand(first)
            && isPanelNavigation(second))
           || (isPointTrackingCommand(second)
               && isPanelNavigation(first));
}

bool isStrictPrefix(const QKeySequence& prefix,
                    const QKeySequence& sequence)
{
    if (prefix.isEmpty()
        || prefix.count() >= sequence.count()) {
        return false;
    }
    for (uint i = 0; i < static_cast<uint>(prefix.count()); ++i) {
        if (prefix[i] != sequence[i]) {
            return false;
        }
    }
    return true;
}
}

ShortcutDialog::ShortcutDialog(QVector<ShortcutBinding> bindings,
                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Keyboard Shortcuts — %1")
                       .arg(shortcutPlatformName()));
    setMinimumSize(860, 600);

    auto* root = new QVBoxLayout(this);
    auto* description = new QLabel(shortcutPlatformDescription(), this);
    description->setWordWrap(true);
    const QPalette descriptionPalette = description->palette();
    const QColor descriptionColor =
        descriptionPalette.color(QPalette::Window).lightness() >= 128
            ? descriptionPalette.color(QPalette::Dark)
            : descriptionPalette.color(QPalette::Text);
    description->setStyleSheet(
        QStringLiteral(
            "QLabel { color: %1; padding: 2px 2px 8px 2px; }")
            .arg(descriptionColor.name()));
    root->addWidget(description);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* contents = new QWidget(scroll);
    auto* groupsLayout = new QVBoxLayout(contents);
    groupsLayout->setContentsMargins(0, 0, 0, 0);
    groupsLayout->setSpacing(10);

    QString currentCategory;
    QGridLayout* categoryLayout = nullptr;
    int categoryRow = 0;
    rows_.reserve(bindings.size());
    for (ShortcutBinding& binding : bindings) {
        if (binding.category != currentCategory) {
            currentCategory = binding.category;
            auto* group = new QGroupBox(currentCategory, contents);
            categoryLayout = new QGridLayout(group);
            categoryLayout->setColumnStretch(0, 1);
            categoryLayout->setColumnMinimumWidth(1, 210);
            categoryLayout->setColumnMinimumWidth(2, 210);
            categoryLayout->addWidget(
                new QLabel(QStringLiteral("Shortcut"), group),
                0,
                1);
            categoryLayout->addWidget(
                new QLabel(QStringLiteral("Alternative (optional)"), group),
                0,
                2);
            groupsLayout->addWidget(group);
            categoryRow = 1;
        }

        auto* label = new QLabel(binding.label, contents);
        auto* editor = new QKeySequenceEdit(binding.sequence, contents);
        auto* alternativeEditor =
            new QKeySequenceEdit(binding.alternative, contents);
        editor->setClearButtonEnabled(true);
        alternativeEditor->setClearButtonEnabled(true);
        categoryLayout->addWidget(label, categoryRow, 0);
        categoryLayout->addWidget(editor, categoryRow, 1);
        categoryLayout->addWidget(alternativeEditor, categoryRow, 2);
        ++categoryRow;
        rows_.push_back(
            {std::move(binding), editor, alternativeEditor});
    }
    groupsLayout->addStretch(1);
    scroll->setWidget(contents);
    root->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::RestoreDefaults
            | QDialogButtonBox::Ok
            | QDialogButtonBox::Cancel,
        this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ShortcutDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &ShortcutDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked,
            this,
            &ShortcutDialog::restoreDefaults);
    root->addWidget(buttons);
}

QVector<ShortcutBinding> ShortcutDialog::bindings() const
{
    QVector<ShortcutBinding> result;
    result.reserve(rows_.size());
    for (const EditorRow& row : rows_) {
        ShortcutBinding binding = row.binding;
        binding.sequence = row.editor->keySequence();
        binding.alternative =
            row.alternativeEditor->keySequence();
        result.push_back(std::move(binding));
    }
    return result;
}

void ShortcutDialog::accept()
{
    if (!validateShortcuts()) {
        return;
    }
    QDialog::accept();
}

void ShortcutDialog::restoreDefaults()
{
    const QVector<ShortcutBinding> defaults = defaultShortcutBindings();
    for (EditorRow& row : rows_) {
        const auto it = std::find_if(
            defaults.cbegin(),
            defaults.cend(),
            [&row](const ShortcutBinding& binding) {
                return binding.command == row.binding.command;
            });
        if (it != defaults.cend()) {
            row.editor->setKeySequence(it->sequence);
            row.alternativeEditor->setKeySequence(
                it->alternative);
        }
    }
}

bool ShortcutDialog::validateShortcuts() const
{
    struct AssignedShortcut {
        ShortcutCommand command;
        QString label;
        QKeySequence sequence;
    };
    QVector<AssignedShortcut> assigned;
    for (const EditorRow& row : rows_) {
        const QList<QPair<QString, QKeySequence>> sequences{
            {QStringLiteral("primary"),
             row.editor->keySequence()},
            {QStringLiteral("alternative"),
             row.alternativeEditor->keySequence()},
        };
        for (const auto& [kind, sequence] : sequences) {
            if (!sequence.isEmpty()) {
                assigned.push_back(
                    {row.binding.command,
                     QStringLiteral("%1 (%2)")
                         .arg(row.binding.label, kind),
                     sequence});
            }
        }
    }

    for (int i = 0; i < assigned.size(); ++i) {
        for (int j = i + 1; j < assigned.size(); ++j) {
            const AssignedShortcut& first = assigned[i];
            const AssignedShortcut& second = assigned[j];
            if (contextsAreExclusive(first.command, second.command)) {
                continue;
            }
            const bool same = first.sequence == second.sequence;
            const bool prefix =
                isStrictPrefix(first.sequence, second.sequence)
                || isStrictPrefix(second.sequence, first.sequence);
            if (!same && !prefix) {
                continue;
            }
            QMessageBox::warning(
                const_cast<ShortcutDialog*>(this),
                QStringLiteral("Shortcut conflict"),
                QStringLiteral("%1 and %2 conflict on %3.")
                    .arg(first.label,
                         second.label,
                         nativeShortcutText(
                             same ? first.sequence
                                  : (first.sequence.count()
                                             < second.sequence.count()
                                         ? first.sequence
                                         : second.sequence))));
            return false;
        }
    }
    return true;
}

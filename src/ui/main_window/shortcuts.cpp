// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "shortcut_dialog.hpp"
#include "ui/plot/plot_widget.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>
#include <QToolButton>

namespace {
bool isModifierKey(int key)
{
    return key == Qt::Key_Control
           || key == Qt::Key_Shift
           || key == Qt::Key_Alt
           || key == Qt::Key_Meta
           || key == Qt::Key_AltGr;
}

bool isInputWidget(QWidget* widget)
{
    if (!widget) {
        return false;
    }
    if (qobject_cast<QLineEdit*>(widget)
        || qobject_cast<QTextEdit*>(widget)
        || qobject_cast<QPlainTextEdit*>(widget)
        || qobject_cast<QAbstractSpinBox*>(widget)) {
        return true;
    }
    return qobject_cast<QComboBox*>(widget) != nullptr;
}

bool isShotInputWidget(QWidget* widget,
                       QComboBox* shotCombo,
                       QLineEdit* shotEdit)
{
    return widget
           && (widget == shotEdit
               || widget == shotCombo
               || (shotCombo
                   && shotCombo->isAncestorOf(widget)));
}

QKeySequence keySequenceFrom(
    const QList<QKeyCombination>& keys)
{
    const QKeyCombination empty =
        QKeyCombination::fromCombined(0);
    return QKeySequence(
        keys.value(0, empty),
        keys.value(1, empty),
        keys.value(2, empty),
        keys.value(3, empty));
}

bool sequenceStartsWith(const QKeySequence& sequence,
                        const QKeySequence& prefix)
{
    if (prefix.isEmpty()
        || prefix.count() > sequence.count()) {
        return false;
    }
    for (uint i = 0; i < static_cast<uint>(prefix.count()); ++i) {
        if (sequence[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

QList<QKeySequence> assignedSequences(
    const ShortcutBinding& binding)
{
    QList<QKeySequence> sequences;
    if (!binding.sequence.isEmpty()) {
        sequences.push_back(binding.sequence);
    }
    if (!binding.alternative.isEmpty()) {
        sequences.push_back(binding.alternative);
    }
    return sequences;
}
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn
        && isShotInputWidget(
            qobject_cast<QWidget*>(watched),
            shotCombo_,
            shotEdit_)) {
        beginShotEditSession();
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() != QEvent::KeyPress) {
        return QMainWindow::eventFilter(watched, event);
    }

    auto* target = qobject_cast<QWidget*>(watched);
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    QWidget* focus = QApplication::focusWidget();
    const bool shotInputFocused =
        isShotInputWidget(
            focus, shotCombo_, shotEdit_)
        || isShotInputWidget(
            target, shotCombo_, shotEdit_);
    if (shotInputFocused) {
        beginShotEditSession();
        if (handleShotEditExitKey(keyEvent)) {
            keyEvent->accept();
            return true;
        }
    }
    if (!target
        || target->window() != this
        || QApplication::activeModalWidget()
        || QApplication::activePopupWidget()) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (isShotInputWidget(
            target, shotCombo_, shotEdit_)) {
        beginShotEditSession();
    }

    if (isInputWidget(target)) {
        pendingShortcutKeys_.clear();
        shortcutSequenceTimer_.stop();
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers()
            & ~Qt::KeypadModifier;
        if (!(modifiers
              & (Qt::ControlModifier
                 | Qt::AltModifier
                 | Qt::MetaModifier))) {
            return QMainWindow::eventFilter(watched, event);
        }
    }

    if (qobject_cast<QAbstractButton*>(target)
        && keyEvent->modifiers() == Qt::NoModifier
        && (keyEvent->key() == Qt::Key_Space
            || keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter)) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (handleShortcutKey(keyEvent, target)) {
        keyEvent->accept();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::handleShortcutKey(QKeyEvent* event,
                                   QWidget* target)
{
    if (!event || isModifierKey(event->key())) {
        return false;
    }

    const bool editingText = isInputWidget(target);
    const bool editingShot = isShotInputWidget(
        target, shotCombo_, shotEdit_);
    Qt::KeyboardModifiers modifiers =
        event->modifiers() & ~Qt::KeypadModifier;
    const QKeyCombination key(
        modifiers,
        static_cast<Qt::Key>(event->key()));

    auto findMatch = [this, editingText, editingShot](
                         const QKeySequence& candidate,
                         ShortcutCommand* exactCommand,
                         bool* partialMatch) {
        *partialMatch = false;
        for (const ShortcutBinding& binding :
             std::as_const(shortcutBindings_)) {
            if (!shortcutCommandEnabled(binding.command)) {
                continue;
            }
            // Editing widgets retain their native Select All, Cut, Undo,
            // cursor movement, and text entry behavior. Save and shot
            // navigation are the only workspace commands allowed through;
            // shot navigation first discards any uncommitted shot draft.
            if (editingText
                && binding.command != ShortcutCommand::Save
                && !(editingShot
                     && (binding.command
                             == ShortcutCommand::PreviousShot
                         || binding.command
                                == ShortcutCommand::NextShot
                         || binding.command
                                == ShortcutCommand::LatestShot))) {
                continue;
            }
            for (const QKeySequence& sequence :
                 assignedSequences(binding)) {
                if (candidate == sequence) {
                    *exactCommand = binding.command;
                    return true;
                }
                if (candidate.count() < sequence.count()
                    && sequenceStartsWith(sequence, candidate)) {
                    *partialMatch = true;
                }
            }
        }
        return false;
    };

    auto tryKeys = [&](QList<QKeyCombination> keys) {
        const QKeySequence candidate =
            keySequenceFrom(keys);
        ShortcutCommand command = ShortcutCommand::Save;
        bool partial = false;
        if (findMatch(candidate, &command, &partial)) {
            pendingShortcutKeys_.clear();
            shortcutSequenceTimer_.stop();
            const bool repeatable =
                command == ShortcutCommand::PanelLeft
                || command == ShortcutCommand::PanelDown
                || command == ShortcutCommand::PanelUp
                || command == ShortcutCommand::PanelRight;
            if (!event->isAutoRepeat() || repeatable) {
                triggerShortcutCommand(command);
            }
            return 2;
        }
        if (partial) {
            pendingShortcutKeys_ = std::move(keys);
            shortcutSequenceTimer_.start();
            return 1;
        }
        return 0;
    };

    QList<QKeyCombination> keys = pendingShortcutKeys_;
    keys.push_back(key);
    int result = tryKeys(keys);
    if (result == 0 && !pendingShortcutKeys_.isEmpty()) {
        pendingShortcutKeys_.clear();
        shortcutSequenceTimer_.stop();
        result = tryKeys({key});
    }
    if (result == 0) {
        pendingShortcutKeys_.clear();
        shortcutSequenceTimer_.stop();
    }
    return result != 0;
}

bool MainWindow::shortcutCommandEnabled(
    ShortcutCommand command) const
{
    switch (command) {
    case ShortcutCommand::PanelLeft:
    case ShortcutCommand::PanelDown:
    case ShortcutCommand::PanelUp:
    case ShortcutCommand::PanelRight:
        return !activePointPlot_
               || !activePointPlot_->pointTrackingActive();
    case ShortcutCommand::PointPrevious:
    case ShortcutCommand::PointNext:
    case ShortcutCommand::ExitPoint:
        return activePointPlot_
               && activePointPlot_->pointTrackingActive();
    default:
        return true;
    }
}

bool MainWindow::triggerShortcutCommand(
    ShortcutCommand command)
{
    switch (command) {
    case ShortcutCommand::Save:
        saveCurrentEnvironment();
        break;
    case ShortcutCommand::PointMode:
        setInteractionMode(InteractionMode::Point);
        break;
    case ShortcutCommand::ZoomMode:
        setInteractionMode(InteractionMode::Zoom);
        break;
    case ShortcutCommand::FocusShot:
        if (shotEdit_) {
            beginShotEditSession();
            shotEdit_->setFocus(Qt::ShortcutFocusReason);
            shotEdit_->selectAll();
        }
        break;
    case ShortcutCommand::ToggleRefresh:
        onStopOrContinue();
        break;
    case ShortcutCommand::MaximizePanel:
        maximizeCurrentPanel();
        break;
    case ShortcutCommand::ResetCurrentScale:
        resetCurrentScale();
        break;
    case ShortcutCommand::ResetAllScales:
        resetScales();
        break;
    case ShortcutCommand::ShowAllPanels:
        showAllPanels();
        break;
    case ShortcutCommand::SameXScale:
        applyScaleToAll();
        break;
    case ShortcutCommand::SameYScale:
        applyYScaleToAll();
        break;
    case ShortcutCommand::PreviousShot:
        stepShot(-1);
        break;
    case ShortcutCommand::NextShot:
        stepShot(1);
        break;
    case ShortcutCommand::LatestShot:
        latestShot();
        break;
    case ShortcutCommand::PointPrevious:
        if (activePointPlot_) {
            activePointPlot_->stepActivePoint(-1);
        }
        break;
    case ShortcutCommand::PointNext:
        if (activePointPlot_) {
            activePointPlot_->stepActivePoint(1);
        }
        break;
    case ShortcutCommand::PanelLeft:
        movePanelSelection(-1, 0);
        break;
    case ShortcutCommand::PanelDown:
        movePanelSelection(0, 1);
        break;
    case ShortcutCommand::PanelUp:
        movePanelSelection(0, -1);
        break;
    case ShortcutCommand::PanelRight:
        movePanelSelection(1, 0);
        break;
    case ShortcutCommand::ExitPoint:
        stopActivePointTracking();
        break;
    }
    return true;
}

void MainWindow::movePanelSelection(int columnDelta,
                                    int rowDelta)
{
    if (plotWidgets_.isEmpty()) {
        return;
    }

    int column = selectedColumn_;
    int row = selectedRow_;
    if (column < 0
        || row < 0
        || column >= plotWidgets_.size()
        || row >= plotWidgets_[column].size()) {
        column = -1;
        row = -1;
        for (int c = 0; c < plotWidgets_.size(); ++c) {
            if (!plotWidgets_[c].isEmpty()) {
                column = c;
                row = 0;
                break;
            }
        }
    } else if (columnDelta != 0) {
        int candidate = column + columnDelta;
        while (candidate >= 0
               && candidate < plotWidgets_.size()
               && plotWidgets_[candidate].isEmpty()) {
            candidate += columnDelta;
        }
        if (candidate >= 0
            && candidate < plotWidgets_.size()
            && !plotWidgets_[candidate].isEmpty()) {
            column = candidate;
            row = std::clamp(
                row,
                0,
                static_cast<int>(
                    plotWidgets_[column].size())
                    - 1);
        }
    } else if (rowDelta != 0) {
        row = std::clamp(
            row + rowDelta,
            0,
            static_cast<int>(plotWidgets_[column].size()) - 1);
    }

    if (column < 0
        || row < 0
        || column >= plotWidgets_.size()
        || row >= plotWidgets_[column].size()) {
        return;
    }

    const bool keepMaximized = singlePanelMaximized_;
    selectPlot(column, row);
    if (keepMaximized) {
        maximizeCurrentPanel();
    }
    focusSelectedPlot();
}

void MainWindow::stopActivePointTracking()
{
    PlotWidget* plot = activePointPlot_;
    if (!plot) {
        return;
    }
    plot->stopPointTracking();
    plot->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::focusSelectedPlot()
{
    if (PlotWidget* plot = currentPlotWidget()) {
        plot->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    if (gridHost_) {
        gridHost_->setFocus(Qt::ShortcutFocusReason);
    }
}

void MainWindow::beginShotEditSession()
{
    if (!shotEdit_ || shotEditSessionActive_) {
        return;
    }
    shotEditSessionText_ = shotEdit_->text();
    shotEditSessionActive_ = true;
    pendingShotEditExitKeys_.clear();
    shotEditExitTimer_.stop();
}

void MainWindow::cancelShotEditSession()
{
    if (!shotEdit_) {
        return;
    }
    if (shotEditSessionActive_
        && shotEdit_->text() != shotEditSessionText_) {
        shotEdit_->setText(shotEditSessionText_);
    }
    shotEditSessionActive_ = false;
    pendingShotEditExitKeys_.clear();
    shotEditExitTimer_.stop();
    pendingShortcutKeys_.clear();
    shortcutSequenceTimer_.stop();
    focusSelectedPlot();
}

bool MainWindow::handleShotEditExitKey(QKeyEvent* event)
{
    if (!event || !shotEditSessionActive_
        || isModifierKey(event->key())) {
        return false;
    }

    const auto binding = std::find_if(
        shortcutBindings_.cbegin(),
        shortcutBindings_.cend(),
        [](const ShortcutBinding& item) {
            return item.command
                   == ShortcutCommand::ExitPoint;
        });
    if (binding == shortcutBindings_.cend()) {
        return false;
    }
    const QList<QKeySequence> sequences =
        assignedSequences(*binding);
    if (sequences.isEmpty()) {
        return false;
    }

    const QKeyCombination key(
        event->modifiers() & ~Qt::KeypadModifier,
        static_cast<Qt::Key>(event->key()));
    auto tryKeys = [&](QList<QKeyCombination> keys) {
        const QKeySequence candidate =
            keySequenceFrom(keys);
        bool partial = false;
        for (const QKeySequence& sequence : sequences) {
            if (candidate == sequence) {
                cancelShotEditSession();
                return 2;
            }
            partial =
                partial
                || (candidate.count() < sequence.count()
                    && sequenceStartsWith(
                        sequence, candidate));
        }
        if (partial) {
            pendingShotEditExitKeys_ = std::move(keys);
            shotEditExitTimer_.start();
            return 1;
        }
        return 0;
    };

    QList<QKeyCombination> keys =
        pendingShotEditExitKeys_;
    keys.push_back(key);
    int result = tryKeys(keys);
    if (result == 0
        && !pendingShotEditExitKeys_.isEmpty()) {
        pendingShotEditExitKeys_.clear();
        shotEditExitTimer_.stop();
        result = tryKeys({key});
    }
    if (result == 0) {
        pendingShotEditExitKeys_.clear();
        shotEditExitTimer_.stop();
    }
    return result != 0;
}

void MainWindow::rebuildShotInputShortcuts()
{
    qDeleteAll(shotInputShortcuts_);
    shotInputShortcuts_.clear();
    if (!shotCombo_) {
        return;
    }

    const QList<ShortcutCommand> commands{
        ShortcutCommand::PreviousShot,
        ShortcutCommand::NextShot,
        ShortcutCommand::LatestShot,
    };
    for (ShortcutCommand command : commands) {
        const auto binding = std::find_if(
            shortcutBindings_.cbegin(),
            shortcutBindings_.cend(),
            [command](const ShortcutBinding& item) {
                return item.command == command;
            });
        if (binding == shortcutBindings_.cend()) {
            continue;
        }
        for (const QKeySequence& sequence :
             assignedSequences(*binding)) {
            auto* shortcut =
                new QShortcut(sequence, shotCombo_);
            shortcut->setContext(
                Qt::WidgetWithChildrenShortcut);
            shortcut->setAutoRepeat(false);
            connect(shortcut,
                    &QShortcut::activated,
                    this,
                    [this, command] {
                beginShotEditSession();
                if (command
                    == ShortcutCommand::ExitPoint) {
                    cancelShotEditSession();
                    return;
                }
                triggerShortcutCommand(command);
            });
            shotInputShortcuts_.push_back(shortcut);
        }
    }
}

void MainWindow::openShortcutDialog()
{
    pendingShortcutKeys_.clear();
    shortcutSequenceTimer_.stop();
    ShortcutDialog dialog(shortcutBindings_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    shortcutBindings_ = dialog.bindings();
    saveShortcutBindings(rootPath_, shortcutBindings_);
    rebuildShotInputShortcuts();
    updateShortcutToolTips();
    setStatus(QStringLiteral("Keyboard shortcuts updated for %1")
                  .arg(shortcutPlatformName()));
}

QString MainWindow::shortcutText(
    ShortcutCommand command) const
{
    const auto it = std::find_if(
        shortcutBindings_.cbegin(),
        shortcutBindings_.cend(),
        [command](const ShortcutBinding& binding) {
            return binding.command == command;
        });
    if (it == shortcutBindings_.cend()) {
        return {};
    }
    QStringList text;
    if (!it->sequence.isEmpty()) {
        text.push_back(nativeShortcutText(it->sequence));
    }
    if (!it->alternative.isEmpty()) {
        text.push_back(nativeShortcutText(it->alternative));
    }
    return text.join(QStringLiteral(" / "));
}

void MainWindow::updateShortcutToolTips()
{
    auto withShortcut = [this](const QString& text,
                               ShortcutCommand command) {
        const QString keys = shortcutText(command);
        return keys.isEmpty()
                   ? text
                   : QStringLiteral("%1 (%2)").arg(text, keys);
    };

    if (keyboardShortcutsAction_) {
        keyboardShortcutsAction_->setToolTip(
            QStringLiteral("Keyboard shortcuts — %1")
                .arg(shortcutPlatformName()));
    }
    if (saveAction_) {
        saveAction_->setToolTip(
            withShortcut(QStringLiteral("Save"),
                         ShortcutCommand::Save));
    }
    if (zoomButton_) {
        zoomButton_->setToolTip(
            withShortcut(
                QStringLiteral(
                    "Zoom / Move: drag to zoom, middle-drag or Shift-drag to move"),
                ShortcutCommand::ZoomMode));
    }
    if (pointButton_) {
        const QString pointMovement =
            QStringLiteral("%1 / %2")
                .arg(shortcutText(
                         ShortcutCommand::PointPrevious),
                     shortcutText(
                         ShortcutCommand::PointNext));
        pointButton_->setToolTip(
            withShortcut(
                QStringLiteral("Point: click to activate"),
                ShortcutCommand::PointMode)
            + QStringLiteral("; ")
            + QStringLiteral("move point (%1)")
                  .arg(pointMovement)
            + QStringLiteral("; ")
            + withShortcut(
                QStringLiteral("exit tracking"),
                ShortcutCommand::ExitPoint));
    }
    if (stopButton_) {
        const QString label =
            refresh_ && refresh_->dataPaused()
                ? QStringLiteral("Continue data refresh")
                : QStringLiteral("Stop data refresh");
        stopButton_->setToolTip(
            withShortcut(label,
                         ShortcutCommand::ToggleRefresh));
    }
    if (previousShotButton_) {
        previousShotButton_->setToolTip(
            withShortcut(QStringLiteral("Previous shot"),
                         ShortcutCommand::PreviousShot));
    }
    if (nextShotButton_) {
        nextShotButton_->setToolTip(
            withShortcut(QStringLiteral("Next shot"),
                         ShortcutCommand::NextShot));
    }
    if (latestShotButton_) {
        latestShotButton_->setToolTip(
            withShortcut(QStringLiteral("Latest shot"),
                         ShortcutCommand::LatestShot));
    }
}

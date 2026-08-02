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
#include <QMenu>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>
#include <QToolButton>

#include <utility>

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

bool isPopupMenuNavigation(ShortcutCommand command)
{
    return command == ShortcutCommand::MenuLeft
           || command == ShortcutCommand::MenuDown
           || command == ShortcutCommand::MenuUp
           || command == ShortcutCommand::MenuRight;
}

bool isGlobalShortcutAllowedWhileEditing(ShortcutCommand command)
{
    return command == ShortcutCommand::OpenFile
           || command == ShortcutCommand::OpenRecentFiles
           || command == ShortcutCommand::OpenWebMenu
           || command == ShortcutCommand::Save
           || command == ShortcutCommand::GlobalRate
           || command == ShortcutCommand::GlobalLayout
           || command == ShortcutCommand::GlobalExport
           || command == ShortcutCommand::RefreshData
           || command == ShortcutCommand::Escape;
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
        const QKeyCombination sequenceKey = sequence[i];
        const QKeyCombination prefixKey = prefix[i];
        const bool enterEquivalent =
            (sequenceKey.key() == Qt::Key_Return
             || sequenceKey.key() == Qt::Key_Enter)
            && (prefixKey.key() == Qt::Key_Return
                || prefixKey.key() == Qt::Key_Enter);
        if (sequenceKey.keyboardModifiers()
                != prefixKey.keyboardModifiers()
            || (!enterEquivalent
                && sequenceKey.key() != prefixKey.key())) {
            return false;
        }
    }
    return true;
}

bool sequencesEqual(const QKeySequence& first,
                    const QKeySequence& second)
{
    return first.count() == second.count()
           && sequenceStartsWith(first, second);
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
    if (dispatchingPopupMenuKey_ || dispatchingEscapeKey_) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (QApplication::activePopupWidget()) {
        if (handleShortcutKey(keyEvent, target)) {
            keyEvent->accept();
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }
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
    if (QApplication::activeModalWidget()) {
        if (handleShortcutKey(keyEvent, target)) {
            keyEvent->accept();
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }
    if (!target || target->window() != this) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (isShotInputWidget(
            target, shotCombo_, shotEdit_)) {
        beginShotEditSession();
    }

    if (isInputWidget(target)) {
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers()
            & ~Qt::KeypadModifier;
        if (!(modifiers
              & (Qt::ControlModifier
                 | Qt::AltModifier
                 | Qt::MetaModifier))
            && pendingShortcutKeys_.isEmpty()) {
            return QMainWindow::eventFilter(watched, event);
        }
    }

    if (qobject_cast<QAbstractButton*>(target)
        && keyEvent->modifiers() == Qt::NoModifier) {
        const bool activateKey =
            keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter;
        if (keyEvent->key() == Qt::Key_Space
            || (activateKey
                && !shortcutCommandEnabled(
                    ShortcutCommand::MenuActivate))) {
            return QMainWindow::eventFilter(watched, event);
        }
    }

    if (handleFixedPointKey(keyEvent, target)) {
        keyEvent->accept();
        return true;
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
        bool exactMatch = false;
        for (const ShortcutBinding& binding :
             std::as_const(shortcutBindings_)) {
            if (!shortcutCommandEnabled(binding.command)) {
                continue;
            }
            // Editing widgets retain their native Select All, Cut, Undo,
            // cursor movement, and text entry behavior. Explicit global
            // commands and shot navigation are allowed through; shot
            // navigation first discards any uncommitted shot draft.
            if (editingText
                && !isGlobalShortcutAllowedWhileEditing(
                    binding.command)
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
                if (sequencesEqual(candidate, sequence)) {
                    if (!exactMatch) {
                        *exactCommand = binding.command;
                        exactMatch = true;
                    }
                    continue;
                }
                if (candidate.count() < sequence.count()
                    && sequenceStartsWith(sequence, candidate)) {
                    *partialMatch = true;
                }
            }
        }
        return exactMatch;
    };

    auto tryKeys = [&](QList<QKeyCombination> keys) {
        const QKeySequence candidate =
            keySequenceFrom(keys);
        ShortcutCommand command = ShortcutCommand::Save;
        bool partial = false;
        const bool exact =
            findMatch(candidate, &command, &partial);
        if (exact && partial) {
            pendingShortcutKeys_ = std::move(keys);
            const bool panelNavigation =
                command == ShortcutCommand::PanelLeft
                || command == ShortcutCommand::PanelDown
                || command == ShortcutCommand::PanelUp
                || command == ShortcutCommand::PanelRight;
            const bool menuNavigation =
                command == ShortcutCommand::MenuLeft
                || command == ShortcutCommand::MenuDown
                || command == ShortcutCommand::MenuUp
                || command == ShortcutCommand::MenuRight;
            const bool navigateImmediately =
                menuNavigation
                || panelNavigation;
            if (navigateImmediately) {
                if (panelNavigation) {
                    pendingPanelNavigationOrigin_ =
                        PanelId {selectedColumn_, selectedRow_};
                    pendingPanelNavigationPausedPoint_ =
                        pausedPointPlot_;
                }
                triggerShortcutCommand(command);
                pendingExactShortcut_.reset();
            } else {
                pendingExactShortcut_ = command;
            }
            shortcutSequenceTimer_.start();
            return 1;
        }
        if (exact) {
            if (pendingPanelNavigationOrigin_
                && keys.size() > 1) {
                restorePendingPanelNavigation();
            } else {
                pendingPanelNavigationOrigin_.reset();
                pendingPanelNavigationPausedPoint_.clear();
            }
            pendingShortcutKeys_.clear();
            pendingExactShortcut_.reset();
            shortcutSequenceTimer_.stop();
            const bool repeatable =
                command == ShortcutCommand::PanelLeft
                || command == ShortcutCommand::PanelDown
                || command == ShortcutCommand::PanelUp
                || command == ShortcutCommand::PanelRight
                || command == ShortcutCommand::MenuLeft
                || command == ShortcutCommand::MenuDown
                || command == ShortcutCommand::MenuUp
                || command == ShortcutCommand::MenuRight;
            if (!event->isAutoRepeat() || repeatable) {
                triggerShortcutCommand(command);
            }
            return 2;
        }
        if (partial) {
            pendingShortcutKeys_ = std::move(keys);
            pendingExactShortcut_.reset();
            pendingPanelNavigationOrigin_.reset();
            pendingPanelNavigationPausedPoint_.clear();
            shortcutSequenceTimer_.start();
            return 1;
        }
        return 0;
    };

    QList<QKeyCombination> keys = pendingShortcutKeys_;
    keys.push_back(key);
    int result = tryKeys(keys);
    if (result == 0 && !pendingShortcutKeys_.isEmpty()) {
        const std::optional<ShortcutCommand> delayed =
            std::exchange(pendingExactShortcut_, std::nullopt);
        if (key.key() == Qt::Key_Escape) {
            restorePendingPanelNavigation();
        } else {
            pendingPanelNavigationOrigin_.reset();
            pendingPanelNavigationPausedPoint_.clear();
        }
        pendingShortcutKeys_.clear();
        shortcutSequenceTimer_.stop();
        if (delayed && shortcutCommandEnabled(*delayed)) {
            triggerShortcutCommand(*delayed);
        }
        result = tryKeys({key});
    }
    if (result == 0) {
        pendingShortcutKeys_.clear();
        pendingExactShortcut_.reset();
        pendingPanelNavigationOrigin_.reset();
        pendingPanelNavigationPausedPoint_.clear();
        shortcutSequenceTimer_.stop();
    }
    return result != 0;
}

bool MainWindow::shortcutCommandEnabled(
    ShortcutCommand command) const
{
    const bool popupActive =
        QApplication::activePopupWidget() != nullptr;
    if (command == ShortcutCommand::Escape) {
        return !dispatchingEscapeKey_;
    }
    if (command == ShortcutCommand::MenuActivate) {
        return (popupActive && !dispatchingPopupMenuKey_)
               || (!QApplication::activeModalWidget()
                   && currentInteractionMode_
                          == InteractionMode::Point
                   && (!activePointPlot_
                       || !activePointPlot_->pointTrackingActive())
                   && (pausedPointPlot_ != nullptr
                       || currentPlotWidget() != nullptr));
    }
    if (isPopupMenuNavigation(command)) {
        return popupActive && !dispatchingPopupMenuKey_;
    }
    if (popupActive) {
        return false;
    }
    if (QApplication::activeModalWidget()) {
        return false;
    }
    switch (command) {
    case ShortcutCommand::PanelLeft:
    case ShortcutCommand::PanelDown:
    case ShortcutCommand::PanelUp:
    case ShortcutCommand::PanelRight:
        return !activePointPlot_
               || !activePointPlot_->pointTrackingActive();
    case ShortcutCommand::PointPrevious:
    case ShortcutCommand::PointNext:
        return activePointPlot_
               && activePointPlot_->pointTrackingActive();
    case ShortcutCommand::PanelRate:
    case ShortcutCommand::PanelSourceSetup:
    case ShortcutCommand::PanelExport:
    case ShortcutCommand::PanelSetup:
        return currentPlotWidget() != nullptr;
    default:
        return true;
    }
}

bool MainWindow::triggerShortcutCommand(
    ShortcutCommand command)
{
    switch (command) {
    case ShortcutCommand::OpenFile:
        openEnvironmentFile();
        break;
    case ShortcutCommand::OpenRecentFiles:
        showRecentEnvironmentMenu();
        break;
    case ShortcutCommand::OpenWebMenu:
        showInternalWebMenu();
        break;
    case ShortcutCommand::Save:
        saveCurrentEnvironment();
        break;
    case ShortcutCommand::GlobalRate:
        openGlobalRateMenu();
        break;
    case ShortcutCommand::GlobalLayout:
        openLayoutSetupDialog();
        break;
    case ShortcutCommand::GlobalExport:
        openExportDataDialog();
        break;
    case ShortcutCommand::PointMode:
        setInteractionMode(InteractionMode::Point);
        activatePointForCurrentPanel();
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
    case ShortcutCommand::RefreshData:
        refreshData();
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
    case ShortcutCommand::PanelRate:
        if (PlotWidget* plot = currentPlotWidget()) {
            showPanelContextMenu(
                plot,
                selectedColumn_,
                selectedRow_,
                plot->rect().center(),
                true);
        }
        break;
    case ShortcutCommand::PanelSourceSetup:
        dataSourceSetupForCurrentPanel();
        break;
    case ShortcutCommand::PanelExport:
        exportCurrentPanelData();
        break;
    case ShortcutCommand::PanelSetup:
        panelSetupForCurrentPanel();
        break;
    case ShortcutCommand::Escape:
        if (QApplication::activePopupWidget()
            || QApplication::activeModalWidget()) {
            dispatchEscapeKey();
        } else if (activePointPlot_
            && activePointPlot_->pointTrackingActive()) {
            pauseActivePointTracking();
        } else {
            dispatchEscapeKey();
        }
        break;
    case ShortcutCommand::MenuLeft:
        dispatchPopupMenuKey(Qt::Key_Left);
        break;
    case ShortcutCommand::MenuDown:
        dispatchPopupMenuKey(Qt::Key_Down);
        break;
    case ShortcutCommand::MenuUp:
        dispatchPopupMenuKey(Qt::Key_Up);
        break;
    case ShortcutCommand::MenuRight:
        dispatchPopupMenuKey(Qt::Key_Right);
        break;
    case ShortcutCommand::MenuActivate:
        if (QApplication::activePopupWidget()) {
            dispatchPopupMenuKey(Qt::Key_Return);
        } else if (!resumePausedPoint()) {
            activatePointForCurrentPanel();
        }
        break;
    }
    return true;
}

void MainWindow::dispatchPopupMenuKey(Qt::Key key)
{
    QWidget* popup = QApplication::activePopupWidget();
    if (!popup) {
        return;
    }
    QWidget* receiver = qobject_cast<QMenu*>(popup)
                            ? popup
                            : QApplication::focusWidget();
    if (!receiver) {
        receiver = popup;
    }

    dispatchingPopupMenuKey_ = true;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(receiver, &press);
    dispatchingPopupMenuKey_ = false;
}

void MainWindow::dispatchEscapeKey()
{
    QWidget* receiver = QApplication::activePopupWidget();
    if (!receiver) {
        receiver = QApplication::activeModalWidget();
    }
    if (!receiver) {
        receiver = QApplication::focusWidget();
    }
    if (!receiver) {
        receiver = this;
    }

    dispatchingEscapeKey_ = true;
    QKeyEvent press(
        QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(receiver, &press);
    dispatchingEscapeKey_ = false;
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

void MainWindow::restorePendingPanelNavigation()
{
    const std::optional<PanelId> origin =
        std::exchange(pendingPanelNavigationOrigin_, std::nullopt);
    PlotWidget* pausedPoint =
        pendingPanelNavigationPausedPoint_.data();
    pendingPanelNavigationPausedPoint_.clear();
    if (!origin || origin->column < 0 || origin->row < 0
        || origin->column >= plotWidgets_.size()
        || origin->row >= plotWidgets_[origin->column].size()) {
        return;
    }
    selectPlot(origin->column, origin->row);
    if (pausedPoint && !activePointPlot_
        && currentInteractionMode_ == InteractionMode::Point) {
        pausedPointPlot_ = pausedPoint;
    }
    if (singlePanelMaximized_) {
        maximizeCurrentPanel();
    }
    focusSelectedPlot();
}

bool MainWindow::handleFixedPointKey(QKeyEvent* event,
                                     QWidget* target)
{
    if (!event || event->isAutoRepeat()
        || isInputWidget(target)
        || (event->modifiers() & ~Qt::KeypadModifier)
               != Qt::NoModifier) {
        return false;
    }
    if (!activePointPlot_
        || !activePointPlot_->pointTrackingActive()
        || event->key() < Qt::Key_1
        || event->key() > Qt::Key_9) {
        return false;
    }
    const int seriesIndex = event->key() - Qt::Key_1;
    if (!activePointPlot_->activatePointAtViewCenterForDataSeries(
            seriesIndex)) {
        setStatus(QStringLiteral("Visible Point curve %1 is unavailable")
                      .arg(seriesIndex + 1));
    }
    return true;
}

bool MainWindow::activatePointForCurrentPanel(int seriesIndex)
{
    PlotWidget* plot = currentPlotWidget();
    if (!plot) {
        movePanelSelection(0, 0);
        plot = currentPlotWidget();
    }
    if (!plot) {
        return false;
    }
    if (activePointPlot_ && activePointPlot_ != plot) {
        activePointPlot_->deactivatePointTracking();
    }
    if (pausedPointPlot_ && pausedPointPlot_ != plot) {
        pausedPointPlot_->deactivatePointTracking();
    }
    activePointPlot_ = plot;
    pausedPointPlot_ = nullptr;
    if (!plot->activatePointAtViewCenter(seriesIndex)) {
        activePointPlot_ = nullptr;
        setStatus(QStringLiteral("No point data is available in the current panel"));
        return false;
    }
    plot->setFocus(Qt::ShortcutFocusReason);
    plot->moveCursorToActivePoint();
    return true;
}

bool MainWindow::resumePausedPoint()
{
    if (currentInteractionMode_ != InteractionMode::Point
        || !pausedPointPlot_) {
        return false;
    }
    PlotWidget* plot = pausedPointPlot_;
    activePointPlot_ = plot;
    pausedPointPlot_ = nullptr;
    if (!plot->resumePointTracking()) {
        activePointPlot_ = nullptr;
        return false;
    }
    plot->setFocus(Qt::ShortcutFocusReason);
    plot->moveCursorToActivePoint();
    return true;
}

void MainWindow::pauseActivePointTracking()
{
    PlotWidget* plot = activePointPlot_;
    if (!plot) {
        return;
    }
    plot->pausePointTracking();
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
    pendingExactShortcut_.reset();
    pendingPanelNavigationOrigin_.reset();
    pendingPanelNavigationPausedPoint_.clear();
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
                   == ShortcutCommand::Escape;
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
                    == ShortcutCommand::Escape) {
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
    pendingExactShortcut_.reset();
    pendingPanelNavigationOrigin_.reset();
    pendingPanelNavigationPausedPoint_.clear();
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
    if (refreshAction_) {
        refreshAction_->setToolTip(
            withShortcut(QStringLiteral("Refresh data"),
                         ShortcutCommand::RefreshData));
    }
    if (exportAction_) {
        exportAction_->setToolTip(
            withShortcut(QStringLiteral("Export data"),
                         ShortcutCommand::GlobalExport));
    }
    if (layoutAction_) {
        layoutAction_->setToolTip(
            withShortcut(QStringLiteral("Layout setup"),
                         ShortcutCommand::GlobalLayout));
    }
    if (openButton_) {
        openButton_->setToolTip(
            withShortcut(QStringLiteral("Open configure file"),
                         ShortcutCommand::OpenFile));
    }
    if (recentEnvironmentButton_) {
        recentEnvironmentButton_->setToolTip(
            withShortcut(QStringLiteral("Recent configure files"),
                         ShortcutCommand::OpenRecentFiles));
    }
    if (internalWebButton_) {
        internalWebButton_->setToolTip(
            withShortcut(QStringLiteral("Internal web pages"),
                         ShortcutCommand::OpenWebMenu));
    }
    if (dataModeCombo_) {
        const int defaultIndex = dataModeCombo_->findData(
            static_cast<int>(defaultRateMode_));
        dataModeCombo_->setToolTip(
            withShortcut(
                QStringLiteral(
                    "Startup default: %1\nRight-click to set the current Rate as default")
                    .arg(defaultIndex >= 0
                             ? dataModeCombo_->itemText(defaultIndex)
                             : QStringLiteral("Thin")),
                ShortcutCommand::GlobalRate));
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
                QStringLiteral("pause tracking"),
                ShortcutCommand::Escape)
            + QStringLiteral("; select curve (1–9); ")
            + withShortcut(
                QStringLiteral("resume tracking"),
                ShortcutCommand::MenuActivate));
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

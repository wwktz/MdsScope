// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QKeySequence>
#include <QString>
#include <QVector>

enum class ShortcutCommand {
    OpenFile,
    OpenRecentFiles,
    OpenWebMenu,
    Save,
    GlobalRate,
    GlobalLayout,
    GlobalExport,
    PointMode,
    ZoomMode,
    FocusShot,
    ToggleRefresh,
    MaximizePanel,
    ResetCurrentScale,
    ResetAllScales,
    ShowAllPanels,
    SameXScale,
    SameYScale,
    PreviousShot,
    NextShot,
    LatestShot,
    PointPrevious,
    PointNext,
    PanelLeft,
    PanelDown,
    PanelUp,
    PanelRight,
    PanelRate,
    PanelSourceSetup,
    PanelExport,
    PanelSetup,
    ExitPoint,
    MenuLeft,
    MenuDown,
    MenuUp,
    MenuRight,
    MenuActivate,
};

struct ShortcutBinding {
    ShortcutCommand command;
    QString id;
    QString category;
    QString label;
    QKeySequence sequence;
    QKeySequence alternative;
};

QVector<ShortcutBinding> defaultShortcutBindings();
QVector<ShortcutBinding> loadShortcutBindings(const QString& rootPath);
void saveShortcutBindings(const QString& rootPath,
                          const QVector<ShortcutBinding>& bindings);
QString shortcutPlatformName();
QString shortcutPlatformDescription();
QString nativeShortcutText(const QKeySequence& sequence);

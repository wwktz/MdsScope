// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/app_paths.hpp"
#include "shortcut_settings.hpp"

#include <QSettings>

namespace {
QKeySequence shortcut(const char* portableText)
{
    return QKeySequence::fromString(
        QString::fromLatin1(portableText),
        QKeySequence::PortableText);
}

QString shortcutSettingsGroup()
{
    return QStringLiteral("shortcuts/%1")
        .arg(shortcutPlatformName().toLower());
}
}

QVector<ShortcutBinding> defaultShortcutBindings()
{
#ifdef Q_OS_LINUX
    const QKeySequence previousShot = shortcut("Ctrl+H");
    const QKeySequence nextShot = shortcut("Ctrl+L");
    const QKeySequence latestShot = shortcut("Ctrl+Shift+L");
    const QKeySequence pointPrevious = shortcut("H");
    const QKeySequence pointNext = shortcut("L");
    const QKeySequence panelLeft = shortcut("H");
    const QKeySequence panelDown = shortcut("J");
    const QKeySequence panelUp = shortcut("K");
    const QKeySequence panelRight = shortcut("L");
    const QKeySequence escape = shortcut("J, K");
    const QKeySequence escapeAlternative = shortcut("Esc");
    const QKeySequence menuLeft = shortcut("H");
    const QKeySequence menuDown = shortcut("J");
    const QKeySequence menuUp = shortcut("K");
    const QKeySequence menuRight = shortcut("L");
#else
    const QKeySequence previousShot = shortcut("Ctrl+Left");
    const QKeySequence nextShot = shortcut("Ctrl+Right");
    const QKeySequence latestShot = shortcut("Ctrl+Shift+Right");
    const QKeySequence pointPrevious = shortcut("Left");
    const QKeySequence pointNext = shortcut("Right");
    const QKeySequence panelLeft = shortcut("Left");
    const QKeySequence panelDown = shortcut("Down");
    const QKeySequence panelUp = shortcut("Up");
    const QKeySequence panelRight = shortcut("Right");
    const QKeySequence escape = shortcut("Esc");
    const QKeySequence escapeAlternative;
    const QKeySequence menuLeft = shortcut("Left");
    const QKeySequence menuDown = shortcut("Down");
    const QKeySequence menuUp = shortcut("Up");
    const QKeySequence menuRight = shortcut("Right");
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt maps Ctrl to Command and Meta to the physical Control key on macOS.
    // Command+M is reserved by the system for minimizing the window.
    const QKeySequence maximizePanel = shortcut("Meta+M");
#else
    const QKeySequence maximizePanel = shortcut("Ctrl+M");
#endif

    return {
        {ShortcutCommand::OpenFile, "open_file", "General", "Open file", shortcut("Ctrl+O"), {}},
        {ShortcutCommand::OpenRecentFiles, "open_recent_files", "General", "Open recent files", shortcut("Ctrl+Shift+O"), {}},
        {ShortcutCommand::OpenWebMenu, "open_web_menu", "General", "Open web menu", shortcut("Ctrl+W"), {}},
        {ShortcutCommand::Save, "save", "General", "Save", shortcut("Ctrl+S"), {}},
        {ShortcutCommand::GlobalRate, "global_rate", "Global", "Global Rate", shortcut("Ctrl+G, R"), {}},
        {ShortcutCommand::GlobalLayout, "global_layout", "Global", "Layout setup", shortcut("Ctrl+G, L"), {}},
        {ShortcutCommand::GlobalExport, "global_export", "Global", "Export data", shortcut("Ctrl+E"), {}},
        {ShortcutCommand::PointMode, "point_mode", "General", "Point mode", shortcut("Ctrl+P"), {}},
        {ShortcutCommand::ZoomMode, "zoom_mode", "General", "Zoom / Move mode", shortcut("Ctrl+Z"), {}},
        {ShortcutCommand::FocusShot, "focus_shot", "General", "Focus shot input", shortcut("I"), {}},
        {ShortcutCommand::RefreshData, "refresh_data", "General", "Refresh data", shortcut("Ctrl+Shift+R"), {}},
        {ShortcutCommand::ToggleRefresh, "toggle_refresh", "General", "Stop / Continue", shortcut("Space"), {}},
        {ShortcutCommand::MenuActivate, "menu_activate", "General", "Enter / activate", shortcut("Enter"), {}},
        // Keep the historical settings id so existing custom bindings remain
        // valid now that this command behaves as a general Escape equivalent.
        {ShortcutCommand::Escape, "exit_point", "General", "Escape / cancel", escape, escapeAlternative},

        {ShortcutCommand::MaximizePanel, "maximize_panel", "Panel", "Maximize panel", maximizePanel, {}},
        {ShortcutCommand::ResetCurrentScale, "reset_current_scale", "Panel", "Reset current scale", shortcut("Ctrl+R, C"), {}},
        {ShortcutCommand::ResetAllScales, "reset_all_scales", "Panel", "Reset all scales", shortcut("Ctrl+R, A"), {}},
        {ShortcutCommand::ShowAllPanels, "show_all_panels", "Panel", "Show all panels", shortcut("Ctrl+A"), {}},
        {ShortcutCommand::SameXScale, "same_x_scale", "Panel", "All same X scale", shortcut("Ctrl+X"), {}},
        {ShortcutCommand::SameYScale, "same_y_scale", "Panel", "All same Y scale", shortcut("Ctrl+Y"), {}},

        {ShortcutCommand::PreviousShot, "previous_shot", "Shot", "Previous shot", previousShot, {}},
        {ShortcutCommand::NextShot, "next_shot", "Shot", "Next shot", nextShot, {}},
        {ShortcutCommand::LatestShot, "latest_shot", "Shot", "Latest shot", latestShot, {}},

        {ShortcutCommand::PointPrevious, "point_previous", "Point tracking", "Move Point left", pointPrevious, {}},
        {ShortcutCommand::PointNext, "point_next", "Point tracking", "Move Point right", pointNext, {}},

        {ShortcutCommand::PanelLeft, "panel_left", "Panel navigation", "Select panel left", panelLeft, {}},
        {ShortcutCommand::PanelDown, "panel_down", "Panel navigation", "Select panel down", panelDown, {}},
        {ShortcutCommand::PanelUp, "panel_up", "Panel navigation", "Select panel up", panelUp, {}},
        {ShortcutCommand::PanelRight, "panel_right", "Panel navigation", "Select panel right", panelRight, {}},

        {ShortcutCommand::PanelRate, "panel_rate", "Current panel", "Rate", shortcut("Ctrl+T, R"), {}},
        {ShortcutCommand::PanelSourceSetup, "panel_source_setup", "Current panel", "Source setup", shortcut("Ctrl+T, S"), {}},
        {ShortcutCommand::PanelExport, "panel_export", "Current panel", "Export", shortcut("Ctrl+T, E"), {}},
        {ShortcutCommand::PanelSetup, "panel_setup", "Current panel", "Panel setup", shortcut("Ctrl+T, P"), {}},

        {ShortcutCommand::MenuLeft, "menu_left", "Popup menu navigation", "Move left / close submenu", menuLeft, {}},
        {ShortcutCommand::MenuDown, "menu_down", "Popup menu navigation", "Move down", menuDown, {}},
        {ShortcutCommand::MenuUp, "menu_up", "Popup menu navigation", "Move up", menuUp, {}},
        {ShortcutCommand::MenuRight, "menu_right", "Popup menu navigation", "Move right / open submenu", menuRight, {}},
    };
}

QVector<ShortcutBinding> loadShortcutBindings(const QString& rootPath)
{
    QVector<ShortcutBinding> bindings = defaultShortcutBindings();
    QSettings settings(uiSettingsPath(rootPath), QSettings::IniFormat);
    settings.beginGroup(shortcutSettingsGroup());
    const bool hasRefreshBinding =
        settings.contains(QStringLiteral("refresh_data"));
    for (ShortcutBinding& binding : bindings) {
        if (!settings.contains(binding.id)) {
            continue;
        }
        const QKeySequence storedSequence = QKeySequence::fromString(
            settings.value(binding.id).toString(),
            QKeySequence::PortableText);
        // Migrate earlier Reset All defaults to the current Reset-then-All
        // sequence while preserving every other customized binding.
        const bool outdatedResetAllDefault =
            binding.command == ShortcutCommand::ResetAllScales
            && ((!hasRefreshBinding
                && storedSequence == shortcut("Ctrl+Shift+R"))
                || storedSequence == shortcut("Ctrl+A, R")
                || storedSequence == shortcut("Ctrl+G, A"));
        const bool outdatedResetCurrentDefault =
            binding.command == ShortcutCommand::ResetCurrentScale
            && storedSequence == shortcut("Ctrl+R");
        if (!outdatedResetAllDefault
            && !outdatedResetCurrentDefault) {
            binding.sequence = storedSequence;
        }
        const QKeySequence storedAlternative = QKeySequence::fromString(
            settings.value(binding.id + QStringLiteral("_alternative"))
                .toString(),
            QKeySequence::PortableText);
#ifdef Q_OS_LINUX
        const bool legacyEscapeDefault =
            binding.command == ShortcutCommand::Escape
            && storedSequence == shortcut("J, K")
            && storedAlternative.isEmpty();
        if (!legacyEscapeDefault) {
            binding.alternative = storedAlternative;
        }
#else
        binding.alternative = storedAlternative;
#endif
    }
    settings.endGroup();
    return bindings;
}

void saveShortcutBindings(const QString& rootPath,
                          const QVector<ShortcutBinding>& bindings)
{
    QSettings settings(uiSettingsPath(rootPath), QSettings::IniFormat);
    settings.beginGroup(shortcutSettingsGroup());
    for (const ShortcutBinding& binding : bindings) {
        settings.setValue(
            binding.id,
            binding.sequence.toString(QKeySequence::PortableText));
        settings.setValue(
            binding.id + QStringLiteral("_alternative"),
            binding.alternative.toString(QKeySequence::PortableText));
    }
    settings.endGroup();
}

QString shortcutPlatformName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("macOS");
#else
    return QStringLiteral("Linux");
#endif
}

QString shortcutPlatformDescription()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "Windows defaults use arrow keys and Esc. You can optionally add one alternative shortcut per action.");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral(
        "macOS defaults use native Command shortcuts, arrow keys, and Esc. You can optionally add one alternative shortcut per action.");
#else
    return QStringLiteral(
        "Linux defaults use Vim-style H/J/K/L navigation, including in popup menus, and J, K as an Escape equivalent. You can optionally add one alternative shortcut per action.");
#endif
}

QString nativeShortcutText(const QKeySequence& sequence)
{
    const QString text = sequence.toString(QKeySequence::NativeText);
    return text.isEmpty() ? QStringLiteral("None") : text;
}

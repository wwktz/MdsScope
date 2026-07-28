// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "shortcut_settings.hpp"

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
    const QKeySequence exitPoint = shortcut("J, K");
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
    const QKeySequence exitPoint = shortcut("Esc");
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt maps Ctrl to Command and Meta to the physical Control key on macOS.
    // Command+M is reserved by the system for minimizing the window.
    const QKeySequence maximizePanel = shortcut("Meta+M");
#else
    const QKeySequence maximizePanel = shortcut("Ctrl+M");
#endif

    return {
        {ShortcutCommand::Save, "save", "General", "Save", shortcut("Ctrl+S"), {}},
        {ShortcutCommand::PointMode, "point_mode", "General", "Point mode", shortcut("Ctrl+P"), {}},
        {ShortcutCommand::ZoomMode, "zoom_mode", "General", "Zoom / Move mode", shortcut("Ctrl+Z"), {}},
        {ShortcutCommand::FocusShot, "focus_shot", "General", "Focus shot input", shortcut("I"), {}},
        {ShortcutCommand::ToggleRefresh, "toggle_refresh", "General", "Stop / Continue", shortcut("Space"), {}},

        {ShortcutCommand::MaximizePanel, "maximize_panel", "Panel", "Maximize panel", maximizePanel, {}},
        {ShortcutCommand::ResetCurrentScale, "reset_current_scale", "Panel", "Reset current scale", shortcut("Ctrl+R"), {}},
        {ShortcutCommand::ResetAllScales, "reset_all_scales", "Panel", "Reset all scales", shortcut("Ctrl+Shift+R"), {}},
        {ShortcutCommand::ShowAllPanels, "show_all_panels", "Panel", "Show all panels", shortcut("Ctrl+A"), {}},
        {ShortcutCommand::SameXScale, "same_x_scale", "Panel", "All same X scale", shortcut("Ctrl+X"), {}},
        {ShortcutCommand::SameYScale, "same_y_scale", "Panel", "All same Y scale", shortcut("Ctrl+Y"), {}},

        {ShortcutCommand::PreviousShot, "previous_shot", "Shot", "Previous shot", previousShot, {}},
        {ShortcutCommand::NextShot, "next_shot", "Shot", "Next shot", nextShot, {}},
        {ShortcutCommand::LatestShot, "latest_shot", "Shot", "Latest shot", latestShot, {}},

        {ShortcutCommand::PointPrevious, "point_previous", "Point tracking", "Move Point left", pointPrevious, {}},
        {ShortcutCommand::PointNext, "point_next", "Point tracking", "Move Point right", pointNext, {}},
        {ShortcutCommand::ExitPoint, "exit_point", "Point tracking", "Exit Point tracking", exitPoint, {}},

        {ShortcutCommand::PanelLeft, "panel_left", "Panel navigation", "Select panel left", panelLeft, {}},
        {ShortcutCommand::PanelDown, "panel_down", "Panel navigation", "Select panel down", panelDown, {}},
        {ShortcutCommand::PanelUp, "panel_up", "Panel navigation", "Select panel up", panelUp, {}},
        {ShortcutCommand::PanelRight, "panel_right", "Panel navigation", "Select panel right", panelRight, {}},
    };
}

QVector<ShortcutBinding> loadShortcutBindings(const QString& rootPath)
{
    QVector<ShortcutBinding> bindings = defaultShortcutBindings();
    QSettings settings(uiSettingsPath(rootPath), QSettings::IniFormat);
    settings.beginGroup(shortcutSettingsGroup());
    for (ShortcutBinding& binding : bindings) {
        if (!settings.contains(binding.id)) {
            continue;
        }
        binding.sequence = QKeySequence::fromString(
            settings.value(binding.id).toString(),
            QKeySequence::PortableText);
        binding.alternative = QKeySequence::fromString(
            settings.value(binding.id + QStringLiteral("_alternative"))
                .toString(),
            QKeySequence::PortableText);
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
        "Linux defaults use Vim-style H/J/K/L navigation and J, K to exit Point tracking. You can optionally add one alternative shortcut per action.");
#endif
}

QString nativeShortcutText(const QKeySequence& sequence)
{
    const QString text = sequence.toString(QKeySequence::NativeText);
    return text.isEmpty() ? QStringLiteral("None") : text;
}

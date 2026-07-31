// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/main_window/shortcut_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <utility>

QString uiSettingsPath(const QString& rootPath)
{
    return QDir(rootPath).filePath(QStringLiteral("ui.ini"));
}

namespace {
const ShortcutBinding* bindingFor(
    const QVector<ShortcutBinding>& bindings,
    ShortcutCommand command)
{
    const auto it = std::find_if(
        bindings.cbegin(),
        bindings.cend(),
        [command](const ShortcutBinding& binding) {
            return binding.command == command;
        });
    return it == bindings.cend() ? nullptr : &*it;
}

bool expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical("%s", message);
    }
    return condition;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QVector<ShortcutBinding> bindings =
        defaultShortcutBindings();
    bool ok = true;
    ok &= expect(bindings.size() == 36,
                 "unexpected shortcut count");
    for (const ShortcutBinding& binding : std::as_const(bindings)) {
        ok &= expect(!binding.sequence.isEmpty(),
                     "default shortcut is empty");
        ok &= expect(binding.alternative.isEmpty(),
                     "default alternative should be empty");
    }

    const ShortcutBinding* panelLeft =
        bindingFor(bindings, ShortcutCommand::PanelLeft);
    const ShortcutBinding* exitPoint =
        bindingFor(bindings, ShortcutCommand::ExitPoint);
    const ShortcutBinding* previousShot =
        bindingFor(bindings, ShortcutCommand::PreviousShot);
    const ShortcutBinding* pointPrevious =
        bindingFor(bindings, ShortcutCommand::PointPrevious);
    const ShortcutBinding* globalRate =
        bindingFor(bindings, ShortcutCommand::GlobalRate);
    const ShortcutBinding* panelRate =
        bindingFor(bindings, ShortcutCommand::PanelRate);
    const ShortcutBinding* menuDown =
        bindingFor(bindings, ShortcutCommand::MenuDown);
    ok &= expect(panelLeft && exitPoint && previousShot
                     && pointPrevious && globalRate && panelRate
                     && menuDown,
                 "missing platform shortcut");
    ok &= expect(
        globalRate->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Ctrl+G, R"),
        "global Rate shortcut should use the global prefix");
    ok &= expect(
        panelRate->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Ctrl+T, R"),
        "panel Rate shortcut should use the current-panel prefix");
#ifdef Q_OS_LINUX
    ok &= expect(
        menuDown->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("J"),
        "Linux should default to J popup-menu navigation");
    ok &= expect(
        panelLeft->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("H"),
        "Linux should default to H panel navigation");
    ok &= expect(
        exitPoint->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("J, K"),
        "Linux should default to J, K Point exit");
    ok &= expect(
        previousShot->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Ctrl+H"),
        "Linux should default to Ctrl+H previous shot");
    ok &= expect(
        pointPrevious->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("H"),
        "Linux should default to H Point movement");
#else
    ok &= expect(
        panelLeft->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Left"),
        "non-Linux platforms should default to arrow navigation");
    ok &= expect(
        exitPoint->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Esc"),
        "non-Linux platforms should default to Esc Point exit");
    ok &= expect(
        pointPrevious->sequence.toString(QKeySequence::PortableText)
            == QStringLiteral("Left"),
        "non-Linux platforms should default to arrow Point movement");
#endif

    QTemporaryDir temporary;
    ok &= expect(temporary.isValid(),
                 "could not create temporary settings directory");
    if (temporary.isValid()) {
        bindings[0].alternative =
            QKeySequence::fromString(
                QStringLiteral("Ctrl+Shift+S"),
                QKeySequence::PortableText);
        saveShortcutBindings(temporary.path(), bindings);
        const QVector<ShortcutBinding> loaded =
            loadShortcutBindings(temporary.path());
        ok &= expect(
            loaded[0].alternative == bindings[0].alternative,
            "alternative shortcut did not persist");
    }
    return ok ? 0 : 1;
}

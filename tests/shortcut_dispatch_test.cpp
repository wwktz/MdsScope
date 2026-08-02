// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/main_window/main_window.hpp"
#include "ui/plot/plot_widget.hpp"

#include <QApplication>
#include <QDialog>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <algorithm>
#include <cmath>

class ShortcutDispatchTestAccess {
public:
    static void configure(MainWindow& window,
                          PlotWidget* first,
                          PlotWidget* second,
                          PlotWidget* third)
    {
        window.config_.columns = {
            {first->spec(), third->spec()},
            {second->spec()},
        };
        window.displayConfig_ = window.config_;
        window.plotWidgets_ = {{first, third}, {second}};
        window.selectedColumn_ = 0;
        window.selectedRow_ = 0;
        window.currentInteractionMode_ = InteractionMode::Point;
        if (window.pointButton_) {
            window.pointButton_->setChecked(true);
        }
        if (window.zoomButton_) {
            window.zoomButton_->setChecked(false);
        }
        window.shortcutBindings_ = defaultShortcutBindings();
        auto sequence = [](const char* text) {
            return QKeySequence::fromString(
                QString::fromLatin1(text),
                QKeySequence::PortableText);
        };
        for (ShortcutBinding& binding : window.shortcutBindings_) {
            switch (binding.command) {
            case ShortcutCommand::Escape:
                binding.sequence = sequence("J, K");
                binding.alternative = sequence("Esc");
                break;
            case ShortcutCommand::MenuActivate:
                binding.sequence = sequence("Enter");
                binding.alternative = sequence("A");
                break;
            case ShortcutCommand::PointPrevious:
            case ShortcutCommand::PanelLeft:
            case ShortcutCommand::MenuLeft:
                binding.sequence = sequence("H");
                break;
            case ShortcutCommand::PanelDown:
            case ShortcutCommand::MenuDown:
                binding.sequence = sequence("J");
                break;
            case ShortcutCommand::PanelUp:
            case ShortcutCommand::MenuUp:
                binding.sequence = sequence("K");
                break;
            case ShortcutCommand::PointNext:
            case ShortcutCommand::PanelRight:
            case ShortcutCommand::MenuRight:
                binding.sequence = sequence("L");
                break;
            default:
                break;
            }
        }
        for (PlotWidget* plot : {first, second, third}) {
            QObject::connect(
                plot,
                &PlotWidget::pointTrackingPaused,
                &window,
                [&window, plot] {
                    if (window.activePointPlot_ == plot) {
                        window.activePointPlot_ = nullptr;
                        window.pausedPointPlot_ = plot;
                    }
                    window.pointSyncSource_ = nullptr;
                    window.pointSyncQueued_ = false;
                    window.pendingPointX_ = qQNaN();
                    ++window.pointSyncGeneration_;
                });
        }
        window.selectPlot(0, 0);
    }

    static bool activateCurrent(MainWindow& window)
    {
        return window.activatePointForCurrentPanel();
    }

    static void setPointMode(MainWindow& window)
    {
        window.setInteractionMode(InteractionMode::Point);
    }

    static void setZoomMode(MainWindow& window)
    {
        window.setInteractionMode(InteractionMode::Zoom);
    }

    static void select(MainWindow& window, int column, int row)
    {
        window.selectPlot(column, row);
    }

    static PlotWidget* active(const MainWindow& window)
    {
        return window.activePointPlot_;
    }

    static PlotWidget* paused(const MainWindow& window)
    {
        return window.pausedPointPlot_;
    }

    static int selectedColumn(const MainWindow& window)
    {
        return window.selectedColumn_;
    }

    static int selectedRow(const MainWindow& window)
    {
        return window.selectedRow_;
    }

    static QWidget* pointButton(MainWindow& window)
    {
        return window.pointButton_;
    }

    static QLineEdit* shotEdit(MainWindow& window)
    {
        return window.shotEdit_;
    }

    static PlotWidget* rebuildConnectedPlot(MainWindow& window)
    {
        PlotSpec spec;
        spec.signalSpecs.resize(1);
        spec.signalSpecs[0].yExpr = QStringLiteral("connected");
        window.config_.columns = {{spec}};
        window.rebuildGrid();
        if (window.plotWidgets_.isEmpty()
            || window.plotWidgets_.first().isEmpty()) {
            return nullptr;
        }
        PlotWidget* plot = window.plotWidgets_.first().first();
        plot->resize(640, 360);
        SignalSeries series;
        series.uniformStart = 0.0;
        series.uniformStep = 1.0;
        series.uniformY = {1.0F, 2.0F, 3.0F};
        series.uniformMinY = 1.0;
        series.uniformMaxY = 3.0;
        plot->setSeries(0, std::move(series));
        window.setInteractionMode(InteractionMode::Point);
        window.selectPlot(0, 0);
        return plot;
    }

    static bool markerVisible(const PlotWidget& plot)
    {
        return !plot.hoverText_.isEmpty()
               && plot.hoverSeriesIndex_ >= 0
               && plot.syncedPoint_.visible;
    }
};

namespace {
void sendKey(QWidget* target,
             Qt::Key key,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             const QString& text = {})
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(target, &event);
}

bool expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical("%s", message);
    }
    return condition;
}

void configurePlot(PlotWidget* plot,
                   const QStringList& labels,
                   const QVector<int>& dataIndexes)
{
    plot->resize(640, 360);
    PlotSpec spec;
    spec.signalSpecs.resize(labels.size());
    for (int i = 0; i < labels.size(); ++i) {
        spec.signalSpecs[i].yExpr = labels[i];
    }
    plot->setSpec(spec);
    plot->setInteractionMode(InteractionMode::Point);
    for (int index : dataIndexes) {
        SignalSeries series;
        series.uniformStart = index * 10.0;
        series.uniformStep = 1.0;
        series.uniformY = {1.0F, 2.0F, 3.0F};
        series.uniformMinY = 1.0;
        series.uniformMaxY = 3.0;
        plot->setSeries(index, std::move(series));
    }
    plot->resetScale();
}
}

int main(int argc, char** argv)
{
    QTemporaryDir configHome;
    if (!configHome.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QApplication app(argc, argv);
    MainWindow window(configHome.path());
    auto* first = new PlotWidget(&window);
    auto* second = new PlotWidget(&window);
    auto* third = new PlotWidget(&window);
    configurePlot(
        first,
        {QStringLiteral("empty"),
         QStringLiteral("first"),
         QStringLiteral("second")},
        {1, 2});
    configurePlot(second, {QStringLiteral("other")}, {0});
    configurePlot(third, {QStringLiteral("third")}, {0});
    ShortcutDispatchTestAccess::configure(
        window, first, second, third);

    bool ok = true;
    ok &= expect(
        ShortcutDispatchTestAccess::activateCurrent(window),
        "Ctrl+P-equivalent activation failed");
    const double originalX = first->activePointX();
    sendKey(first, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    sendKey(first, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(
        !first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == nullptr
            && ShortcutDispatchTestAccess::paused(window) == first
            && std::abs(first->activePointX() - originalX) < 1e-9,
        "J,K did not pause Point while retaining its data position");

    sendKey(first, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedRow(window) == 1
            && ShortcutDispatchTestAccess::paused(window) == nullptr,
        "J did not navigate immediately after pausing Point");
    sendKey(third, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedRow(window) == 0
            && ShortcutDispatchTestAccess::paused(window) == first
            && ShortcutDispatchTestAccess::markerVisible(*first),
        "J,K rollback did not restore the paused Point panel");

    sendKey(first, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    ok &= expect(
        first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == first
            && ShortcutDispatchTestAccess::paused(window) == nullptr
            && std::abs(first->activePointX() - originalX) < 1e-9,
        "A alternative did not resume the paused Point");

    sendKey(first, Qt::Key_Escape);
    sendKey(first, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    ok &= expect(
        first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == first
            && ShortcutDispatchTestAccess::paused(window) == nullptr
            && std::abs(first->activePointX() - originalX) < 1e-9,
        "Return primary did not resume the paused Point");

    sendKey(first, Qt::Key_Escape);
    sendKey(first, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedColumn(window) == 0
            && ShortcutDispatchTestAccess::selectedRow(window) == 1
            && ShortcutDispatchTestAccess::paused(window) == nullptr
            && ShortcutDispatchTestAccess::markerVisible(*first),
        "standalone J did not navigate immediately while preserving the marker");
    sendKey(third, Qt::Key_Return,
            Qt::NoModifier, QStringLiteral("\r"));
    ok &= expect(
        third->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == third,
        "Return did not activate the panel reached by standalone J");
    sendKey(third, Qt::Key_Escape);
    sendKey(third, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    sendKey(first, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    ok &= expect(
        first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == first,
        "A did not activate the panel reached by standalone K");

    sendKey(first, Qt::Key_Escape);
    ok &= expect(
        ShortcutDispatchTestAccess::markerVisible(*first),
        "pausing Point erased its marker");
    sendKey(first, Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedColumn(window) == 1
            && ShortcutDispatchTestAccess::selectedRow(window) == 0
            && ShortcutDispatchTestAccess::paused(window) == nullptr
            && ShortcutDispatchTestAccess::markerVisible(*first),
        "changing panels erased the paused marker");
    sendKey(second, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    ok &= expect(
        second->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == second,
        "A did not activate Point on the newly selected panel");

    sendKey(second, Qt::Key_Escape);
    sendKey(second, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    sendKey(first, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    ok &= expect(
        first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == first,
        "Return did not activate Point on the newly selected panel");
    sendKey(first, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
    ok &= expect(
        first->activePointSeriesIndex() == 1,
        "digit 1 did not select the first visible data series");
    sendKey(first, Qt::Key_2, Qt::NoModifier, QStringLiteral("2"));
    ok &= expect(
        first->activePointSeriesIndex() == 2,
        "digit 2 did not select the second visible data series");

    ShortcutDispatchTestAccess::setZoomMode(window);
    ShortcutDispatchTestAccess::select(window, 0, 0);
    sendKey(first, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedRow(window) == 1,
        "standalone J did not navigate down immediately");
    sendKey(third, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(
        ShortcutDispatchTestAccess::selectedColumn(window) == 0
            && ShortcutDispatchTestAccess::selectedRow(window) == 0,
        "J,K Escape did not roll back speculative panel navigation");

    ShortcutDispatchTestAccess::setPointMode(window);
    ShortcutDispatchTestAccess::select(window, 0, 0);
    sendKey(ShortcutDispatchTestAccess::pointButton(window),
            Qt::Key_Return,
            Qt::NoModifier,
            QStringLiteral("\r"));
    ok &= expect(
        first->pointTrackingActive()
            && ShortcutDispatchTestAccess::active(window) == first,
        "Return differed from its alternative while a button had focus");

    ShortcutDispatchTestAccess::setZoomMode(window);
    QLineEdit input(&window);
    sendKey(&input, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    ok &= expect(input.text() == QStringLiteral("a"),
                 "plain A was stolen from a text input");

    QLineEdit* shotEdit = ShortcutDispatchTestAccess::shotEdit(window);
    shotEdit->setText(QStringLiteral("123"));
    QFocusEvent focusIn(QEvent::FocusIn);
    QApplication::sendEvent(shotEdit, &focusIn);
    shotEdit->setText(QStringLiteral("456"));
    sendKey(shotEdit, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    sendKey(shotEdit, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(
        shotEdit->text() == QStringLiteral("123"),
        "J,K Escape did not cancel an uncommitted shot edit");

    PlotWidget* connected =
        ShortcutDispatchTestAccess::rebuildConnectedPlot(window);
    ok &= expect(connected != nullptr,
                 "could not build a connected production plot");
    if (connected) {
        connected->selected();
        ok &= expect(
            ShortcutDispatchTestAccess::active(window) == nullptr,
            "selecting a panel created a false active Point state");
        connected->activatePointAtViewCenter();
        ok &= expect(
            ShortcutDispatchTestAccess::active(window) == connected,
            "a real mouse-style data point did not take ownership");
    }

    bool popupActivated = false;
    QMenu menu(&window);
    QAction* action = menu.addAction(QStringLiteral("Action"));
    QObject::connect(action, &QAction::triggered, [&popupActivated] {
        popupActivated = true;
    });
    menu.popup(QPoint(0, 0));
    QApplication::processEvents();
    menu.setActiveAction(action);
    sendKey(&menu, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    ok &= expect(popupActivated,
                 "A alternative did not activate a popup-menu item");

    QMenu escapeMenu(&window);
    escapeMenu.addAction(QStringLiteral("First"));
    escapeMenu.addAction(QStringLiteral("Second"));
    escapeMenu.popup(QPoint(0, 0));
    QApplication::processEvents();
    sendKey(&escapeMenu, Qt::Key_J,
            Qt::NoModifier, QStringLiteral("j"));
    sendKey(&escapeMenu, Qt::Key_K,
            Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(!escapeMenu.isVisible(),
                 "J,K Escape did not close a popup menu");

    QDialog modal(&window);
    modal.setModal(true);
    modal.show();
    QApplication::processEvents();
    sendKey(&modal, Qt::Key_J,
            Qt::NoModifier, QStringLiteral("j"));
    sendKey(&modal, Qt::Key_K,
            Qt::NoModifier, QStringLiteral("k"));
    ok &= expect(!modal.isVisible(),
                 "J,K Escape did not close a modal dialog");

    return ok ? 0 : 2;
}

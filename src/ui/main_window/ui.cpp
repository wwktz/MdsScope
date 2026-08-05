// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/api_auth.hpp"
#include "core/app_paths.hpp"
#include "core/mds_helpers.hpp"
#include "ui/visuals.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "shot_workflow.hpp"
#include "ui/plot/plot_widget.hpp"
#include "shared.hpp"
#include "theme.hpp"
#include "user_preferences.hpp"
#include "ssh/ssh_tunnel_manager.hpp"

#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProxyStyle>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>

namespace {

enum class ShotControlGlyph {
    Apply,
    Stop,
    Continue,
    Previous,
    Next,
    Latest,
};

QPixmap shotControlPixmap(ShotControlGlyph glyph, const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);
    painter.setPen(
        QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);

    switch (glyph) {
    case ShotControlGlyph::Apply: {
        QPainterPath check;
        check.moveTo(4.8, 12.4);
        check.lineTo(9.8, 17.2);
        check.lineTo(19.3, 6.8);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(check);
        break;
    }
    case ShotControlGlyph::Stop:
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(7.0, 7.0, 10.0, 10.0), 1.0, 1.0);
        break;
    case ShotControlGlyph::Continue:
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF{
            QPointF(7.5, 5.8),
            QPointF(18.6, 12.0),
            QPointF(7.5, 18.2),
        });
        break;
    case ShotControlGlyph::Previous:
        painter.drawLine(QPointF(6.3, 6.8), QPointF(6.3, 17.2));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF{
            QPointF(17.9, 6.2),
            QPointF(8.8, 12.0),
            QPointF(17.9, 17.8),
        });
        break;
    case ShotControlGlyph::Next:
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygonF{
            QPointF(6.1, 6.2),
            QPointF(15.2, 12.0),
            QPointF(6.1, 17.8),
        });
        painter.setPen(
            QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(17.7, 6.8), QPointF(17.7, 17.2));
        break;
    case ShotControlGlyph::Latest: {
        QPainterPath chevron;
        chevron.moveTo(7.0, 6.5);
        chevron.lineTo(13.0, 12.0);
        chevron.lineTo(7.0, 17.5);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(chevron);
        painter.drawLine(QPointF(17.5, 6.8), QPointF(17.5, 17.2));
        break;
    }
    }
    return pixmap;
}

QIcon shotControlIcon(ShotControlGlyph glyph)
{
    const QPalette palette = QApplication::palette();
    QIcon icon;
    icon.addPixmap(
        shotControlPixmap(glyph, palette.color(QPalette::ButtonText)),
        QIcon::Normal);
    icon.addPixmap(
        shotControlPixmap(
            glyph,
            palette.color(QPalette::Disabled, QPalette::ButtonText)),
        QIcon::Disabled);
    return icon;
}

QPixmap keyboardShortcutPixmap(const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);
    painter.setPen(
        QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(
        QRectF(2.8, 5.0, 18.4, 14.0), 2.2, 2.2);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const qreal y : {8.2, 11.6}) {
        for (const qreal x : {6.0, 9.4, 12.8, 16.2, 19.0}) {
            painter.drawRoundedRect(
                QRectF(x - 0.85, y - 0.75, 1.7, 1.5),
                0.35,
                0.35);
        }
    }
    painter.drawRoundedRect(
        QRectF(7.0, 15.0, 10.0, 1.6), 0.55, 0.55);
    return pixmap;
}

QIcon keyboardShortcutIcon()
{
    const QPalette palette = QApplication::palette();
    QIcon icon;
    icon.addPixmap(
        keyboardShortcutPixmap(
            palette.color(QPalette::ButtonText)),
        QIcon::Normal);
    icon.addPixmap(
        keyboardShortcutPixmap(
            palette.color(
                QPalette::Disabled,
                QPalette::ButtonText)),
        QIcon::Disabled);
    return icon;
}

enum class PanelMenuGlyph {
    Maximize,
    ShowAll,
    ResetCurrent,
    ResetAll,
    SameX,
    SameY,
    Rate,
    Export,
    DataSource,
    PanelSetup,
};

QPixmap panelMenuPixmap(PanelMenuGlyph glyph, const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);
    painter.setPen(
        QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    auto drawDoubleArrow = [&painter](
                               const QPointF& start,
                               const QPointF& end,
                               bool horizontal) {
        painter.drawLine(start, end);
        if (horizontal) {
            painter.drawLine(start, start + QPointF(2.6, -2.2));
            painter.drawLine(start, start + QPointF(2.6, 2.2));
            painter.drawLine(end, end + QPointF(-2.6, -2.2));
            painter.drawLine(end, end + QPointF(-2.6, 2.2));
        } else {
            painter.drawLine(start, start + QPointF(-2.2, 2.6));
            painter.drawLine(start, start + QPointF(2.2, 2.6));
            painter.drawLine(end, end + QPointF(-2.2, -2.6));
            painter.drawLine(end, end + QPointF(2.2, -2.6));
        }
    };
    auto drawResetArrow = [&painter, &color] {
        painter.setPen(
            QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(3.5, 3.5, 17.0, 17.0),
                        28 * 16,
                        304 * 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(QPolygonF{
            QPointF(23.0, 10.5),
            QPointF(18.4, 1.8),
            QPointF(13.8, 7.6),
        });
    };

    switch (glyph) {
    case PanelMenuGlyph::Maximize:
        painter.drawLine(QPointF(4.0, 9.0), QPointF(4.0, 4.0));
        painter.drawLine(QPointF(4.0, 4.0), QPointF(9.0, 4.0));
        painter.drawLine(QPointF(15.0, 4.0), QPointF(20.0, 4.0));
        painter.drawLine(QPointF(20.0, 4.0), QPointF(20.0, 9.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(4.0, 20.0));
        painter.drawLine(QPointF(4.0, 20.0), QPointF(9.0, 20.0));
        painter.drawLine(QPointF(15.0, 20.0), QPointF(20.0, 20.0));
        painter.drawLine(QPointF(20.0, 20.0), QPointF(20.0, 15.0));
        break;
    case PanelMenuGlyph::ShowAll:
        for (int y : {4, 13}) {
            for (int x : {4, 13}) {
                painter.drawRoundedRect(
                    QRectF(x, y, 7.0, 7.0), 1.2, 1.2);
            }
        }
        break;
    case PanelMenuGlyph::ResetCurrent: {
        drawResetArrow();
        break;
    }
    case PanelMenuGlyph::ResetAll: {
        drawResetArrow();
        for (const QPointF& center :
             {QPointF(10.0, 10.0),
              QPointF(14.0, 10.0),
              QPointF(10.0, 14.0),
              QPointF(14.0, 14.0)}) {
            painter.drawEllipse(center, 1.1, 1.1);
        }
        break;
    }
    case PanelMenuGlyph::SameX:
        drawDoubleArrow(
            QPointF(4.5, 12.0), QPointF(19.5, 12.0), true);
        break;
    case PanelMenuGlyph::SameY:
        drawDoubleArrow(
            QPointF(12.0, 4.5), QPointF(12.0, 19.5), false);
        break;
    case PanelMenuGlyph::Rate:
        painter.setPen(
            QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(6.0, 18.0), QPointF(6.0, 14.0));
        painter.drawLine(QPointF(12.0, 18.0), QPointF(12.0, 9.0));
        painter.drawLine(QPointF(18.0, 18.0), QPointF(18.0, 5.0));
        break;
    case PanelMenuGlyph::Export:
        painter.drawLine(QPointF(12.0, 3.5), QPointF(12.0, 14.0));
        painter.drawLine(QPointF(8.5, 10.8), QPointF(12.0, 14.3));
        painter.drawLine(QPointF(15.5, 10.8), QPointF(12.0, 14.3));
        painter.drawRoundedRect(
            QRectF(4.0, 15.0, 16.0, 5.0), 1.3, 1.3);
        break;
    case PanelMenuGlyph::DataSource:
        painter.drawEllipse(QRectF(4.0, 4.0, 16.0, 5.0));
        painter.drawLine(QPointF(4.0, 6.5), QPointF(4.0, 17.5));
        painter.drawLine(QPointF(20.0, 6.5), QPointF(20.0, 17.5));
        painter.drawArc(QRectF(4.0, 10.0, 16.0, 5.0),
                        180 * 16,
                        180 * 16);
        painter.drawArc(QRectF(4.0, 15.0, 16.0, 5.0),
                        180 * 16,
                        180 * 16);
        break;
    case PanelMenuGlyph::PanelSetup:
        for (const qreal y : {6.0, 12.0, 18.0}) {
            painter.drawLine(QPointF(4.0, y), QPointF(20.0, y));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(9.0, 6.0), 2.0, 2.0);
        painter.drawEllipse(QPointF(16.0, 12.0), 2.0, 2.0);
        painter.drawEllipse(QPointF(12.0, 18.0), 2.0, 2.0);
        break;
    }
    return pixmap;
}

QIcon panelMenuIcon(PanelMenuGlyph glyph)
{
    const QPalette palette = QApplication::palette();
    QIcon icon;
    icon.addPixmap(
        panelMenuPixmap(glyph, palette.color(QPalette::Text)),
        QIcon::Normal);
    const QPixmap active =
        panelMenuPixmap(
            glyph,
            palette.color(QPalette::HighlightedText));
    icon.addPixmap(active, QIcon::Active);
    icon.addPixmap(active, QIcon::Selected);
    icon.addPixmap(
        panelMenuPixmap(
            glyph,
            palette.color(QPalette::Disabled, QPalette::Text)),
        QIcon::Disabled);
    return icon;
}

class PanelContextMenuStyle final : public QProxyStyle {
public:
    explicit PanelContextMenuStyle(int iconSize)
        : iconSize_(iconSize)
    {
    }

    int pixelMetric(PixelMetric metric,
                    const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        if (metric == QStyle::PM_SmallIconSize) {
            return iconSize_;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

private:
    int iconSize_ = 20;
};

class DownwardComboStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint,
                  const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup) {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

} // namespace


void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange) {
        if (aboutButton_) {
            aboutButton_->setIcon(infoIcon());
        }
        if (recentEnvironmentButton_) {
            recentEnvironmentButton_->setIcon(recentArrowIcon());
        }
        if (refreshAction_) {
            refreshAction_->setIcon(refreshIcon());
        }
        if (layoutAction_) {
            layoutAction_->setIcon(layoutIcon());
        }
        if (appearanceAction_) {
            appearanceAction_->setIcon(appearanceIcon());
        }
        if (keyboardShortcutsAction_) {
            keyboardShortcutsAction_->setIcon(
                keyboardShortcutIcon());
        }
        if (zoomButton_ && pointButton_) {
            zoomButton_->setIcon(
                modeIcon(InteractionMode::Zoom,
                         currentInteractionMode_ == InteractionMode::Zoom));
            pointButton_->setIcon(
                modeIcon(InteractionMode::Point,
                         currentInteractionMode_ == InteractionMode::Point));
        }
        setStopButtonPaused(refresh_ && refresh_->dataPaused());
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateTopControlVisibility();
}

void MainWindow::setStopButtonPaused(bool paused)
{
    if (applyShotButton_) {
        applyShotButton_->setIcon(
            shotControlIcon(ShotControlGlyph::Apply));
    }
    if (stopButton_) {
        stopButton_->setIcon(
            shotControlIcon(paused ? ShotControlGlyph::Continue
                                   : ShotControlGlyph::Stop));
        stopButton_->setToolTip(
            paused ? "Continue data refresh" : "Stop data refresh");
    }
    if (previousShotButton_) {
        previousShotButton_->setIcon(
            shotControlIcon(ShotControlGlyph::Previous));
    }
    if (nextShotButton_) {
        nextShotButton_->setIcon(
            shotControlIcon(ShotControlGlyph::Next));
    }
    if (latestShotButton_) {
        latestShotButton_->setIcon(
            shotControlIcon(ShotControlGlyph::Latest));
    }
    updateShortcutToolTips();
}

void MainWindow::schedulePointSync(PlotWidget* source, double x)
{
    pendingPointX_ = x;
    pointSyncSource_ = source;
    const int generation = pointSyncGeneration_;
    if (pointSyncQueued_) {
        return;
    }
    pointSyncQueued_ = true;
    QTimer::singleShot(16, this, [this, generation] {
        if (generation != pointSyncGeneration_) {
            return;
        }
        pointSyncQueued_ = false;
        if (!(pointButton_ && pointButton_->isChecked()) || !std::isfinite(pendingPointX_)) {
            return;
        }
        PlotWidget* queuedSource = pointSyncSource_;
        const double queuedX = pendingPointX_;
        QRect visibleArea = gridHost_->rect();
        if (scrollArea_ && scrollArea_->viewport()) {
            visibleArea = QRect(gridHost_->mapFrom(scrollArea_->viewport(), QPoint(0, 0)),
                                scrollArea_->viewport()->size()).intersected(gridHost_->rect());
        }
        for (const auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                if (!plot || !plot->isVisible()) {
                    continue;
                }
                const QRect plotGeometry = QRect(plot->mapTo(gridHost_, QPoint(0, 0)), plot->size());
                if (!visibleArea.intersects(plotGeometry)) {
                    continue;
                }
                const int seriesIndex =
                    plot == queuedSource ? plot->activePointSeriesIndex() : 0;
                const bool interpolate = plot != queuedSource && !singlePanelMaximized_;
                plot->setSyncedPointX(queuedX, seriesIndex, interpolate);
            }
        }
    });
}

void MainWindow::buildUi()
{
    setWindowTitle("MdsScope");
    menuBar()->hide();
    QAction* zoomModeAction = new QAction("Zoom / Move", this);
    QAction* pointModeAction = new QAction("Point", this);
    connect(zoomModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Point); });
    addAction(zoomModeAction);
    addAction(pointModeAction);

    auto* toolbar = toolbar_ = addToolBar("Tools");
    toolbar->setMovable(false);
    toolbar->toggleViewAction()->setVisible(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 5px; padding: 2px 4px; border: 0px; }"
        "QToolButton {"
        "  margin: 0px;"
        "  padding: 3px;"
        "  min-width: 30px;"
        "  min-height: 30px;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "}"
        "QToolButton:hover {"
        "  background: palette(midlight);"
        "  border-color: palette(highlight);"
        "}"
        "QToolButton:pressed {"
        "  background: palette(dark);"
        "  border-color: palette(highlight);"
        "}");
    QAction* openAction = toolbar->addAction(
        openFileIcon(),
        "Open configure file",
        this,
        &MainWindow::openEnvironmentFile);
    openButton_ = qobject_cast<QToolButton*>(toolbar->widgetForAction(openAction));
    if (openButton_) {
        openButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    auto* recentEnvironmentSpace =
        recentEnvironmentSpace_ = new QWidget(toolbar);
    recentEnvironmentSpace->setFixedSize(7, 30);
    toolbar->addWidget(recentEnvironmentSpace);
    recentEnvironmentButton_ = new QToolButton(toolbar);
    recentEnvironmentButton_->setObjectName("recentEnvironmentButton");
    recentEnvironmentButton_->setIcon(recentArrowIcon());
    recentEnvironmentButton_->setIconSize(QSize(12, 30));
    recentEnvironmentButton_->setToolTip("Recent configure files");
    recentEnvironmentButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    recentEnvironmentButton_->setAutoRaise(true);
    recentEnvironmentButton_->setFixedSize(12, 30);
    recentEnvironmentButton_->setStyleSheet(
        "QToolButton#recentEnvironmentButton {"
        "  background: transparent;"
        "  color: palette(buttonText);"
        "  border: 0px;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  min-width: 12px;"
        "  max-width: 12px;"
        "  min-height: 30px;"
        "  max-height: 30px;"
        "}");
    connect(recentEnvironmentButton_, &QToolButton::clicked, this, &MainWindow::showRecentEnvironmentMenu);
    QWidget* recentMenuParent = openButton_ ? static_cast<QWidget*>(openButton_) : static_cast<QWidget*>(toolbar);
    recentEnvironmentMenu_ = new QMenu(recentMenuParent);
    connect(recentEnvironmentMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentEnvironmentMenu);
    refreshRecentEnvironmentMenu();
    saveAction_ = toolbar->addAction(
        saveIcon(),
        "Save",
        this,
        &MainWindow::saveCurrentEnvironment);
    exportAction_ = toolbar->addAction(
        exportDataIcon(),
        "Export data",
        this,
        &MainWindow::openExportDataDialog);
    refreshAction_ = toolbar->addAction(
        refreshIcon(),
        "Refresh data",
        this,
        &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login", this, &MainWindow::openLoginDialog);
    updateLoginActionIcon();
    sshAction_ = toolbar->addAction(sshIcon(0), "SSH remote access", this, &MainWindow::openSshDialog);
    connect(sshTunnelManager_, &SshTunnelManager::stateChanged, this, [this] {
        updateSshActionIcon();
    });
    updateSshActionIcon();
    QAction* internalWebAction = toolbar->addAction(browserIcon(), "Internal web pages");
    if ((internalWebButton_ = qobject_cast<QToolButton*>(toolbar->widgetForAction(internalWebAction)))) {
        internalWebButton_->setObjectName(QStringLiteral("internalWebButton"));
        internalWebButton_->setStyleSheet(
            QStringLiteral("QToolButton#internalWebButton::menu-indicator { image: none; width: 0px; }"));
        internalWebMenu_ = new QMenu(internalWebButton_);
        connect(internalWebMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshInternalWebMenu);
        internalWebButton_->setMenu(internalWebMenu_);
        internalWebButton_->setPopupMode(QToolButton::InstantPopup);
        refreshInternalWebMenu();
    }
    layoutAction_ = toolbar->addAction(
        layoutIcon(), "Layout setup", this, &MainWindow::openLayoutSetupDialog);
    appearanceAction_ = toolbar->addAction(
        appearanceIcon(),
        "Customize appearance",
        this,
        &MainWindow::openCustomizeDialog);
    keyboardShortcutsAction_ = toolbar->addAction(
        keyboardShortcutIcon(),
        "Keyboard shortcuts",
        this,
        &MainWindow::openShortcutDialog);

    gridHost_ = new QWidget(this);
    gridHost_->setFocusPolicy(Qt::StrongFocus);
    gridHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridLayout_ = new QGridLayout(gridHost_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(0);
    setCentralWidget(gridHost_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: palette(highlight);");

    toolbar->addSeparator();
    auto* topControls = topControls_ = new QWidget(toolbar);
    topControls->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    topControls->setMinimumWidth(0);
    auto* topLayout =
        topControlsLayout_ = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(2, 0, 2, 0);
    topLayout->setSpacing(3);
    topControls->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
        "QToolButton#aboutButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 15px;"
        "  background: transparent;"
        "  padding: 0px;"
        "  margin-left: 8px;"
        "}");
    rateLabel_ = new QLabel("Rate", topControls);
    topLayout->addWidget(rateLabel_);
    dataModeCombo_ = new QComboBox(topControls);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Medium", static_cast<int>(DataReadMode::Medium));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(
        dataModeCombo_->findData(static_cast<int>(globalRateMode_)));
    dataModeCombo_->setFixedWidth(90);
    auto* downwardComboStyle = new DownwardComboStyle;
    downwardComboStyle->setParent(dataModeCombo_);
    dataModeCombo_->setStyle(downwardComboStyle);
    auto updateRateToolTip = [this] {
        const int defaultIndex =
            dataModeCombo_->findData(static_cast<int>(defaultRateMode_));
        QString toolTip =
            QString("Startup default: %1\nRight-click to set the current Rate as default")
                .arg(defaultIndex >= 0
                         ? dataModeCombo_->itemText(defaultIndex)
                         : QStringLiteral("Thin"));
        const QString keys = shortcutText(ShortcutCommand::GlobalRate);
        if (!keys.isEmpty()) {
            toolTip += QStringLiteral(" (%1)").arg(keys);
        }
        dataModeCombo_->setToolTip(toolTip);
    };
    auto setStartupDefault = [this, updateRateToolTip](DataReadMode mode,
                                                       const QString& label) {
        defaultRateMode_ = mode;
        preferences_->setDefaultReadMode(defaultRateMode_);
        updateRateToolTip();
        setStatus(QString("Startup default Rate: %1").arg(label));
    };
    updateRateToolTip();
    dataModeCombo_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dataModeCombo_,
            &QWidget::customContextMenuRequested,
            this,
            [this, setStartupDefault](const QPoint& pos) {
        QMenu menu(dataModeCombo_);
        QAction* setDefault = menu.addAction("Set Default");
        if (menu.exec(dataModeCombo_->mapToGlobal(pos)) != setDefault) {
            return;
        }
        const QVariant value = dataModeCombo_->currentData();
        if (!value.isValid()) {
            return;
        }
        setStartupDefault(static_cast<DataReadMode>(value.toInt()),
                          dataModeCombo_->currentText());
    });
    topLayout->addWidget(dataModeCombo_);

    topInfoLabel_ = new QLabel("Shot: --", topControls);
    ipInfoLabel_ = new QLabel("Ip: --", topControls);
    pulseInfoLabel_ = new QLabel("Pulse: --", topControls);
    itInfoLabel_ = new QLabel("It: --", topControls);
    timeInfoLabel_ = new QLabel("Time: --", topControls);
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        label->setStyleSheet("color: palette(highlight);");
        topLayout->addWidget(label);
    }
    topLayout->addStretch(1);
    themeModeButton_ = new ThemeModeButton(topControls);
    topLayout->addWidget(themeModeButton_);
    aboutButton_ = new QToolButton(topControls);
    aboutButton_->setObjectName("aboutButton");
    aboutButton_->setIcon(infoIcon());
    aboutButton_->setIconSize(QSize(28, 28));
    aboutButton_->setFixedSize(34, 34);
    aboutButton_->setToolTip("About MdsScope");
    topLayout->addWidget(aboutButton_);
    toolbar->addWidget(topControls);

    auto* bottom = bottomControls_ = new QWidget(this);
    auto* bottomLayout =
        bottomControlsLayout_ = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(4, 1, 4, 1);
    bottomLayout->setSpacing(5);
    bottom->setMaximumHeight(34);
    bottom->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QToolButton { margin: 0px; padding: 1px; min-width: 30px; min-height: 28px; }"
        "QToolButton[interactionControl=\"true\"]:hover {"
        "  background: palette(midlight);"
        "  border-color: palette(highlight);"
        "}"
        "QToolButton[interactionControl=\"true\"]:pressed {"
        "  background: palette(dark);"
        "  border-color: palette(highlight);"
        "}"
        "QToolButton[shotControl=\"true\"] {"
        "  min-width: 66px;"
        "  max-width: 66px;"
        "  min-height: 28px;"
        "  max-height: 28px;"
        "  background: palette(button);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 4px;"
        "}"
        "QToolButton[shotControl=\"true\"]:hover {"
        "  background: palette(midlight);"
        "  border-color: palette(highlight);"
        "}"
        "QToolButton[shotControl=\"true\"]:pressed {"
        "  background: palette(dark);"
        "  border-color: palette(highlight);"
        "}");
    zoomButton_ = new QToolButton(bottom);
    pointButton_ = new QToolButton(bottom);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setProperty("interactionControl", true);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip(
        "Point (Ctrl+P): click to activate, Esc to pause, Enter to resume");
    pointButton_->setChecked(true);
    bottomLayout->addWidget(zoomButton_);
    bottomLayout->addWidget(pointButton_);
    bottomLayout->addWidget(new QLabel("Shot", bottom));
    shotCombo_ = new QComboBox(bottom);
    shotCombo_->setEditable(true);
    shotCombo_->setInsertPolicy(QComboBox::NoInsert);
    shotCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    shotCombo_->setMaxVisibleItems(10);
    shotCombo_->view()->setTextElideMode(Qt::ElideMiddle);
    shotCombo_->view()->setMinimumWidth(260);
    shotCombo_->view()->setMaximumWidth(520);
    shotEdit_ = shotCombo_->lineEdit();
    refreshShotHistory();
    auto resizeShotEdit = [this] {
        if (!shotEdit_ || !shotCombo_) {
            return;
        }
        const QFontMetrics fm(shotEdit_->font());
        const int textWidth = fm.horizontalAdvance(shotEdit_->text().trimmed().isEmpty()
                                                       ? QStringLiteral("143850-143858")
                                                       : shotEdit_->text().trimmed());
        shotCombo_->setFixedWidth(std::clamp(textWidth + 52, 120, 620));
    };
    resizeShotEdit();
    bottomLayout->addWidget(shotCombo_);
    applyShotButton_ = new QToolButton(bottom);
    stopButton_ = new QToolButton(bottom);
    previousShotButton_ = new QToolButton(bottom);
    nextShotButton_ = new QToolButton(bottom);
    latestShotButton_ = new QToolButton(bottom);
    for (QToolButton* button :
         {applyShotButton_,
          stopButton_,
          previousShotButton_,
          nextShotButton_,
          latestShotButton_}) {
        button->setProperty("shotControl", true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(66, 28);
    }
    applyShotButton_->setToolTip("Apply shot (Enter)");
    previousShotButton_->setToolTip("Previous shot");
    nextShotButton_->setToolTip("Next shot");
    latestShotButton_->setToolTip("Latest shot");
    setStopButtonPaused(false);
    bottomLayout->addWidget(applyShotButton_);
    bottomLayout->addWidget(stopButton_);
    bottomLayout->addWidget(previousShotButton_);
    bottomLayout->addWidget(nextShotButton_);
    bottomLayout->addWidget(latestShotButton_);
    bottomLayout->addWidget(statusLabel_, 1);
    statusBar()->addWidget(bottom, 1);
    setInteractionMode(InteractionMode::Point);

    connect(zoomButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Point); });
    connect(applyShotButton_, &QToolButton::clicked, this, &MainWindow::applyShot);
    connect(stopButton_, &QToolButton::clicked, this, &MainWindow::onStopOrContinue);
    connect(previousShotButton_, &QToolButton::clicked, this, [this] { stepShot(-1); });
    connect(nextShotButton_, &QToolButton::clicked, this, [this] { stepShot(1); });
    connect(latestShotButton_, &QToolButton::clicked, this, &MainWindow::latestShot);
    connect(shotEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyShot);
    connect(shotEdit_,
            &QLineEdit::returnPressed,
            this,
            &MainWindow::focusSelectedPlot);
    connect(shotEdit_, &QLineEdit::textChanged, this, [resizeShotEdit] { resizeShotEdit(); });
    connect(shotCombo_, &QComboBox::activated, this, [this](int index) {
        const QString shot = shotCombo_->itemData(index).toString();
        if (!shot.isEmpty()) {
            shotCombo_->setEditText(shot);
        }
        applyShot();
    });
    connect(dataModeCombo_, &QComboBox::activated, this, [this](int index) {
        const QVariant value = dataModeCombo_->itemData(index);
        if (!value.isValid()) {
            return;
        }
        const DataReadMode mode = static_cast<DataReadMode>(value.toInt());
        globalRateMode_ = mode;
        const LayoutConfig previousDisplay = displayConfig_;
        bool configChanged = false;
        QHash<PanelId, QRectF> rateRefreshViews;
        // Preserve only an explicit user view. currentView() also returns the
        // current automatic data bounds; preserving those would turn a Thin
        // 0..1 auto-range into a fixed range for later Medium/Full reads.
        for (int c = 0; c < plotWidgets_.size(); ++c) {
            for (int r = 0; r < plotWidgets_[c].size(); ++r) {
                if (c >= displayConfig_.columns.size()
                    || r >= displayConfig_.columns[c].size()
                    || displayConfig_.columns[c][r].signalSpecs.isEmpty()
                    || !plotWidgets_[c][r]) {
                    continue;
                }
                if (!plotWidgets_[c][r]->hasView()) {
                    continue;
                }
                const QRectF view =
                    RefreshCoordinator::preservedRateView(
                        true,
                        plotWidgets_[c][r]->currentView());
                if (view.isValid()) {
                    rateRefreshViews.insert({c, r}, view);
                }
            }
        }
        for (int c = 0; c < config_.columns.size(); ++c) {
            for (int r = 0; r < config_.columns[c].size(); ++r) {
                PlotSpec& plot = config_.columns[c][r];
                for (SignalSpec& sig : plot.signalSpecs) {
                    if (sig.readMode != mode || !sig.readModeExplicit) {
                        sig.readMode = mode;
                        sig.readModeExplicit = true;
                        configChanged = true;
                    }
                }
            }
        }
        if (!configChanged) {
            refresh_->clearPendingRateViews();
            setStatus(QString("Global Rate unchanged: %1")
                          .arg(dataModeCombo_->currentText()));
            return;
        }

        syncDisplayConfig();
        for (auto it = rateRefreshViews.cbegin(); it != rateRefreshViews.cend(); ++it) {
            const int c = it.key().column;
            const int r = it.key().row;
            if (c >= 0 && r >= 0
                && c < plotWidgets_.size()
                && r < plotWidgets_[c].size()) {
                plotWidgets_[c][r]->applyView(it.value());
            }
        }

        struct ChangedRatePanel {
            int column = -1;
            int row = -1;
            QVector<int> signalIndices;
        };
        QVector<ChangedRatePanel> changedPanels;
        int visibleSignals = 0;
        int changedSignals = 0;
        for (int c = 0; c < displayConfig_.columns.size(); ++c) {
            for (int r = 0; r < displayConfig_.columns[c].size(); ++r) {
                const PlotSpec& currentPlot =
                    displayConfig_.columns[c][r];
                const PlotSpec* previousPlot =
                    c < previousDisplay.columns.size()
                        && r < previousDisplay.columns[c].size()
                    ? &previousDisplay.columns[c][r]
                    : nullptr;
                const bool stableSlots =
                    previousPlot
                    && previousPlot->signalSpecs.size()
                           == currentPlot.signalSpecs.size()
                    && c < plotWidgets_.size()
                    && r < plotWidgets_[c].size()
                    && plotWidgets_[c][r];
                QVector<int> panelChanges;
                for (int s = 0; s < currentPlot.signalSpecs.size(); ++s) {
                    const SignalSpec& currentSignal =
                        currentPlot.signalSpecs[s];
                    if (currentSignal.hidden) {
                        continue;
                    }
                    ++visibleSignals;
                    bool sameRequest = false;
                    if (stableSlots) {
                        const SignalSpec& previousSignal =
                            previousPlot->signalSpecs[s];
                        sameRequest =
                            plotWidgets_[c][r]->hasSeriesData(s)
                            && !previousSignal.hidden
                            && effectiveSignalShot(*previousPlot,
                                                   previousSignal)
                                == effectiveSignalShot(currentPlot,
                                                       currentSignal)
                            && previousSignal.yExpr
                                   == currentSignal.yExpr
                            && previousSignal.xExpr
                                   == currentSignal.xExpr
                            && previousSignal.experiment
                                   == currentSignal.experiment
                            && previousSignal.serverIp
                                   == currentSignal.serverIp
                            && previousSignal.readMode
                                   == currentSignal.readMode;
                    }
                    if (!sameRequest) {
                        panelChanges.push_back(s);
                        ++changedSignals;
                    }
                }
                if (!panelChanges.isEmpty()) {
                    changedPanels.push_back(
                        {c, r, std::move(panelChanges)});
                }
            }
        }

        if (changedSignals <= 0) {
            refresh_->clearPendingRateViews();
            setStatus(QString("Global Rate updated without data reload: %1")
                          .arg(dataModeCombo_->currentText()));
        } else if (changedSignals == visibleSignals) {
            // When everything changed, retain the concurrent global path. It
            // is faster than serial panel reads and has no unchanged data to
            // preserve.
            refresh_->setPendingRateViews(rateRefreshViews);
            refreshData();
        } else {
            refresh_->clearPendingRateViews();
            clearDataPause();
            for (ChangedRatePanel& panel : changedPanels) {
                refreshSignals(
                    panel.column,
                    panel.row,
                    std::move(panel.signalIndices),
                    globalRateMode_,
                    rateRefreshViews.value(
                        {panel.column, panel.row}));
            }
        }
    });
    connect(aboutButton_, &QToolButton::clicked, this, &MainWindow::openAboutDialog);
    rebuildShotInputShortcuts();
    applyUiMetrics();
    updateShortcutToolTips();
}

void MainWindow::updateGlobalRateControl()
{
    if (!dataModeCombo_) {
        return;
    }
    const int index = dataModeCombo_->findData(static_cast<int>(globalRateMode_));
    QSignalBlocker blocker(dataModeCombo_);
    dataModeCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void MainWindow::openGlobalRateMenu()
{
    if (!dataModeCombo_) {
        return;
    }
    dataModeCombo_->setFocus(Qt::ShortcutFocusReason);
    dataModeCombo_->showPopup();
}

void MainWindow::showPanelContextMenu(PlotWidget* plot,
                                      int column,
                                      int row,
                                      const QPoint& pos,
                                      bool openRateSubmenu)
{
    if (!plot) {
        return;
    }
    selectPlot(column, row);

    QMenu menu(this);
    constexpr int menuIconSize = 26;
    auto* menuStyle = new PanelContextMenuStyle(menuIconSize);
    menuStyle->setParent(&menu);
    menu.setStyle(menuStyle);
    const QPalette menuPalette = menu.palette();
    menu.setStyleSheet(
        QStringLiteral(
            "QMenu {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 30px 6px 10px;"
            "  margin: 1px 3px;"
            "  border-radius: 5px;"
            "}"
            "QMenu::item:selected {"
            "  background: %4;"
            "  color: %5;"
            "}"
            "QMenu::item:disabled {"
            "  color: %6;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: %3;"
            "  margin: 4px 8px;"
            "}"
            "QMenu::right-arrow {"
            "  margin-right: 7px;"
            "}")
            .arg(menuPalette.color(QPalette::Base).name(),
                 menuPalette.color(QPalette::Text).name(),
                 menuPalette.color(QPalette::Mid).name(),
                 menuPalette.color(QPalette::Highlight).name(),
                 menuPalette.color(QPalette::HighlightedText).name(),
                 menuPalette
                     .color(QPalette::Disabled, QPalette::Text)
                     .name()));

    auto actionText = [this](const QString& label,
                             ShortcutCommand command) {
        const QString keys = shortcutText(command);
        return keys.isEmpty()
                   ? label
                   : label + QStringLiteral("\t") + keys;
    };
    QAction* maxAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::Maximize),
        actionText(QStringLiteral("Maximize Panel"),
                   ShortcutCommand::MaximizePanel));
    QAction* showAllAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::ShowAll),
        actionText(QStringLiteral("Show All Panels"),
                   ShortcutCommand::ShowAllPanels));
    showAllAction->setEnabled(singlePanelMaximized_);
    QAction* resetCurrentAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::ResetCurrent),
        actionText(QStringLiteral("Reset Current Scale"),
                   ShortcutCommand::ResetCurrentScale));
    QAction* resetAllAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::ResetAll),
        actionText(QStringLiteral("Reset All Scales"),
                   ShortcutCommand::ResetAllScales));

    menu.addSeparator();
    QAction* sameXAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::SameX),
        actionText(QStringLiteral("All Same X Scale"),
                   ShortcutCommand::SameXScale));
    QAction* sameYAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::SameY),
        actionText(QStringLiteral("All Same Y Scale"),
                   ShortcutCommand::SameYScale));

    menu.addSeparator();
    QMenu* rateMenu = menu.addMenu(
        panelMenuIcon(PanelMenuGlyph::Rate),
        actionText(QStringLiteral("Rate"),
                   ShortcutCommand::PanelRate));
    connect(&menu, &QMenu::hovered, &menu, [rateMenu](QAction* action) {
        if (!rateMenu->isVisible()
            || action == rateMenu->menuAction()
            || rateMenu->actions().contains(action)
            || rateMenu->rect().contains(
                rateMenu->mapFromGlobal(QCursor::pos()))) {
            return;
        }
        rateMenu->close();
    });
    QAction* thinRateAction = rateMenu->addAction("Thin");
    QAction* mediumRateAction = rateMenu->addAction("Medium");
    QAction* fullRateAction = rateMenu->addAction("Full");
    const QVector<QPair<QAction*, DataReadMode>> rateActions = {
        {thinRateAction, DataReadMode::Thin},
        {mediumRateAction, DataReadMode::Medium},
        {fullRateAction, DataReadMode::Full},
    };
    const bool validPanel = column >= 0 && row >= 0
                            && column < config_.columns.size()
                            && row < config_.columns[column].size();
    rateMenu->setEnabled(validPanel && !config_.columns[column][row].signalSpecs.isEmpty());
    if (validPanel && !config_.columns[column][row].signalSpecs.isEmpty()) {
        const DataReadMode firstMode =
            effectiveSignalReadMode(globalRateMode_,
                                    config_.columns[column][row].signalSpecs.front());
        const bool uniformMode = std::all_of(config_.columns[column][row].signalSpecs.cbegin(),
                                             config_.columns[column][row].signalSpecs.cend(),
                                             [this, firstMode](const SignalSpec& sig) {
                                                 return effectiveSignalReadMode(globalRateMode_, sig)
                                                        == firstMode;
                                             });
        for (const auto& [action, mode] : rateActions) {
            action->setCheckable(true);
            action->setChecked(uniformMode && mode == firstMode);
        }
    }
    QAction* exportDataAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::Export),
        actionText(QStringLiteral("Export Data"),
                   ShortcutCommand::PanelExport));

    menu.addSeparator();
    QAction* dataSourceAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::DataSource),
        actionText(QStringLiteral("Data Source Setup"),
                   ShortcutCommand::PanelSourceSetup));
    QAction* panelSetupAction = menu.addAction(
        panelMenuIcon(PanelMenuGlyph::PanelSetup),
        actionText(QStringLiteral("Panel Setup"),
                   ShortcutCommand::PanelSetup));

    if (openRateSubmenu && rateMenu->isEnabled()) {
        QTimer::singleShot(0, &menu, [&menu, rateMenu] {
            menu.setActiveAction(rateMenu->menuAction());
            const QRect actionRect =
                menu.actionGeometry(rateMenu->menuAction());
            rateMenu->popup(
                menu.mapToGlobal(
                    QPoint(menu.width() - 4, actionRect.top())));
            const QList<QAction*> actions = rateMenu->actions();
            const auto checked = std::find_if(
                actions.cbegin(),
                actions.cend(),
                [](QAction* action) { return action->isChecked(); });
            rateMenu->setActiveAction(
                checked != actions.cend()
                    ? *checked
                    : actions.value(0));
        });
    }
    const QAction* chosen = menu.exec(plot->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == maxAction) {
        maximizeCurrentPanel();
    } else if (chosen == showAllAction) {
        showAllPanels();
    } else if (chosen == panelSetupAction) {
        panelSetupForCurrentPanel();
    } else if (chosen == dataSourceAction) {
        dataSourceSetupForCurrentPanel();
    } else if (chosen == thinRateAction || chosen == mediumRateAction || chosen == fullRateAction) {
        const DataReadMode mode =
            chosen == fullRateAction ? DataReadMode::Full
            : chosen == mediumRateAction ? DataReadMode::Medium
                                         : DataReadMode::Thin;
        // An automatic range belongs to the old data and must be recomputed
        // from the newly selected Rate. Preserve X only after the user has
        // explicitly changed the view.
        const QRectF rateRefreshView = RefreshCoordinator::preservedRateView(
            plot->hasView(),
            plot->currentView());
        PlotSpec& panel = config_.columns[column][row];
        QVector<int> changedSignals;
        for (int i = 0; i < panel.signalSpecs.size(); ++i) {
            if (panel.signalSpecs[i].readMode != mode
                || !panel.signalSpecs[i].readModeExplicit) {
                panel.signalSpecs[i].readMode = mode;
                panel.signalSpecs[i].readModeExplicit = true;
                changedSignals.push_back(i);
            }
        }
        if (changedSignals.isEmpty()) {
            setStatus(QString("Panel Rate unchanged: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        syncDisplayConfig();
        if (rateRefreshView.isValid() && rateRefreshView.width() > 0.0 && rateRefreshView.height() > 0.0) {
            plot->applyView(rateRefreshView);
        }
        if (displayConfig_.columns[column][row].signalSpecs.size() == panel.signalSpecs.size()) {
            refreshSignals(column,
                           row,
                           std::move(changedSignals),
                           globalRateMode_,
                           rateRefreshView);
        } else {
            // Expanded shot expressions do not have a one-to-one source index.
            refreshSignals(column, row, {}, globalRateMode_, rateRefreshView);
        }
    } else if (chosen == exportDataAction) {
        exportCurrentPanelData();
    } else if (chosen == resetCurrentAction) {
        resetCurrentScale();
    } else if (chosen == resetAllAction) {
        resetScales();
    } else if (chosen == sameXAction) {
        applyScaleToAll();
    } else if (chosen == sameYAction) {
        applyYScaleToAll();
    }
}

void MainWindow::openCustomizeDialog()
{
    FontSettings& fonts = fontSettings_;
    QDialog dialog(this);
    dialog.setWindowTitle("Customize Appearance");
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        dialog.setStyleSheet(
            "QDialog { background: #f6f6f6; color: #111827; }"
            "QLabel { background: transparent; color: #111827; }"
            "QSpinBox, QFontComboBox, QComboBox {"
            "  background: #ffffff;"
            "  color: #111827;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 3px;"
            "  padding: 3px 6px;"
            "  selection-background-color: #2563eb;"
            "  selection-color: #ffffff;"
            "}"
            "QSpinBox:focus, QFontComboBox:focus, QComboBox:focus { border-color: #2563eb; }"
            "QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }");
    }
    auto* layout = new QFormLayout(&dialog);
    auto* family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(fonts.family));
    auto* legendSize = new QSpinBox(&dialog);
    auto* axisSize = new QSpinBox(&dialog);
    auto* unitSize = new QSpinBox(&dialog);
    auto* uiSize = new QSpinBox(&dialog);
    auto* iconSize = new QComboBox(&dialog);
    for (QSpinBox* box : {legendSize, axisSize, unitSize, uiSize}) {
        box->setRange(6, 28);
        box->setSingleStep(1);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    legendSize->setValue(fonts.legendSize);
    axisSize->setValue(fonts.axisSize);
    unitSize->setValue(fonts.unitSize);
    uiSize->setValue(fonts.uiSize);
    iconSize->addItem("20 px (Compact)", 20);
    iconSize->addItem("24 px (Default)", 24);
    iconSize->addItem("28 px (Large)", 28);
    iconSize->addItem("32 px (Extra large)", 32);
    iconSize->setCurrentIndex(
        std::max(0, iconSize->findData(fonts.iconSize)));
    layout->addRow("Font", family);
    layout->addRow("Legend size", legendSize);
    layout->addRow("Axis size", axisSize);
    layout->addRow("Unit size", unitSize);
    layout->addRow("UI size", uiSize);
    layout->addRow("Icon size", iconSize);
    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton("OK", &dialog);
    auto* cancel = new QPushButton("Cancel", &dialog);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addRow(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    fonts.family = family->currentFont().family();
    fonts.legendSize = legendSize->value();
    fonts.axisSize = axisSize->value();
    fonts.unitSize = unitSize->value();
    fonts.uiSize = uiSize->value();
    fonts.iconSize = iconSize->currentData().toInt();
    saveFontSettings(rootPath_, fontSettings_);
    applyUiFont();
    applyUiMetrics();
    refreshPlotFonts();
}

void MainWindow::positionRecentEnvironmentButton()
{
    if (!toolbar_ || !openButton_ || !recentEnvironmentButton_) {
        return;
    }
    const qreal scale = fontSettings_.iconSize / 24.0;
    const int overlap = qRound(4.0 * scale);
    const int y =
        std::max(0, (openButton_->height()
                     - recentEnvironmentButton_->height())
                        / 2);
    recentEnvironmentButton_->move(
        openButton_->mapTo(
            toolbar_,
            QPoint(std::max(0, openButton_->width() - overlap), y)));
    recentEnvironmentButton_->raise();
    recentEnvironmentButton_->show();
}

void MainWindow::updateTopControlVisibility()
{
    if (!topControls_
        || !topControlsLayout_
        || !rateLabel_
        || !dataModeCombo_
        || !topInfoLabel_
        || !themeModeButton_
        || !aboutButton_) {
        return;
    }

    const QVector<QLabel*> secondaryLabels{
        ipInfoLabel_,
        pulseInfoLabel_,
        itInfoLabel_,
        timeInfoLabel_,
    };
    for (QLabel* label : secondaryLabels) {
        if (label) {
            label->hide();
        }
    }

    topControlsLayout_->invalidate();
    topControls_->updateGeometry();
    if (toolbar_ && toolbar_->layout()) {
        toolbar_->layout()->invalidate();
        toolbar_->layout()->activate();
    }

    int available = topControls_->width();
    if (toolbar_) {
        const qreal scale = fontSettings_.iconSize / 24.0;
        const int rightPadding =
            std::max(1, qRound(4.0 * scale));
        const int left =
            topControls_->mapTo(toolbar_, QPoint(0, 0)).x();
        const int remaining =
            toolbar_->width() - left - rightPadding;
        if (remaining > 0) {
            available = remaining;
            topControls_->setMaximumWidth(available);
        }
    }
    if (available <= 0) {
        return;
    }
    topInfoLabel_->setMinimumWidth(0);
    topInfoLabel_->setMinimumWidth(topInfoLabel_->sizeHint().width());

    const QMargins margins = topControlsLayout_->contentsMargins();
    const int spacing = topControlsLayout_->spacing();
    const QVector<QWidget*> alwaysVisible{
        rateLabel_,
        dataModeCombo_,
        topInfoLabel_,
        themeModeButton_,
        aboutButton_,
    };
    int required = margins.left() + margins.right();
    for (QWidget* widget : alwaysVisible) {
        required += widget->sizeHint().width();
    }
    // The stretch between the summary and the theme control is also a layout
    // item, so the five persistent widgets create five inter-item gaps.
    required += spacing * static_cast<int>(alwaysVisible.size());

    for (QLabel* label : secondaryLabels) {
        if (!label) {
            continue;
        }
        const int candidate = required + spacing
                              + label->sizeHint().width();
        if (candidate > available) {
            continue;
        }
        label->show();
        required = candidate;
    }
}

void MainWindow::applyUiMetrics()
{
    FontSettings& settings = fontSettings_;
    if (!QList<int>{20, 24, 28, 32}.contains(settings.iconSize)) {
        settings.iconSize = 24;
    }
    const int iconSize = settings.iconSize;
    const qreal scale = iconSize / 24.0;
    const auto metric = [scale](int base) {
        return std::max(1, qRound(base * scale));
    };

    const int toolbarButton = metric(30);
    const int toolbarRadius = metric(4);
    if (toolbar_) {
        toolbar_->setIconSize(QSize(iconSize, iconSize));
        toolbar_->setStyleSheet(
            QStringLiteral(
                "QToolBar {"
                "  spacing: %1px;"
                "  padding: %2px %3px;"
                "  border: 0px;"
                "}"
                "QToolButton {"
                "  margin: 0px;"
                "  padding: %4px;"
                "  min-width: %5px;"
                "  min-height: %5px;"
                "  background: transparent;"
                "  border: 1px solid transparent;"
                "  border-radius: %6px;"
                "}"
                "QToolButton:hover {"
                "  background: palette(midlight);"
                "  border-color: palette(highlight);"
                "}"
                "QToolButton:pressed {"
                "  background: palette(dark);"
                "  border-color: palette(highlight);"
                "}")
                .arg(metric(5))
                .arg(metric(2))
                .arg(metric(4))
                .arg(metric(3))
                .arg(toolbarButton)
                .arg(toolbarRadius));
    }

    const int recentWidth = metric(12);
    const int recentHeight = toolbarButton;
    if (recentEnvironmentSpace_) {
        recentEnvironmentSpace_->setFixedSize(
            metric(7), recentHeight);
    }
    if (recentEnvironmentButton_) {
        recentEnvironmentButton_->setIconSize(
            QSize(recentWidth, recentHeight));
        recentEnvironmentButton_->setFixedSize(
            recentWidth, recentHeight);
        recentEnvironmentButton_->setStyleSheet(
            QStringLiteral(
                "QToolButton#recentEnvironmentButton {"
                "  background: transparent;"
                "  color: palette(buttonText);"
                "  border: 0px;"
                "  padding: 0px;"
                "  margin: 0px;"
                "  min-width: %1px;"
                "  max-width: %1px;"
                "  min-height: %2px;"
                "  max-height: %2px;"
                "}")
                .arg(recentWidth)
                .arg(recentHeight));
    }

    if (topControlsLayout_) {
        topControlsLayout_->setContentsMargins(
            metric(2), 0, metric(2), 0);
        topControlsLayout_->setSpacing(metric(3));
    }
    if (topControls_) {
        topControls_->setStyleSheet(
            QStringLiteral(
                "QPushButton {"
                "  padding: %1px %2px;"
                "  min-height: %3px;"
                "}"
                "QLineEdit, QComboBox {"
                "  min-height: %3px;"
                "  padding: 0px %4px;"
                "}"
                "QLabel {"
                "  margin-left: %4px;"
                "  margin-right: %4px;"
                "}"
                "QToolButton#aboutButton {"
                "  border: 1px solid transparent;"
                "  border-radius: %5px;"
                "  background: transparent;"
                "  padding: 0px;"
                "  margin-left: %6px;"
                "}")
                .arg(metric(1))
                .arg(metric(8))
                .arg(metric(18))
                .arg(metric(2))
                .arg(metric(15))
                .arg(metric(8)));
    }
    if (themeModeButton_) {
        themeModeButton_->setUiScale(scale);
    }
    if (aboutButton_) {
        const int aboutIcon = metric(28);
        const int aboutButton = metric(34);
        aboutButton_->setIconSize(QSize(aboutIcon, aboutIcon));
        aboutButton_->setFixedSize(aboutButton, aboutButton);
    }

    const int controlHeight =
        std::max(metric(28), iconSize + metric(4));
    const int shotWidth = metric(66);
    const int bottomHeight =
        std::max(metric(34), controlHeight + metric(2));
    if (bottomControlsLayout_) {
        bottomControlsLayout_->setContentsMargins(
            metric(4), metric(1), metric(4), metric(1));
        bottomControlsLayout_->setSpacing(metric(5));
    }
    if (bottomControls_) {
        bottomControls_->setFixedHeight(bottomHeight);
        bottomControls_->setStyleSheet(
            QStringLiteral(
                "QPushButton {"
                "  padding: %1px %2px;"
                "  min-height: %3px;"
                "}"
                "QLineEdit, QComboBox {"
                "  min-height: %3px;"
                "  padding: 0px %4px;"
                "}"
                "QToolButton {"
                "  margin: 0px;"
                "  padding: %1px;"
                "  min-width: %5px;"
                "  min-height: %6px;"
                "}"
                "QToolButton[interactionControl=\"true\"]:hover {"
                "  background: palette(midlight);"
                "  border-color: palette(highlight);"
                "}"
                "QToolButton[interactionControl=\"true\"]:pressed {"
                "  background: palette(dark);"
                "  border-color: palette(highlight);"
                "}"
                "QToolButton[shotControl=\"true\"] {"
                "  min-width: %7px;"
                "  max-width: %7px;"
                "  min-height: %6px;"
                "  max-height: %6px;"
                "  background: palette(button);"
                "  border: 1px solid palette(mid);"
                "  border-radius: %8px;"
                "}"
                "QToolButton[shotControl=\"true\"]:hover {"
                "  background: palette(midlight);"
                "  border-color: palette(highlight);"
                "}"
                "QToolButton[shotControl=\"true\"]:pressed {"
                "  background: palette(dark);"
                "  border-color: palette(highlight);"
                "}")
                .arg(metric(1))
                .arg(metric(8))
                .arg(metric(18))
                .arg(metric(2))
                .arg(metric(30))
                .arg(controlHeight)
                .arg(shotWidth)
                .arg(metric(4)));
    }

    for (QToolButton* button : {zoomButton_, pointButton_}) {
        if (button) {
            button->setIconSize(QSize(iconSize, iconSize));
        }
    }
    for (QToolButton* button :
         {applyShotButton_,
          stopButton_,
          previousShotButton_,
          nextShotButton_,
          latestShotButton_}) {
        if (!button) {
            continue;
        }
        button->setIconSize(QSize(iconSize, iconSize));
        button->setFixedSize(shotWidth, controlHeight);
    }

    if (toolbar_ && toolbar_->layout()) {
        toolbar_->layout()->activate();
    }
    positionRecentEnvironmentButton();
    updateTopControlVisibility();
    QTimer::singleShot(
        0, this, &MainWindow::positionRecentEnvironmentButton);
    QTimer::singleShot(
        0, this, &MainWindow::updateTopControlVisibility);
    if (statusBar()) {
        statusBar()->updateGeometry();
    }
    updateGeometry();
}

void MainWindow::applyUiFont()
{
    const FontSettings& fonts = fontSettings_;
    QFont uiFont(fonts.family, fonts.uiSize);
    if (QApplication::font() != uiFont) {
        QApplication::setFont(uiFont);
    }
    if (font() != uiFont) {
        setFont(uiFont);
    }
    for (QWidget* widget : findChildren<QWidget*>()) {
        if (!qobject_cast<PlotWidget*>(widget) && widget->font() != uiFont) {
            widget->setFont(uiFont);
        }
    }
    if (statusLabel_ && statusLabel_->font() != uiFont) {
        statusLabel_->setFont(uiFont);
    }
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        if (label && label->font() != uiFont) {
            label->setFont(uiFont);
        }
    }
    updateTopControlVisibility();
}

void MainWindow::refreshPlotFonts()
{
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->setFontSettings(fontSettings_);
                plot->refreshStyle();
            }
        }
    }
}

void MainWindow::setInteractionMode(InteractionMode mode)
{
    if (mode != InteractionMode::Point) {
        activePointPlot_ = nullptr;
        pausedPointPlot_ = nullptr;
        pointSyncSource_ = nullptr;
        pointSyncQueued_ = false;
        pendingPointX_ = qQNaN();
        ++pointSyncGeneration_;
    }
    if (currentInteractionMode_ == mode
        && zoomButton_ && zoomButton_->isChecked() == (mode == InteractionMode::Zoom)
        && pointButton_ && pointButton_->isChecked() == (mode == InteractionMode::Point)) {
        return;
    }
    currentInteractionMode_ = mode;
    if (zoomButton_) {
        zoomButton_->setChecked(mode == InteractionMode::Zoom);
        pointButton_->setChecked(mode == InteractionMode::Point);
        zoomButton_->setIcon(modeIcon(InteractionMode::Zoom, mode == InteractionMode::Zoom));
        pointButton_->setIcon(modeIcon(InteractionMode::Point, mode == InteractionMode::Point));
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->setInteractionMode(mode);
        }
    }
}

void MainWindow::updateTopInfoLabels()
{
    // The top strip describes the shot currently displayed by the plots.
    // The latest-shot cache is maintained independently by ShotWorkflow and is
    // only applied when the user explicitly chooses Latest.
    QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const PlotSpec* plot = nullptr;
    if (selectedColumn_ >= 0 && selectedRow_ >= 0
        && selectedColumn_ < config_.columns.size()
        && selectedRow_ < config_.columns[selectedColumn_].size()) {
        plot = &config_.columns[selectedColumn_][selectedRow_];
    }
    if (!plot) {
        for (const auto& col : config_.columns) {
            for (const PlotSpec& candidate : col) {
                if (!candidate.signalSpecs.isEmpty()) {
                    plot = &candidate;
                    break;
                }
            }
            if (plot) {
                break;
            }
        }
    }
    if (plot) {
        if (shot.isEmpty()) {
            shot = plot->shot.trimmed();
        }
    }

    if (shot.isEmpty()) {
        shotWorkflow_->clearSummary();
    } else if (shot != shotWorkflow_->summary().shot) {
        scheduleTopInfoUpdate(shot);
    }

    const ShotSummary& summary = shotWorkflow_->summary();
    setLabelTextIfChanged(topInfoLabel_, "Shot: " + (shot.isEmpty() ? QStringLiteral("--") : shot));
    const bool loading = !shot.isEmpty()
                         && shot != summary.shot
                         && shotWorkflow_->pendingSummaryShot() == shot;
    const QString emptyText = loading ? QStringLiteral("...") : QStringLiteral("--");
    setLabelTextIfChanged(ipInfoLabel_, "Ip: " + (summary.ip.isEmpty() ? emptyText : summary.ip + " KA"));
    setLabelTextIfChanged(pulseInfoLabel_, "Pulse: " + (summary.pulse.isEmpty() ? emptyText : summary.pulse + " s"));
    setLabelTextIfChanged(itInfoLabel_, "It: " + (summary.it.isEmpty() ? emptyText : summary.it + " A"));
    setLabelTextIfChanged(timeInfoLabel_, "Time: " + (summary.time.isEmpty() ? emptyText : summary.time));
    if (topInfoLabel_) {
        topInfoLabel_->setToolTip(
            QStringLiteral("%1\n%2\n%3\n%4\n%5")
                .arg(topInfoLabel_->text(),
                     ipInfoLabel_->text(),
                     pulseInfoLabel_->text(),
                     itInfoLabel_->text(),
                     timeInfoLabel_->text()));
    }
    updateTopControlVisibility();
}

void MainWindow::scheduleTopInfoUpdate(const QString& shot)
{
    const QString trimmedShot = shot.trimmed();
    int generation = 0;
    if (!shotWorkflow_->beginSummaryFetch(trimmedShot, &generation)) {
        return;
    }

    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        shotWorkflow_->failSummaryFetchStart(trimmedShot, generation);
        return;
    }
    QThreadPool::globalInstance()->start([this, trimmedShot, generation, apiUrl] {
        ShotSummary summary;
        const bool ok = shotWorkflow_->fetchSummary(
            trimmedShot, &summary, apiUrl);
        QMetaObject::invokeMethod(this, [this, trimmedShot, generation, ok, summary] {
            if (!shotWorkflow_->completeSummaryFetch(
                    generation, trimmedShot, ok, summary)) {
                return;
            }
            updateTopInfoLabels();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setStatus(const QString& text)
{
    setLabelTextIfChanged(statusLabel_, text);
}

PlotWidget* MainWindow::currentPlotWidget() const
{
    if (selectedColumn_ < 0 || selectedRow_ < 0 ||
        selectedColumn_ >= plotWidgets_.size() || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return nullptr;
    }
    return plotWidgets_[selectedColumn_][selectedRow_];
}

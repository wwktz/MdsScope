// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "main_window.hpp"
#include "refresh_coordinator.hpp"
#include "ui/plot/plot_widget.hpp"
#include "shared.hpp"
#include "theme.hpp"
#include "ssh_tunnel_manager.hpp"

#include <QFontMetrics>
#include <QProxyStyle>
#include <QToolTip>

namespace {

class UpwardToolTipButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip && !toolTip().isEmpty()) {
            const QFontMetrics metrics(QToolTip::font());
            const QRect textBounds = metrics.boundingRect(toolTip());
            const int tooltipWidth = textBounds.width() + 12;
            const int tooltipHeight = textBounds.height() + 10;
            const QPoint topCenter =
                mapToGlobal(QPoint(width() / 2, 0));
            QToolTip::showText(
                QPoint(topCenter.x() - tooltipWidth / 2,
                       topCenter.y() - tooltipHeight - 17),
                toolTip(),
                this);
            return true;
        }
        return QToolButton::event(event);
    }
};

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
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(28.0 / 24.0, 28.0 / 24.0);
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
        if (layoutAction_) {
            layoutAction_->setIcon(layoutIcon());
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
        PlotWidget* source = pointSyncSource_;
        const double x = pendingPointX_;
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
                const int seriesIndex = plot == source ? plot->activePointSeriesIndex() : 0;
                const bool interpolate = plot != source && !singlePanelMaximized_;
                plot->setSyncedPointX(x, seriesIndex, interpolate);
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
    zoomModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    pointModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    addAction(zoomModeAction);
    addAction(pointModeAction);

    auto* toolbar = addToolBar("Tools");
    toolbar->setMovable(false);
    toolbar->toggleViewAction()->setVisible(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 5px; padding: 2px 4px; border: 0px; }"
        "QToolButton { margin: 0px; padding: 3px; min-width: 30px; min-height: 30px; }");
    QAction* openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open configure file", this, &MainWindow::openEnvironmentFile);
    openButton_ = qobject_cast<QToolButton*>(toolbar->widgetForAction(openAction));
    if (openButton_) {
        openButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    auto* recentEnvironmentSpace = new QWidget(toolbar);
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
    auto positionRecentEnvironmentButton = [toolbar, this] {
        if (openButton_ && recentEnvironmentButton_) {
            recentEnvironmentButton_->move(openButton_->mapTo(toolbar, QPoint(openButton_->width() - 4, 0)));
            recentEnvironmentButton_->raise();
        }
    };
    if (openButton_) {
        positionRecentEnvironmentButton();
        QTimer::singleShot(0, this, positionRecentEnvironmentButton);
        recentEnvironmentButton_->show();
    }
    connect(recentEnvironmentButton_, &QToolButton::clicked, this, &MainWindow::showRecentEnvironmentMenu);
    QWidget* recentMenuParent = openButton_ ? static_cast<QWidget*>(openButton_) : static_cast<QWidget*>(toolbar);
    recentEnvironmentMenu_ = new QMenu(recentMenuParent);
    connect(recentEnvironmentMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentEnvironmentMenu);
    refreshRecentEnvironmentMenu();
    QAction* saveAction = toolbar->addAction(saveIcon(), "Save", this, &MainWindow::saveCurrentEnvironment);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(saveAction);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Export data", this, &MainWindow::openExportDataDialog);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this, &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login", this, &MainWindow::openLoginDialog);
    updateLoginActionIcon();
    sshAction_ = toolbar->addAction(sshIcon(0), "SSH remote access", this, &MainWindow::openSshDialog);
    connect(sshTunnelManager_, &SshTunnelManager::stateChanged, this, [this] {
        cachedApiSourceUrl_.clear();
        cachedPreparedApiUrl_.clear();
        updateSshActionIcon();
    });
    updateSshActionIcon();
    QAction* internalWebAction = toolbar->addAction(browserIcon(), "Internal web pages");
    if (auto* internalWebButton = qobject_cast<QToolButton*>(toolbar->widgetForAction(internalWebAction))) {
        internalWebButton->setObjectName(QStringLiteral("internalWebButton"));
        internalWebButton->setStyleSheet(
            QStringLiteral("QToolButton#internalWebButton::menu-indicator { image: none; width: 0px; }"));
        internalWebMenu_ = new QMenu(internalWebButton);
        connect(internalWebMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshInternalWebMenu);
        internalWebButton->setMenu(internalWebMenu_);
        internalWebButton->setPopupMode(QToolButton::InstantPopup);
        refreshInternalWebMenu();
    }
    layoutAction_ = toolbar->addAction(
        layoutIcon(), "Layout setup", this, &MainWindow::openLayoutSetupDialog);
    toolbar->addAction(fontIcon(), "Customize fonts", this, &MainWindow::openCustomizeDialog);

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
    auto* topControls = new QWidget(toolbar);
    topControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* topLayout = new QHBoxLayout(topControls);
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
    topLayout->addWidget(new QLabel("Rate", topControls));
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
        dataModeCombo_->setToolTip(
            QString("Startup default: %1\nRight-click to set the current Rate as default")
                .arg(defaultIndex >= 0
                         ? dataModeCombo_->itemText(defaultIndex)
                         : QStringLiteral("Thin")));
    };
    auto setStartupDefault = [this, updateRateToolTip](DataReadMode mode,
                                                       const QString& label) {
        defaultRateMode_ = mode;
        QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
        settings.setValue("rate/default_mode",
                          static_cast<int>(defaultRateMode_));
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
    topLayout->addWidget(new ThemeModeButton(topControls));
    aboutButton_ = new QToolButton(topControls);
    aboutButton_->setObjectName("aboutButton");
    aboutButton_->setIcon(infoIcon());
    aboutButton_->setIconSize(QSize(28, 28));
    aboutButton_->setFixedSize(34, 34);
    aboutButton_->setToolTip("About MdsScope");
    topLayout->addWidget(aboutButton_);
    toolbar->addWidget(topControls);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QHBoxLayout(bottom);
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
        "QToolButton[shotControl=\"true\"] {"
        "  min-width: 66px;"
        "  max-width: 66px;"
        "  min-height: 32px;"
        "  max-height: 32px;"
        "}");
    zoomButton_ = new QToolButton(bottom);
    pointButton_ = new QToolButton(bottom);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip("Point (Ctrl+P): click to activate, Esc to exit");
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
    applyShotButton_ = new UpwardToolTipButton(bottom);
    stopButton_ = new UpwardToolTipButton(bottom);
    previousShotButton_ = new UpwardToolTipButton(bottom);
    nextShotButton_ = new UpwardToolTipButton(bottom);
    latestShotButton_ = new UpwardToolTipButton(bottom);
    for (QToolButton* button :
         {applyShotButton_,
          stopButton_,
          previousShotButton_,
          nextShotButton_,
          latestShotButton_}) {
        button->setProperty("shotControl", true);
        button->setIconSize(QSize(28, 28));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(66, 32);
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

void MainWindow::showPanelContextMenu(PlotWidget* plot, int column, int row, const QPoint& pos)
{
    if (!plot) {
        return;
    }
    selectPlot(column, row);

    QMenu menu(this);
    QAction* maxAction = menu.addAction("Max");
    QAction* showAllAction = menu.addAction("Show All Panels");
    showAllAction->setEnabled(singlePanelMaximized_);
    menu.addSeparator();
    QAction* panelSetupAction = menu.addAction("Panel Setup");
    QAction* dataSourceAction = menu.addAction("Data Source Setup");
    QMenu* rateMenu = menu.addMenu("Rate");
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
    QAction* exportDataAction = menu.addAction("Export Data");
    menu.addSeparator();
    QAction* resetCurrentAction = menu.addAction("Reset Current Scale");
    QAction* resetAllAction = menu.addAction("Reset All Panels");
    QAction* sameXAction = menu.addAction("All Same X Scale");
    QAction* sameYAction = menu.addAction("All Same Y Scale");

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
    FontSettings& fonts = fontSettings();
    QDialog dialog(this);
    dialog.setWindowTitle("Customize Fonts");
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        dialog.setStyleSheet(
            "QDialog { background: #f6f6f6; color: #111827; }"
            "QLabel { background: transparent; color: #111827; }"
            "QSpinBox, QFontComboBox {"
            "  background: #ffffff;"
            "  color: #111827;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 3px;"
            "  padding: 3px 6px;"
            "  selection-background-color: #2563eb;"
            "  selection-color: #ffffff;"
            "}"
            "QSpinBox:focus, QFontComboBox:focus { border-color: #2563eb; }"
            "QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }");
    }
    auto* layout = new QFormLayout(&dialog);
    auto* family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(fonts.family));
    auto* legendSize = new QSpinBox(&dialog);
    auto* axisSize = new QSpinBox(&dialog);
    auto* unitSize = new QSpinBox(&dialog);
    auto* uiSize = new QSpinBox(&dialog);
    for (QSpinBox* box : {legendSize, axisSize, unitSize, uiSize}) {
        box->setRange(6, 28);
        box->setSingleStep(1);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    legendSize->setValue(fonts.legendSize);
    axisSize->setValue(fonts.axisSize);
    unitSize->setValue(fonts.unitSize);
    uiSize->setValue(fonts.uiSize);
    layout->addRow("Font", family);
    layout->addRow("Legend size", legendSize);
    layout->addRow("Axis size", axisSize);
    layout->addRow("Unit size", unitSize);
    layout->addRow("UI size", uiSize);
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
    saveFontSettings(rootPath_);
    applyUiFont();
    refreshPlotFonts();
}

void MainWindow::applyUiFont()
{
    const FontSettings& fonts = fontSettings();
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
}

void MainWindow::refreshPlotFonts()
{
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->refreshStyle();
            }
        }
    }
}

void MainWindow::setInteractionMode(InteractionMode mode)
{
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
    // latestShot_ is maintained independently by the background poll and is
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
        topSummaryShot_.clear();
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        pendingTopSummaryShot_.clear();
    } else if (shot != topSummaryShot_) {
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        scheduleTopInfoUpdate(shot);
    }

    setLabelTextIfChanged(topInfoLabel_, "Shot: " + (shot.isEmpty() ? QStringLiteral("--") : shot));
    const bool loading = !shot.isEmpty() && shot != topSummaryShot_ && pendingTopSummaryShot_ == shot;
    const QString emptyText = loading ? QStringLiteral("...") : QStringLiteral("--");
    setLabelTextIfChanged(ipInfoLabel_, "Ip: " + (topSummaryIp_.isEmpty() ? emptyText : topSummaryIp_ + " KA"));
    setLabelTextIfChanged(pulseInfoLabel_, "Pulse: " + (topSummaryPulse_.isEmpty() ? emptyText : topSummaryPulse_ + " s"));
    setLabelTextIfChanged(itInfoLabel_, "It: " + (topSummaryIt_.isEmpty() ? emptyText : topSummaryIt_ + " A"));
    setLabelTextIfChanged(timeInfoLabel_, "Time: " + (topSummaryTime_.isEmpty() ? emptyText : topSummaryTime_));
}

void MainWindow::scheduleTopInfoUpdate(const QString& shot)
{
    const QString trimmedShot = shot.trimmed();
    if (trimmedShot.isEmpty() || pendingTopSummaryShot_ == trimmedShot) {
        return;
    }

    pendingTopSummaryShot_ = trimmedShot;
    const int generation = ++topSummaryGeneration_;
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        pendingTopSummaryShot_.clear();
        return;
    }
    QThreadPool::globalInstance()->start([this, trimmedShot, generation, apiUrl] {
        QString ip;
        QString pulse;
        QString it;
        QString shotTime;
        const bool ok = loadShotSummaryFromApi(trimmedShot, &ip, &pulse, &it, &shotTime, apiUrl);
        QMetaObject::invokeMethod(this, [this, trimmedShot, generation, ok, ip, pulse, it, shotTime] {
            if (generation != topSummaryGeneration_ || pendingTopSummaryShot_ != trimmedShot) {
                return;
            }
            pendingTopSummaryShot_.clear();
            topSummaryShot_ = trimmedShot;
            topSummaryIp_ = ok ? ip : QString();
            topSummaryPulse_ = ok ? pulse : QString();
            topSummaryIt_ = ok ? it : QString();
            topSummaryTime_ = ok ? shotTime : QString();
            if (!ok) {
                cachedApiSourceUrl_.clear();
                cachedPreparedApiUrl_.clear();
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

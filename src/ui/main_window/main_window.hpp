// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"
#include "core/font_settings.hpp"
#include "shortcut_settings.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <memory>
#include <optional>

class QAction;
class QComboBox;
class QEvent;
class QGridLayout;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QResizeEvent;
class QScrollArea;
class QShortcut;
class QToolBar;
class QToolButton;
class PlotWidget;
class RefreshCoordinator;
struct PanelRefreshRequest;
class SshTunnelManager;
class ShotWorkflow;
class ThemeModeButton;
class UserPreferences;
class ShortcutDispatchTestAccess;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QString rootPath, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class ShortcutDispatchTestAccess;

    void buildUi();
    void loadDefaultEnvironment(bool useLatestWhenNoCurrentShot = false);
    void loadEnvironmentList(bool useLatestWhenNoCurrentShot = false);
    void openEnvironmentFile();
    void openRecentEnvironmentFile(const QString& path);
    void showRecentEnvironmentMenu();
    void refreshRecentEnvironmentMenu();
    void clearRecentEnvironmentFiles();
    void refreshShotHistory();
    bool loadEnvironmentFile(const QString& path,
                             bool useLatestWhenNoCurrentShot = false,
                             bool rememberRecent = true,
                             bool prewarmBeforeRefresh = false);
    void rebuildGrid();
    void selectPlot(int column, int row);
    void setInteractionMode(InteractionMode mode);
    void applyShot();
    void stepShot(int delta);
    void latestShot();
    void openLoginDialog();
    void openSshDialog();
    void openInternalWebPage(const QString& source);
    void showInternalWebMenu();
    void addInternalWebPage();
    void editInternalWebPage();
    void removeInternalWebPage();
    void refreshInternalWebMenu();
    bool prepareSshLayout(const LayoutConfig& source, LayoutConfig* prepared);
    bool prepareSshUrl(const QString& source, QString* prepared);
    void updateSshActionIcon();
    void openAboutDialog();
    void applyLoginSuccessStatus(const QString& statusText);
    void applyLogoutStatus();
    void updateLoginActionIcon();
    void fetchLatestShotAsync(bool applyLatest = true);
    void updateShotControlsFromConfig(const QString& preferredShot = {});
    void setAllPlotShots(const QString& shot);
    QString maxShotInConfig() const;
    void scheduleShotRefresh();
    void refreshData();
    void stopDataRefresh();
    void onStopOrContinue();
    void resumeDataRefresh();
    void launchDataFetch(const LayoutConfig& snapshot,
                         DataReadMode readMode,
                         const QString& key);
    int countRemainingSignals(const LayoutConfig& snapshot) const;
    void clearDataPause();
    void setStopButtonPaused(bool paused);
    void cancelDataFetch();
    void cancelPanelFetch();
    void cancelPrewarmConnections();
    void startPendingFetchIfIdle();
    bool prewarmConnections();
    bool canStartDeferredRefresh() const;
    void maybeStartDeferredRefresh();
    QString refreshKey(DataReadMode readMode) const;
    QString panelRefreshKey(int column,
                            int row,
                            const QVector<int>& signalIndices,
                            DataReadMode readMode) const;
    void refreshSignals(int column,
                        int row,
                        QVector<int> signalIndices,
                        DataReadMode readMode,
                        const QRectF& rateRefreshView = {});
    void queueLoadedSignal(LoadedSignal item);
    void flushQueuedLoadedSignals();
    void applyLoadedSignal(LoadedSignal item);
    void applyLoadedSignals(const QVector<LoadedSignal>& loaded);
    void applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded,
                                 const PanelRefreshRequest& request);
    void settleDataRefreshPanelIfComplete(int column, int row);
    void fitRemainingRateRefreshPanels();
    void rememberLoadedSourceSignal(const LoadedSignal& item);
    void panelSetupForCurrentPanel();
    void dataSourceSetupForCurrentPanel();
    void openExportDataDialog();
    void exportCurrentPanelData();
    void exportDataForPanels(
        const QVector<QPair<int, int>>& panels,
        const QString& baseDirPath,
        int exportFormat,
        int exportRange,
        double customXMin = qQNaN(),
        double customXMax = qQNaN(),
        const QHash<PanelId, QSet<int>>& signalFilter = {});
    void applyScaleToAll();
    void applyYScaleToAll();
    void resetCurrentScale();
    void resetScales();
    void maximizeCurrentPanel();
    void showAllPanels();
    void showPanelContextMenu(PlotWidget* plot,
                              int column,
                              int row,
                              const QPoint& pos,
                              bool openRateSubmenu = false);
    void openGlobalRateMenu();
    void updateGlobalRateControl();
    void openLayoutSetupDialog();
    void openCustomizeDialog();
    void openShortcutDialog();
    void applyUiFont();
    void applyUiMetrics();
    void positionRecentEnvironmentButton();
    void updateTopControlVisibility();
    void refreshPlotFonts();
    void saveCurrentEnvironment();
    void saveCurrentEnvironmentAs();
    bool saveEnvironmentFile(const QString& path);
    PlotSpec defaultPlotFromSelection() const;
    void updateTopInfoLabels();
    void setStatus(const QString& text);
    PlotWidget* currentPlotWidget() const;
    void schedulePointSync(PlotWidget* source, double x);
    void scheduleTopInfoUpdate(const QString& shot);
    void syncDisplayConfig();
    bool handleShortcutKey(QKeyEvent* event, QWidget* target);
    bool triggerShortcutCommand(ShortcutCommand command);
    bool shortcutCommandEnabled(ShortcutCommand command) const;
    bool handleFixedPointKey(QKeyEvent* event, QWidget* target);
    bool activatePointForCurrentPanel(int seriesIndex = -1);
    bool resumePausedPoint();
    void restorePendingPanelNavigation();
    void dispatchPopupMenuKey(Qt::Key key);
    void dispatchEscapeKey();
    void movePanelSelection(int columnDelta, int rowDelta);
    void pauseActivePointTracking();
    void focusSelectedPlot();
    void beginShotEditSession();
    void cancelShotEditSession();
    bool handleShotEditExitKey(QKeyEvent* event);
    void rebuildShotInputShortcuts();
    void updateShortcutToolTips();
    QString shortcutText(ShortcutCommand command) const;

    QString rootPath_;
    std::unique_ptr<UserPreferences> preferences_;
    FontSettings fontSettings_;
    std::unique_ptr<ShotWorkflow> shotWorkflow_;
    QString environmentPath_;
    QString exportBasePath_;
    LayoutConfig config_;
    LayoutConfig displayConfig_;
    QMenu* recentEnvironmentMenu_ = nullptr;
    QMenu* internalWebMenu_ = nullptr;
    QWidget* gridHost_ = nullptr;
    QGridLayout* gridLayout_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* rateLabel_ = nullptr;
    QLabel* topInfoLabel_ = nullptr;
    QLabel* ipInfoLabel_ = nullptr;
    QLabel* pulseInfoLabel_ = nullptr;
    QLabel* itInfoLabel_ = nullptr;
    QLabel* timeInfoLabel_ = nullptr;
    QComboBox* shotCombo_ = nullptr;
    QLineEdit* shotEdit_ = nullptr;
    QComboBox* dataModeCombo_ = nullptr;
    DataReadMode defaultRateMode_ = DataReadMode::Thin;
    DataReadMode globalRateMode_ = DataReadMode::Thin;
    QAction* loginAction_ = nullptr;
    QAction* sshAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* layoutAction_ = nullptr;
    QAction* appearanceAction_ = nullptr;
    QAction* keyboardShortcutsAction_ = nullptr;
    SshTunnelManager* sshTunnelManager_ = nullptr;
    QString cachedApiSourceUrl_;
    QString cachedPreparedApiUrl_;
    QToolBar* toolbar_ = nullptr;
    QWidget* recentEnvironmentSpace_ = nullptr;
    QWidget* topControls_ = nullptr;
    QHBoxLayout* topControlsLayout_ = nullptr;
    ThemeModeButton* themeModeButton_ = nullptr;
    QWidget* bottomControls_ = nullptr;
    QHBoxLayout* bottomControlsLayout_ = nullptr;
    QToolButton* openButton_ = nullptr;
    QToolButton* recentEnvironmentButton_ = nullptr;
    QToolButton* internalWebButton_ = nullptr;
    QToolButton* aboutButton_ = nullptr;
    QToolButton* zoomButton_ = nullptr;
    QToolButton* pointButton_ = nullptr;
    QToolButton* applyShotButton_ = nullptr;
    QToolButton* stopButton_ = nullptr;
    QToolButton* previousShotButton_ = nullptr;
    QToolButton* nextShotButton_ = nullptr;
    QToolButton* latestShotButton_ = nullptr;
    QVector<QVector<PlotWidget*>> plotWidgets_;
    QVector<ShortcutBinding> shortcutBindings_;
    QList<QKeyCombination> pendingShortcutKeys_;
    std::optional<ShortcutCommand> pendingExactShortcut_;
    std::optional<PanelId> pendingPanelNavigationOrigin_;
    QPointer<PlotWidget> pendingPanelNavigationPausedPoint_;
    QTimer shortcutSequenceTimer_;
    QTimer shotEditExitTimer_;
    QList<QKeyCombination> pendingShotEditExitKeys_;
    QVector<QShortcut*> shotInputShortcuts_;
    bool dispatchingPopupMenuKey_ = false;
    bool dispatchingEscapeKey_ = false;
    QString shotEditSessionText_;
    bool shotEditSessionActive_ = false;
    std::unique_ptr<RefreshCoordinator> refresh_;
    int selectedColumn_ = -1;
    int selectedRow_ = -1;
    bool singlePanelMaximized_ = false;
    int maximizedColumn_ = -1;
    int maximizedRow_ = -1;
    InteractionMode currentInteractionMode_ = InteractionMode::Zoom;
    QSet<QString> rememberedSourceSignals_;
    PlotWidget* activePointPlot_ = nullptr;
    PlotWidget* pausedPointPlot_ = nullptr;
    PlotWidget* pointSyncSource_ = nullptr;
    bool pointSyncQueued_ = false;
    int pointSyncGeneration_ = 0;
    double pendingPointX_ = qQNaN();
};

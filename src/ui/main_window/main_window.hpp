// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QTimer>

#include <memory>

class QAction;
class QComboBox;
class QEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QScrollArea;
class QToolButton;
class PlotWidget;
class RefreshCoordinator;
struct PanelRefreshRequest;
class SshTunnelManager;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QString rootPath, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi();
    void loadDefaultEnvironment(bool useLatestWhenNoCurrentShot = false);
    void loadEnvironmentList(bool useLatestWhenNoCurrentShot = false);
    void openEnvironmentFile();
    void openRecentEnvironmentFile(const QString& path);
    void showRecentEnvironmentMenu();
    QString rememberedFileDialogDir() const;
    void rememberFileDialogDir(const QString& path);
    QStringList recentEnvironmentFiles() const;
    void rememberRecentEnvironmentFile(const QString& path);
    void refreshRecentEnvironmentMenu();
    void clearRecentEnvironmentFiles();
    QStringList recentShotExpressions() const;
    void rememberShotExpression(const QString& shot);
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
    QVector<InternalWebBookmark> savedInternalWebPages() const;
    void saveInternalWebPages(
        const QVector<InternalWebBookmark>& bookmarks) const;
    void addInternalWebPage();
    void editInternalWebPage();
    void removeInternalWebPage();
    void refreshInternalWebMenu();
    bool prepareSshLayout(const LayoutConfig& source, LayoutConfig* prepared);
    bool prepareSshUrl(const QString& source, QString* prepared);
    void updateSshActionIcon();
    void openAboutDialog();
    void applyLoginSuccessStatus(const QString& statusText);
    void updateLoginActionIcon();
    void fetchLatestShotAsync(bool applyLatest = true);
    void updateShotControlsFromConfig(const QString& preferredShot = {});
    void setAllPlotShots(const QString& shot);
    QString maxShotInConfig() const;
    QString latestShotFromApi(const QString& apiOverride = {}) const;
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
    void queuePanelRefresh(PanelRefreshRequest request);
    void queueLoadedSignal(LoadedSignal item);
    void flushQueuedLoadedSignals();
    void applyLoadedSignal(LoadedSignal item);
    void applyLoadedSignals(const QVector<LoadedSignal>& loaded);
    void applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded);
    void fitRateRefreshPanelIfComplete(int column, int row);
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
                              const QPoint& pos);
    void updateGlobalRateControl();
    void openLayoutSetupDialog();
    void openCustomizeDialog();
    void applyUiFont();
    void refreshPlotFonts();
    void saveCurrentEnvironment();
    void saveCurrentEnvironmentAs();
    bool saveEnvironmentFile(const QString& path);
    bool saveWebscpEnvironmentFile(const QString& path) const;
    PlotSpec defaultPlotFromSelection() const;
    void updateTopInfoLabels();
    bool loadShotSummaryFromApi(const QString& shot,
                                QString* ip,
                                QString* pulse,
                                QString* it,
                                QString* time,
                                const QString& apiOverride = {}) const;
    void setStatus(const QString& text);
    PlotWidget* currentPlotWidget() const;
    void schedulePointSync(PlotWidget* source, double x);
    void scheduleTopInfoUpdate(const QString& shot);
    void syncDisplayConfig();

    QString rootPath_;
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
    SshTunnelManager* sshTunnelManager_ = nullptr;
    QString cachedApiSourceUrl_;
    QString cachedPreparedApiUrl_;
    QToolButton* openButton_ = nullptr;
    QToolButton* recentEnvironmentButton_ = nullptr;
    QToolButton* aboutButton_ = nullptr;
    QToolButton* zoomButton_ = nullptr;
    QToolButton* pointButton_ = nullptr;
    QVector<QVector<PlotWidget*>> plotWidgets_;
    std::unique_ptr<RefreshCoordinator> refresh_;
    QTimer latestShotPollTimer_;
    QString topSummaryShot_;
    QString topSummaryIp_;
    QString topSummaryPulse_;
    QString topSummaryIt_;
    QString topSummaryTime_;
    QString pendingTopSummaryShot_;
    int topSummaryGeneration_ = 0;
    int selectedColumn_ = -1;
    int selectedRow_ = -1;
    bool singlePanelMaximized_ = false;
    int maximizedColumn_ = -1;
    int maximizedRow_ = -1;
    InteractionMode currentInteractionMode_ = InteractionMode::Zoom;
    QPushButton* stopButton_ = nullptr;
    QSet<QString> rememberedSourceSignals_;
    QString latestShot_;
    bool latestShotFetchRunning_ = false;
    bool latestShotApplyPending_ = false;
    int latestShotGeneration_ = 0;
    PlotWidget* activePointPlot_ = nullptr;
    PlotWidget* pointSyncSource_ = nullptr;
    bool pointSyncQueued_ = false;
    int pointSyncGeneration_ = 0;
    double pendingPointX_ = qQNaN();
};

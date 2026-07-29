// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app_types.hpp"
#include "mds_support.hpp"
#include "ssh_settings.hpp"

#include <QtConcurrent>

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QComboBox>
#include <QColorDialog>
#include <QCheckBox>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRadioButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSemaphore>
#include <QSettings>
#include <QSaveFile>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStandardPaths>
#include <QStringListModel>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVariantAnimation>
#include <QFontComboBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#ifndef MDSSCOPE_VERSION
#define MDSSCOPE_VERSION "unknown"
#endif

#ifndef MDSSCOPE_GIT_VERSION
#define MDSSCOPE_GIT_VERSION "unknown"
#endif

struct FontSettings {
    QString family = QStringLiteral("Times New Roman");
    int iconSize = 24;
#ifdef Q_OS_WIN
    int legendSize = 10;
    int axisSize = 10;
    int unitSize = 10;
    int uiSize = 10;
#else
    int legendSize = 14;
    int axisSize = 14;
    int unitSize = 14;
    int uiSize = 14;
#endif
};

struct ApiLoginResult {
    bool ok = false;
    QString token;
    QString error;
};

struct CachedAuth {
    QString userName;
    QString password;
    QString token;
    SshSettings ssh;
};

QString appConfigDir();
QString appCacheDir();
QString appEnvironmentDir(const QString& rootPath);
QString appSourceIndexDir(const QString& rootPath);
bool ensureSourceIndexCache(const QString& rootPath);
bool addSourceIndexSignal(const QString& tree, const QString& signal);
QString defaultExportBaseDir();
QString uiSettingsPath(const QString& rootPath);
FontSettings& fontSettings();
void loadFontSettings(const QString& rootPath);
void saveFontSettings(const QString& rootPath);
QHash<QString, QString> readKeyValueFile(const QString& path);
QString apiUrlPath(const QString& rootPath);
QString readApiUrl(const QString& rootPath);
QHash<QString, QString> readApiSettings(const QString& rootPath);
bool tokenExpiresSoon(const QString& token);
QString authCachePath();
bool loadCachedAuth(CachedAuth* auth);
bool saveCachedAuth(const CachedAuth& auth);
bool clearCachedApiAuth();
ApiLoginResult requestApiToken(const QString& api,
                               const QString& charset,
                               const QString& userName,
                               const QString& password);
QString firstShotLikeText(const QString& text);
QString firstShotFromJsonValue(const QJsonValue& value);
QString colorForIndex(int index);
bool isDefaultSeriesColor(const QString& colorName, int index);
int colorIndexForName(const QString& colorName, int fallback);
void normalizePresetColors(QVector<SignalSpec>& specs);
QStringList uniformAxisValues(const QVector<double>& values);
QIcon appIcon();
QIcon layoutIcon();
QIcon appearanceIcon();
QIcon openFileIcon();
QIcon saveIcon();
QIcon exportDataIcon();
QIcon refreshIcon();
QIcon loginIcon(bool loggedIn);
QIcon sshIcon(int state);
QIcon browserIcon();
QIcon modeIcon(InteractionMode mode, bool active);
LayoutConfig parseEnvironment(const QString& path, QString* error = nullptr);
bool writeEnvironmentToml(const LayoutConfig& config, const QString& path, QString* error = nullptr);
void writeLine(QTextStream& out, const QString& key, const QString& value);
QString escapedMdsExpr(QString expr);

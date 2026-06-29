#pragma once

#include "mdsscope_app.h"

#include <QtConcurrent>

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QComboBox>
#include <QColorDialog>
#include <QCheckBox>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRadioButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSemaphore>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QFontComboBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

constexpr int kMdsPort = 8000;
constexpr int kNetworkTimeoutMs = 2500;

struct FontSettings {
    QString family = QStringLiteral("Times New Roman");
    int legendSize = 10;
    int axisSize = 10;
    int unitSize = 10;
    int uiSize = 9;
};

void traceMdsLine(const QString& line);
QString appConfigDir();
QString appEnvironmentDir(const QString& rootPath);
QString defaultExportBaseDir();
QString credentialsPath(const QString& rootPath);
QString uiSettingsPath(const QString& rootPath);
FontSettings& fontSettings();
void loadFontSettings(const QString& rootPath);
void saveFontSettings(const QString& rootPath);
QHash<QString, QString> readKeyValueFile(const QString& path);
QString apiPropertiesPath(const QString& rootPath);
QHash<QString, QString> readApiProperties(const QString& rootPath);
QString firstShotLikeText(const QString& text);
QString firstShotFromJsonValue(const QJsonValue& value);
QString colorForIndex(int index);
bool isDefaultSeriesColor(const QString& colorName, int index);
int colorIndexForName(const QString& colorName, int fallback);
void normalizePresetColors(QVector<SignalSpec>& specs);
QString compactAxisValue(double value);
QStringList uniformAxisValues(const QVector<double>& values);
QIcon gearIcon();
QIcon fontIcon();
QIcon saveIcon();
QIcon modeIcon(InteractionMode mode, bool active);
LayoutConfig parseEnvironment(const QString& path);
bool writeEnvironmentToml(const LayoutConfig& config, const QString& path, QString* error = nullptr);
void writeLine(QTextStream& out, const QString& key, const QString& value);
QString escapedMdsExpr(QString expr);
QString normalizedMdsSignal(QString expr);
QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig);

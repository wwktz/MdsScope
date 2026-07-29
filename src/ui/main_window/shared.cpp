// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared.hpp"
#include "core/mds_helpers.hpp"

#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

void setLabelTextIfChanged(QLabel* label, const QString& text)
{
    if (label && label->text() != text) {
        label->setText(text);
    }
}

void clearCustomRanges(PlotSpec* plot)
{
    if (!plot) {
        return;
    }
    plot->customXRange = false;
    plot->customYRange = false;
    plot->xmin = qQNaN();
    plot->xmax = qQNaN();
    plot->ymin = qQNaN();
    plot->ymax = qQNaN();
}

bool loadedSignalMatchesConfig(const LayoutConfig& config, const LoadedSignal& item)
{
    if (item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= config.columns.size()
        || item.row >= config.columns[item.column].size()
        || item.signal >= config.columns[item.column][item.row].signalSpecs.size()) {
        return false;
    }
    const PlotSpec& plot = config.columns[item.column][item.row];
    const SignalSpec& sig = plot.signalSpecs[item.signal];
    if (sig.hidden) {
        return false;
    }
    if (item.shot != effectiveSignalShot(plot, sig)) {
        return false;
    }
    const QString expectedName = normalizedMdsSignal(sig.yExpr);
    return item.series.name.isEmpty() || item.series.name == expectedName;
}

QString signalRefreshSignature(const SignalSpec& sig)
{
    return QStringList{
        sig.shot,
        sig.yExpr,
        sig.xExpr,
        sig.experiment,
        sig.serverIp,
        sig.colorName,
        sig.manualColor ? QStringLiteral("manual-color") : QStringLiteral("auto-color"),
        sig.hidden ? QStringLiteral("hidden") : QStringLiteral("shown"),
        QString::number(static_cast<int>(sig.readMode)),
        sig.readModeExplicit ? QStringLiteral("explicit-rate") : QStringLiteral("inherited-rate"),
    }.join(QChar(0x1f));
}

QString plotRefreshSignature(const PlotSpec& plot)
{
    QStringList parts{
        plot.shot,
        plot.title,
        plot.xLabel,
        plot.yLabel,
        QString::number(plot.extractionPoints),
        plot.grid ? QStringLiteral("grid") : QStringLiteral("no-grid"),
        plot.customXRange ? QStringLiteral("custom-x") : QStringLiteral("auto-x"),
        plot.customYRange ? QStringLiteral("custom-y") : QStringLiteral("auto-y"),
        QString::number(plot.xmin, 'g', 17),
        QString::number(plot.xmax, 'g', 17),
        QString::number(plot.ymin, 'g', 17),
        QString::number(plot.ymax, 'g', 17),
    };
    for (const SignalSpec& sig : plot.signalSpecs) {
        parts.push_back(signalRefreshSignature(sig));
    }
    return parts.join(QChar(0x1e));
}

QString layoutRefreshSignature(const LayoutConfig& config)
{
    QStringList parts{config.filePath};
    for (int c = 0; c < config.columns.size(); ++c) {
        parts.push_back(QStringLiteral("col:%1").arg(c));
        for (int r = 0; r < config.columns[c].size(); ++r) {
            parts.push_back(QStringLiteral("row:%1").arg(r));
            parts.push_back(plotRefreshSignature(config.columns[c][r]));
        }
    }
    return parts.join(QChar(0x1d));
}

bool signalDataSourceEqual(const SignalSpec& lhs, const SignalSpec& rhs)
{
    return lhs.shot == rhs.shot
           && lhs.yExpr == rhs.yExpr
           && lhs.xExpr == rhs.xExpr
           && lhs.experiment == rhs.experiment
           && lhs.serverIp == rhs.serverIp
           && lhs.hidden == rhs.hidden
           && lhs.readMode == rhs.readMode;
}

bool signalDataSourcesEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        if (!signalDataSourceEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool signalSpecEqual(const SignalSpec& lhs, const SignalSpec& rhs)
{
    return signalDataSourceEqual(lhs, rhs)
           && lhs.colorName == rhs.colorName
           && lhs.manualColor == rhs.manualColor
           && lhs.readModeExplicit == rhs.readModeExplicit
           && lhs.hidden == rhs.hidden;
}

bool signalSpecsEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        if (!signalSpecEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool optionalDoubleFromText(const QString& text, double* value)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *value = qQNaN();
        return true;
    }
    bool ok = false;
    const double parsed = trimmed.toDouble(&ok);
    if (ok) {
        *value = parsed;
    }
    return ok;
}

QIcon infoIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 28.0, 32.0 / 28.0);

    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    const QColor fill = Qt::transparent;
    const QColor border = dark ? QColor("#cbd5e1") : QColor("#475569");
    const QColor glyph = dark ? QColor("#cbd5e1") : QColor("#475569");
    const QColor highlight = dark ? QColor("#e5e7eb") : QColor("#334155");

    painter.setPen(QPen(border, 1.8));
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(1.9, 1.9, 24.2, 24.2));

    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    painter.drawEllipse(QPointF(14.0, 8.3), 1.9, 1.9);

    painter.setBrush(glyph);
    painter.drawRoundedRect(QRectF(12.45, 11.6, 3.1, 10.4), 1.55, 1.55);
    return QIcon(pixmap);
}

QIcon recentArrowIcon()
{
    QPixmap pixmap(16, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(4.0 / 3.0, 4.0 / 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QApplication::palette().color(QPalette::ButtonText));
    painter.drawPolygon(QPolygonF{
        QPointF(1.5, 12.5),
        QPointF(10.5, 12.5),
        QPointF(6.0, 18.0),
    });
    return QIcon(pixmap);
}

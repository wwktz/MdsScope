// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/visuals.hpp"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <algorithm>

static QPixmap appIconPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = static_cast<double>(size);
    const QRectF outer(s * 0.07, s * 0.07, s * 0.86, s * 0.86);

    QPainterPath body;
    body.addRoundedRect(outer, s * 0.18, s * 0.18);
    QLinearGradient background(outer.topLeft(), outer.bottomRight());
    background.setColorAt(0.0, QColor("#0f172a"));
    background.setColorAt(0.55, QColor("#1e293b"));
    background.setColorAt(1.0, QColor("#0b1120"));
    painter.setPen(QPen(QColor("#334155"), std::max(1.0, s * 0.025)));
    painter.setBrush(background);
    painter.drawPath(body);

    painter.save();
    painter.setClipPath(body);

    const QRectF plot(s * 0.20, s * 0.22, s * 0.61, s * 0.55);
    painter.setPen(QPen(QColor(148, 163, 184, 58), std::max(1.0, s * 0.009)));
    for (int i = 1; i <= 3; ++i) {
        const double x = plot.left() + plot.width() * i / 4.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const double y = plot.top() + plot.height() * i / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(QPen(QColor("#64748b"), std::max(1.0, s * 0.018), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));

    QPainterPath slowWave;
    slowWave.moveTo(plot.left(), plot.center().y() + plot.height() * 0.13);
    slowWave.cubicTo(plot.left() + plot.width() * 0.18, plot.top() + plot.height() * 0.08,
                     plot.left() + plot.width() * 0.37, plot.bottom() - plot.height() * 0.08,
                     plot.left() + plot.width() * 0.55, plot.center().y() + plot.height() * 0.02);
    slowWave.cubicTo(plot.left() + plot.width() * 0.70, plot.top() + plot.height() * 0.12,
                     plot.left() + plot.width() * 0.83, plot.bottom() - plot.height() * 0.17,
                     plot.right(), plot.top() + plot.height() * 0.22);
    painter.setPen(QPen(QColor("#22d3ee"), std::max(1.6, s * 0.05), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(slowWave);

    QPainterPath fastWave;
    fastWave.moveTo(plot.left() + plot.width() * 0.04, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.19, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.28, plot.top() + plot.height() * 0.20);
    fastWave.lineTo(plot.left() + plot.width() * 0.39, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.57, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.68, plot.top() + plot.height() * 0.30);
    fastWave.lineTo(plot.left() + plot.width() * 0.78, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.right() - plot.width() * 0.04, plot.bottom() - plot.height() * 0.18);
    painter.setPen(QPen(QColor("#f97316"), std::max(1.4, s * 0.036), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(fastWave);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#f8fafc"));
    painter.drawEllipse(QPointF(plot.left() + plot.width() * 0.55, plot.center().y() + plot.height() * 0.02),
                         s * 0.035,
                         s * 0.035);
    painter.setBrush(QColor("#f97316"));
    painter.drawEllipse(QPointF(plot.left() + plot.width() * 0.68, plot.top() + plot.height() * 0.30),
                         s * 0.03,
                         s * 0.03);
    painter.restore();

    painter.setPen(QPen(QColor(255, 255, 255, 42), std::max(1.0, s * 0.014)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer.adjusted(s * 0.035, s * 0.035, -s * 0.035, -s * 0.035), s * 0.13, s * 0.13);
    return pixmap;
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addPixmap(appIconPixmap(size));
    }
    return icon;
}

QIcon layoutIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);
    const QColor color =
        QApplication::palette().color(QPalette::ButtonText);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(3.5, 3.5, 6.8, 6.8), 0.8, 0.8);
    painter.drawRoundedRect(QRectF(13.7, 3.5, 6.8, 6.8), 0.8, 0.8);
    painter.drawRoundedRect(QRectF(3.5, 13.7, 6.8, 6.8), 0.8, 0.8);

    painter.setPen(
        QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(14.3, 17.1), QPointF(19.9, 17.1));
    painter.drawLine(QPointF(17.1, 14.3), QPointF(17.1, 19.9));
    return QIcon(pixmap);
}

QIcon appearanceIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPalette palette = QApplication::palette();
    const QColor line = palette.color(QPalette::ButtonText);
    const QColor knob = palette.color(QPalette::Highlight);
    painter.setPen(
        QPen(line, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(4.0, 8.0), QPointF(28.0, 8.0));
    painter.drawLine(QPointF(4.0, 16.0), QPointF(28.0, 16.0));
    painter.drawLine(QPointF(4.0, 24.0), QPointF(28.0, 24.0));
    painter.setPen(QPen(line, 1.4));
    painter.setBrush(knob);
    painter.drawEllipse(QPointF(10.0, 8.0), 3.2, 3.2);
    painter.drawEllipse(QPointF(21.0, 16.0), 3.2, 3.2);
    painter.drawEllipse(QPointF(14.0, 24.0), 3.2, 3.2);
    return QIcon(pixmap);
}

QIcon openFileIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath back;
    back.moveTo(2.0, 14.5);
    back.lineTo(2.0, 7.0);
    back.cubicTo(2.0, 5.2, 3.3, 4.0, 5.2, 4.0);
    back.lineTo(11.7, 4.0);
    back.cubicTo(12.8, 4.0, 13.4, 4.4, 14.2, 5.2);
    back.lineTo(15.5, 6.5);
    back.lineTo(26.8, 6.5);
    back.cubicTo(28.8, 6.5, 30.0, 7.8, 30.0, 9.7);
    back.lineTo(30.0, 15.0);
    back.closeSubpath();
    painter.setPen(QPen(QColor("#8f2305"), 0.8));
    painter.setBrush(QColor("#a22805"));
    painter.drawPath(back);

    QPainterPath front;
    front.moveTo(3.9, 12.8);
    front.lineTo(28.3, 12.8);
    front.cubicTo(30.0, 12.8, 31.0, 14.1, 30.7, 15.8);
    front.lineTo(28.8, 25.8);
    front.cubicTo(28.5, 27.3, 27.4, 28.0, 25.8, 28.0);
    front.lineTo(4.3, 28.0);
    front.cubicTo(2.5, 28.0, 1.5, 26.9, 1.5, 25.2);
    front.lineTo(1.5, 15.3);
    front.cubicTo(1.5, 13.8, 2.4, 12.8, 3.9, 12.8);
    front.closeSubpath();
    painter.setPen(QPen(QColor("#b0441f"), 0.8));
    painter.setBrush(QColor("#da5b2a"));
    painter.drawPath(front);
    return QIcon(pixmap);
}

QIcon saveIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 22.0, 32.0 / 22.0);
    painter.setPen(QPen(QColor("#334155"), 1.6));
    painter.setBrush(QColor("#e2e8f0"));
    painter.drawRoundedRect(QRectF(2.5, 2.0, 17.0, 18.5), 1.8, 1.8);
    painter.setBrush(QColor("#475569"));
    painter.drawRect(QRectF(5.0, 4.5, 11.0, 5.8));
    painter.setBrush(QColor("#ffffff"));
    painter.drawRect(QRectF(6.0, 13.2, 10.0, 5.2));
    painter.setPen(QPen(QColor("#94a3b8"), 1.0));
    painter.drawLine(QPointF(7.8, 15.3), QPointF(14.2, 15.3));
    painter.drawLine(QPointF(7.8, 17.0), QPointF(14.2, 17.0));
    return QIcon(pixmap);
}

QIcon exportDataIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient arrowGradient(0.0, 4.0, 0.0, 26.0);
    arrowGradient.setColorAt(0.0, QColor("#d4ee72"));
    arrowGradient.setColorAt(1.0, QColor("#9ccd38"));
    QPainterPath arrow;
    arrow.moveTo(12.2, 4.5);
    arrow.lineTo(19.8, 4.5);
    arrow.lineTo(19.8, 16.0);
    arrow.lineTo(25.8, 16.0);
    arrow.lineTo(16.0, 26.0);
    arrow.lineTo(6.2, 16.0);
    arrow.lineTo(12.2, 16.0);
    arrow.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(arrowGradient);
    painter.drawPath(arrow);

    QRadialGradient landingGradient(QPointF(16.0, 28.2), 10.5);
    landingGradient.setColorAt(0.0, QColor("#b7df52"));
    landingGradient.setColorAt(1.0, QColor("#83c61f"));
    painter.setBrush(landingGradient);
    painter.drawEllipse(QRectF(6.0, 26.3, 20.0, 4.1));
    return QIcon(pixmap);
}

static QPixmap refreshIconPixmap(const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);

    painter.setPen(
        QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(3.5, 3.5, 17.0, 17.0),
                    28 * 16,
                    304 * 16);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    QPainterPath arrow;
    arrow.moveTo(23.0, 10.5);
    arrow.lineTo(18.4, 1.8);
    arrow.lineTo(13.8, 7.6);
    arrow.closeSubpath();
    painter.drawPath(arrow);
    return pixmap;
}

QIcon refreshIcon()
{
    const QPalette palette = QApplication::palette();
    QIcon icon;
    icon.addPixmap(
        refreshIconPixmap(palette.color(QPalette::Highlight)),
        QIcon::Normal);
    icon.addPixmap(
        refreshIconPixmap(
            palette.color(QPalette::Disabled, QPalette::ButtonText)),
        QIcon::Disabled);
    return icon;
}

QIcon loginIcon(bool loggedIn)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 22.0, 32.0 / 22.0);

    const QColor frame = loggedIn ? QColor("#15803d") : QColor("#64748b");
    const QColor fill = loggedIn ? QColor("#dcfce7") : QColor("#e2e8f0");
    const QColor person = loggedIn ? QColor("#16a34a") : QColor("#94a3b8");

    painter.setPen(QPen(frame, 1.5));
    painter.drawLine(QPointF(6.5, 1.3), QPointF(11.0, 4.0));
    painter.drawLine(QPointF(15.5, 1.3), QPointF(11.0, 4.0));

    painter.setPen(QPen(frame, 1.6));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(1.5, 3.5, 19.0, 17.7), 2.0, 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    painter.drawRoundedRect(QRectF(6.7, 2.7, 8.6, 2.7), 0.9, 0.9);

    painter.setBrush(person);
    painter.drawEllipse(QPointF(11.0, 10.3), 3.2, 3.2);

    QPainterPath shoulders;
    shoulders.moveTo(4.2, 18.0);
    shoulders.cubicTo(5.3, 13.1, 16.7, 13.1, 17.8, 18.0);
    shoulders.lineTo(17.8, 19.2);
    shoulders.lineTo(4.2, 19.2);
    shoulders.closeSubpath();
    painter.drawPath(shoulders);

    return QIcon(pixmap);
}

QIcon sshIcon(int state)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 22.0, 32.0 / 22.0);
    static const QColor colors[] = {
        QColor("#64748b"), QColor("#2563eb"), QColor("#d97706"),
        QColor("#16a34a"), QColor("#dc2626")
    };
    const QColor color = colors[std::clamp(state, 0, 4)];
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.5, 3.5, 19.0, 17.0), 2.0, 2.0);
    painter.drawLine(QPointF(5.5, 8.0), QPointF(8.5, 11.0));
    painter.drawLine(QPointF(8.5, 11.0), QPointF(5.5, 14.0));
    painter.drawLine(QPointF(11.5, 15.0), QPointF(16.5, 15.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(18.5, 4.0), 2.5, 2.5);
    return QIcon(pixmap);
}

QIcon browserIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 22.0, 32.0 / 22.0);

    const QColor frame("#2563eb");
    painter.setPen(QPen(frame, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.2, 2.0, 19.6, 18.0), 2.0, 2.0);
    painter.drawLine(QPointF(1.7, 7.0), QPointF(20.3, 7.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    painter.drawEllipse(QPointF(5.0, 4.7), 0.85, 0.85);
    painter.drawEllipse(QPointF(8.0, 4.7), 0.85, 0.85);
    painter.drawEllipse(QPointF(11.0, 4.7), 0.85, 0.85);

    painter.setPen(QPen(frame, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(11.0, 13.7), 4.4, 4.0);
    painter.drawLine(QPointF(6.8, 13.7), QPointF(15.2, 13.7));
    painter.drawArc(QRectF(9.0, 9.7, 4.0, 8.0), 90 * 16, 180 * 16);
    painter.drawArc(QRectF(9.0, 9.7, 4.0, 8.0), 270 * 16, 180 * 16);
    return QIcon(pixmap);
}

QIcon modeIcon(InteractionMode mode, bool active)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(32.0 / 24.0, 32.0 / 24.0);
    const QPalette palette = QApplication::palette();
    const QColor color =
        active ? palette.color(QPalette::Highlight)
               : palette.color(QPalette::ButtonText);
    painter.setPen(QPen(color, 2.3));
    painter.setBrush(Qt::NoBrush);
    if (mode == InteractionMode::Zoom) {
        painter.drawEllipse(QRectF(4.0, 4.0, 11.8, 11.8));
        painter.drawLine(QPointF(14.5, 14.5), QPointF(19.8, 19.8));
        painter.drawLine(QPointF(7.2, 9.9), QPointF(12.6, 9.9));
        painter.drawLine(QPointF(9.9, 7.2), QPointF(9.9, 12.6));
    } else if (mode == InteractionMode::Point) {
        painter.setPen(
            QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawEllipse(QPointF(12.0, 12.0), 5.7, 5.7);
        painter.drawLine(QPointF(12.0, 2.8), QPointF(12.0, 5.0));
        painter.drawLine(QPointF(12.0, 19.0), QPointF(12.0, 21.2));
        painter.drawLine(QPointF(2.8, 12.0), QPointF(5.0, 12.0));
        painter.drawLine(QPointF(19.0, 12.0), QPointF(21.2, 12.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12.0, 12.0), 1.7, 1.7);
    } else {
        painter.setPen(color);
        QFont iconFont = painter.font();
        iconFont.setPointSize(17);
        painter.setFont(iconFont);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromUtf8("✋"));
    }
    return QIcon(pixmap);
}

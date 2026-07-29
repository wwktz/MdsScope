// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "series_style.hpp"

#include <QColor>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

QString colorForIndex(int index)
{
    static const QStringList colors = {
        "#2364aa", "#c44e52", "#2f855a", "#805ad5", "#d97706",
        "#0f766e", "#9f1239", "#4a5568", "#db2777", "#16a34a",
        "#ea580c", "#0891b2", "#7c3aed", "#ca8a04", "#0ea5e9",
        "#be123c"
    };
    return colors.at(index % colors.size());
}

bool isDefaultSeriesColor(const QString& colorName, int index)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return true;
    }
    return color.name().compare(
               QColor(colorForIndex(index)).name(),
               Qt::CaseInsensitive)
           == 0;
}

int colorIndexForName(const QString& colorName, int fallback)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return fallback;
    }
    const QString normalized = color.name().toLower();
    for (int i = 0; i < 32; ++i) {
        if (QColor(colorForIndex(i)).name().toLower()
            == normalized) {
            return i;
        }
    }
    return fallback;
}

void normalizePresetColors(QVector<SignalSpec>& specs)
{
    for (int i = 0; i < specs.size(); ++i) {
        if (!specs[i].manualColor) {
            specs[i].colorName = colorForIndex(i);
        }
    }
}

QStringList uniformAxisValues(const QVector<double>& values)
{
    QStringList labels;
    labels.reserve(values.size());
    bool scientific = false;
    double minStep = std::numeric_limits<double>::infinity();
    for (int i = 0; i < values.size(); ++i) {
        const double value = values[i];
        if (!std::isfinite(value)) {
            labels.push_back(QString());
            continue;
        }
        const double absValue = std::abs(value);
        if (absValue >= 1000.0
            || (absValue > 0.0 && absValue < 0.001)) {
            scientific = true;
        }
        if (i > 0 && std::isfinite(values[i - 1])) {
            const double step =
                std::abs(value - values[i - 1]);
            if (step > 0.0) {
                minStep = std::min(minStep, step);
            }
        }
        labels.push_back(QString());
    }

    int decimals = 0;
    if (scientific) {
        decimals = 2;
    } else if (std::isfinite(minStep) && minStep > 0.0) {
        if (minStep >= 10.0) {
            decimals = 0;
        } else {
            decimals = std::clamp(
                static_cast<int>(
                    std::ceil(-std::log10(minStep)))
                    + 1,
                0,
                5);
        }
    }

    auto formatted = [&](int precision) {
        QStringList out;
        out.reserve(values.size());
        for (double value : values) {
            out.push_back(
                std::isfinite(value)
                    ? QString::number(
                          value,
                          scientific ? 'e' : 'f',
                          precision)
                    : QString());
        }
        return out;
    };

    labels = formatted(decimals);
    while (decimals < 15) {
        QSet<QString> seen;
        bool duplicate = false;
        for (const QString& label : labels) {
            if (label.isEmpty()) {
                continue;
            }
            if (seen.contains(label)) {
                duplicate = true;
                break;
            }
            seen.insert(label);
        }
        if (!duplicate) {
            break;
        }
        labels = formatted(++decimals);
    }
    return labels;
}

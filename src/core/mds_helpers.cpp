// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

void writeLine(QTextStream& out, const QString& key, const QString& value)
{
    out << key << ':' << value << '\n';
}

QString escapedMdsExpr(QString expr)
{
    expr = expr.trimmed();
    expr.replace("\"", "\\\"");
    return expr;
}

QString normalizedMdsSignal(QString expr)
{
    expr = expr.trimmed();
    while (expr.startsWith("\\\\")) {
        expr.remove(0, 1);
    }
    return expr;
}

QStringList sourceIndexSignalNames(const QString& expression)
{
    static const QRegularExpression nodePattern(
        QStringLiteral(R"(\\[A-Za-z][A-Za-z0-9_$:.]*)"));

    QStringList nodeNames;
    QSet<QString> seen;
    QRegularExpressionMatchIterator matches = nodePattern.globalMatch(expression);
    while (matches.hasNext()) {
        const QString signal = normalizedMdsSignal(matches.next().captured()).trimmed();
        const QString key = signal.toLower();
        if (!signal.isEmpty() && !seen.contains(key)) {
            nodeNames.push_back(signal);
            seen.insert(key);
        }
    }
    if (nodeNames.isEmpty()) {
        const QString bareSignal = expression.trimmed();
        static const QRegularExpression bareNodePattern(
            QStringLiteral(R"(^[A-Za-z][A-Za-z0-9_$:.]*$)"));
        if (bareNodePattern.match(bareSignal).hasMatch()) {
            nodeNames.push_back(QChar('\\') + bareSignal);
        }
    }
    return nodeNames;
}

QString scaledSiUnit(QString unit, double numericScale)
{
    unit = unit.trimmed();
    const double absoluteScale = std::abs(numericScale);
    if (unit.isEmpty() || !std::isfinite(absoluteScale) || absoluteScale == 0.0) {
        return unit;
    }

    // Dividing numeric values by 1000 changes J to kJ; multiplying them by
    // 1000 changes J to mJ. Only exact powers of 1000 are converted so an
    // arbitrary calibration factor never produces a guessed unit.
    const double prefixStepsValue = -std::log(absoluteScale) / std::log(1000.0);
    const int prefixSteps = static_cast<int>(std::llround(prefixStepsValue));
    if (std::abs(prefixStepsValue - static_cast<double>(prefixSteps)) > 1e-10
        || prefixSteps == 0) {
        return unit;
    }

    struct Prefix {
        QString symbol;
        int steps = 0; // Each step represents a factor of 1000.
    };
    static const QVector<Prefix> prefixes = {
        {QStringLiteral("Y"), 8},
        {QStringLiteral("Z"), 7},
        {QStringLiteral("E"), 6},
        {QStringLiteral("P"), 5},
        {QStringLiteral("T"), 4},
        {QStringLiteral("G"), 3},
        {QStringLiteral("M"), 2},
        {QStringLiteral("k"), 1},
        {QStringLiteral("m"), -1},
        {QStringLiteral("u"), -2},
        {QStringLiteral("µ"), -2},
        {QStringLiteral("μ"), -2},
        {QStringLiteral("n"), -3},
        {QStringLiteral("p"), -4},
        {QStringLiteral("f"), -5},
        {QStringLiteral("a"), -6},
        {QStringLiteral("z"), -7},
        {QStringLiteral("y"), -8},
    };
    static const QStringList prefixableUnits = {
        QStringLiteral("mol"),
        QStringLiteral("kat"),
        QStringLiteral("rad"),
        QStringLiteral("bar"),
        QStringLiteral("Ohm"),
        QStringLiteral("Bq"),
        QStringLiteral("Gy"),
        QStringLiteral("Sv"),
        QStringLiteral("Hz"),
        QStringLiteral("Pa"),
        QStringLiteral("Wb"),
        QStringLiteral("eV"),
        QStringLiteral("lm"),
        QStringLiteral("lx"),
        QStringLiteral("sr"),
        QStringLiteral("Ω"),
        QStringLiteral("W"),
        QStringLiteral("J"),
        QStringLiteral("V"),
        QStringLiteral("A"),
        QStringLiteral("s"),
        QStringLiteral("g"),
        QStringLiteral("m"),
        QStringLiteral("K"),
        QStringLiteral("C"),
        QStringLiteral("N"),
        QStringLiteral("F"),
        QStringLiteral("S"),
        QStringLiteral("T"),
        QStringLiteral("H"),
    };
    const auto beginsWithUnit = [](const QString& text) {
        for (const QString& base : prefixableUnits) {
            if (!text.startsWith(base)) {
                continue;
            }
            if (text.size() == base.size()) {
                return true;
            }
            const QChar next = text.at(base.size());
            if (next == '/' || next == '*' || next == '^' || next == ' '
                || next == QChar(0x00b7) || next == QChar(0x22c5)) {
                return true;
            }
        }
        return false;
    };

    int currentSteps = 0;
    QString baseUnit = unit;
    for (const Prefix& prefix : prefixes) {
        if (!unit.startsWith(prefix.symbol)) {
            continue;
        }
        const QString candidate = unit.mid(prefix.symbol.size());
        if (beginsWithUnit(candidate)) {
            currentSteps = prefix.steps;
            baseUnit = candidate;
            break;
        }
    }
    if (baseUnit == unit && !beginsWithUnit(baseUnit)) {
        return unit;
    }

    const int targetSteps = currentSteps + prefixSteps;
    if (targetSteps == 0) {
        return baseUnit;
    }
    for (const Prefix& prefix : prefixes) {
        if (prefix.steps == targetSteps) {
            return prefix.symbol + baseUnit;
        }
    }
    return unit;
}

QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig)
{
    const QString shot = sig.shot.trimmed();
    return shot.isEmpty() ? plot.shot.trimmed() : shot;
}

QStringList expandedShotList(const QString& expression)
{
    constexpr int kMaxExpandedShots = 128;
    QStringList out;
    QSet<QString> seen;
    const QStringList parts = expression.split(';', Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty()) {
            continue;
        }

        int dash = -1;
        for (int i = 1; i < part.size(); ++i) {
            if (part.at(i) == '-') {
                dash = i;
                break;
            }
        }

        QStringList shots;
        if (dash > 0) {
            bool startOk = false;
            bool endOk = false;
            const int start = part.left(dash).trimmed().toInt(&startOk);
            const int end = part.mid(dash + 1).trimmed().toInt(&endOk);
            if (startOk && endOk) {
                const int step = start <= end ? 1 : -1;
                for (int shot = start;; shot += step) {
                    shots.push_back(QString::number(shot));
                    if (shot == end || shots.size() >= kMaxExpandedShots) {
                        break;
                    }
                }
            } else {
                shots.push_back(part);
            }
        } else {
            shots.push_back(part);
        }

        for (const QString& shot : std::as_const(shots)) {
            const QString trimmed = shot.trimmed();
            if (trimmed.isEmpty() || seen.contains(trimmed) || out.size() >= kMaxExpandedShots) {
                continue;
            }
            seen.insert(trimmed);
            out.push_back(trimmed);
        }
        if (out.size() >= kMaxExpandedShots) {
            break;
        }
    }
    return out;
}

LayoutConfig expandedShotLayout(const LayoutConfig& config)
{
    LayoutConfig expanded = config;
    for (QVector<PlotSpec>& column : expanded.columns) {
        for (PlotSpec& plot : column) {
            QVector<SignalSpec> expandedSignals;
            expandedSignals.reserve(plot.signalSpecs.size());
            for (const SignalSpec& sig : std::as_const(plot.signalSpecs)) {
                const QStringList shots = expandedShotList(effectiveSignalShot(plot, sig));
                if (shots.size() <= 1) {
                    expandedSignals.push_back(sig);
                    continue;
                }
                for (const QString& shot : shots) {
                    SignalSpec copy = sig;
                    copy.shot = shot;
                    copy.manualColor = false;
                    copy.colorName = colorForIndex(static_cast<int>(expandedSignals.size()));
                    expandedSignals.push_back(std::move(copy));
                }
            }
            plot.signalSpecs = std::move(expandedSignals);
        }
    }
    return expanded;
}

DataReadMode higherDataReadMode(DataReadMode lhs, DataReadMode rhs)
{
    const auto rank = [](DataReadMode mode) {
        switch (mode) {
        case DataReadMode::Thin:
            return 0;
        case DataReadMode::Medium:
            return 1;
        case DataReadMode::Full:
            return 2;
        }
        return 0;
    };
    return rank(lhs) >= rank(rhs) ? lhs : rhs;
}

DataReadMode effectiveSignalReadMode(DataReadMode, const SignalSpec& sig)
{
    // Opening a configuration has already applied the startup default once.
    // From then on each signal stores its actual current Rate, so later global,
    // panel, and source choices are free to raise or lower it.
    return sig.readMode;
}

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "text_utils.hpp"

QString tomlEscape(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    value.replace("\t", "\\t");
    return "\"" + value + "\"";
}

QString tomlUnescape(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
    }
    QString out;
    out.reserve(value.size());
    bool escaped = false;
    for (QChar ch : value) {
        if (escaped) {
            if (ch == 'n') {
                out += '\n';
            } else if (ch == 'r') {
                out += '\r';
            } else if (ch == 't') {
                out += '\t';
            } else {
                out += ch;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            out += ch;
        }
    }
    if (escaped) {
        out += '\\';
    }
    return out;
}

QString stripTomlComment(QString line)
{
    bool inString = false;
    bool escaped = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (ch == '#' && !inString) {
            return line.left(i).trimmed();
        }
    }
    return line.trimmed();
}

bool parseTomlString(const QString& value, QString* parsed)
{
    const QString trimmed = value.trimmed();
    if (trimmed.size() < 2 || trimmed.front() != '"') {
        return false;
    }
    bool escaped = false;
    for (qsizetype i = 1; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            if (i != trimmed.size() - 1) {
                return false;
            }
            if (parsed) {
                *parsed = tomlUnescape(trimmed);
            }
            return true;
        }
    }
    return false;
}

bool parseTomlBool(const QString& value, bool* parsed)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == "true") {
        *parsed = true;
        return true;
    }
    if (normalized == "false") {
        *parsed = false;
        return true;
    }
    return false;
}

bool parseTomlInt(const QString& value, int* parsed)
{
    bool ok = false;
    const int result = value.trimmed().toInt(&ok);
    if (ok) {
        *parsed = result;
    }
    return ok;
}

bool parseTomlDouble(const QString& value, double* parsed)
{
    bool ok = false;
    const double result = value.trimmed().toDouble(&ok);
    if (ok && std::isfinite(result)) {
        *parsed = result;
        return true;
    }
    return false;
}

LayoutConfig parseFailure(const QString& path,
                          qsizetype lineNumber,
                          const QString& message,
                          QString* error)
{
    if (error) {
        *error = lineNumber > 0
                     ? QStringLiteral("%1:%2: %3").arg(path).arg(lineNumber).arg(message)
                     : QStringLiteral("%1: %2").arg(path, message);
    }
    LayoutConfig config;
    config.filePath = path;
    return config;
}

void writeTomlString(QTextStream& out, const QString& key, const QString& value)
{
    out << key << " = " << tomlEscape(value) << '\n';
}

void writeTomlStringIfNotEmpty(QTextStream& out, const QString& key, const QString& value)
{
    if (!value.isEmpty()) {
        writeTomlString(out, key, value);
    }
}

void writeTomlBool(QTextStream& out, const QString& key, bool value)
{
    out << key << " = " << (value ? "true" : "false") << '\n';
}

void writeTomlBoolIfNotDefault(QTextStream& out, const QString& key, bool value, bool defaultValue)
{
    if (value != defaultValue) {
        writeTomlBool(out, key, value);
    }
}

void writeTomlDouble(QTextStream& out, const QString& key, double value)
{
    if (std::isfinite(value)) {
        out << key << " = " << QString::number(value, 'g', 12) << '\n';
    }
}

LayoutConfig parseTomlEnvironment(const QString& path, QString* error)
{
    LayoutConfig config;
    config.filePath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return parseFailure(path,
                            0,
                            QStringLiteral("cannot open file: %1").arg(file.errorString()),
                            error);
    }

    struct PanelSlot {
        int column = 1;
        int row = 1;
        PlotSpec plot;
    };
    QVector<PanelSlot> panels;
    PanelSlot* currentPanel = nullptr;
    SignalSpec* currentSignal = nullptr;
    QString section;
    qsizetype lineNumber = 0;

    while (!file.atEnd()) {
        ++lineNumber;
        const QString line = stripTomlComment(QString::fromUtf8(file.readLine()));
        if (line.isEmpty()) {
            continue;
        }
        if (line == "[[panels]]") {
            PanelSlot panel;
            panels.push_back(std::move(panel));
            currentPanel = &panels.back();
            currentSignal = nullptr;
            section = "panel";
            continue;
        }
        if (line == "[[panels.signals]]") {
            if (!currentPanel) {
                return parseFailure(path,
                                    lineNumber,
                                    QStringLiteral("[[panels.signals]] appears before [[panels]]"),
                                    error);
            }
            SignalSpec sig;
            currentPanel->plot.signalSpecs.push_back(sig);
            currentSignal = &currentPanel->plot.signalSpecs.back();
            section = "signal";
            continue;
        }
        if (line.startsWith('[')) {
            return parseFailure(path,
                                lineNumber,
                                QStringLiteral("unsupported or malformed section '%1'").arg(line),
                                error);
        }

        const qsizetype eq = line.indexOf('=');
        if (eq < 0) {
            return parseFailure(path,
                                lineNumber,
                                QStringLiteral("expected a key/value assignment"),
                                error);
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key.isEmpty()) {
            return parseFailure(path, lineNumber, QStringLiteral("empty key"), error);
        }
        if (value.isEmpty()) {
            return parseFailure(path,
                                lineNumber,
                                QStringLiteral("missing value for '%1'").arg(key),
                                error);
        }

        if (section.isEmpty()) {
            if (key == "version") {
                int version = 0;
                if (!parseTomlInt(value, &version)) {
                    return parseFailure(path,
                                        lineNumber,
                                        QStringLiteral("invalid integer for 'version': %1").arg(value),
                                        error);
                }
                if (version != 1) {
                    return parseFailure(path,
                                        lineNumber,
                                        QStringLiteral("unsupported configuration version %1").arg(version),
                                        error);
                }
            }
            continue;
        }

        if (section == "panel" && currentPanel) {
            PlotSpec& plot = currentPanel->plot;
            if (key == "column" || key == "row" || key == "extraction_points") {
                int parsed = 0;
                if (!parseTomlInt(value, &parsed) || parsed <= 0) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be a positive integer, got %2").arg(key, value),
                        error);
                }
                if (key == "column") currentPanel->column = parsed;
                else if (key == "row") currentPanel->row = parsed;
                else plot.extractionPoints = parsed;
            } else if (key == "shot" || key == "title" || key == "x_label" || key == "y_label") {
                QString parsed;
                if (!parseTomlString(value, &parsed)) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be a quoted string, got %2").arg(key, value),
                        error);
                }
                if (key == "shot") plot.shot = parsed;
                else if (key == "title") plot.title = parsed;
                else if (key == "x_label") plot.xLabel = parsed;
                else plot.yLabel = parsed;
            } else if (key == "grid" || key == "custom_x_range" || key == "custom_y_range") {
                bool parsed = false;
                if (!parseTomlBool(value, &parsed)) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be true or false, got %2").arg(key, value),
                        error);
                }
                if (key == "grid") plot.grid = parsed;
                else if (key == "custom_x_range") plot.customXRange = parsed;
                else plot.customYRange = parsed;
            } else if (key == "xmin" || key == "xmax" || key == "ymin" || key == "ymax") {
                double parsed = 0.0;
                if (!parseTomlDouble(value, &parsed)) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be a finite number, got %2").arg(key, value),
                        error);
                }
                if (key == "xmin") plot.xmin = parsed;
                else if (key == "xmax") plot.xmax = parsed;
                else if (key == "ymin") plot.ymin = parsed;
                else plot.ymax = parsed;
            }
        } else if (section == "signal" && currentSignal) {
            if (key == "shot" || key == "tree" || key == "server" || key == "y"
                || key == "x" || key == "color" || key == "read_mode") {
                QString parsed;
                if (!parseTomlString(value, &parsed)) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be a quoted string, got %2").arg(key, value),
                        error);
                }
                if (key == "shot") currentSignal->shot = parsed;
                else if (key == "tree") currentSignal->experiment = parsed;
                else if (key == "server") currentSignal->serverIp = parsed;
                else if (key == "y") currentSignal->yExpr = parsed;
                else if (key == "x") currentSignal->xExpr = parsed;
                else if (key == "color") currentSignal->colorName = parsed;
                else {
                    const QString mode = parsed.toLower();
                    if (mode == "full") currentSignal->readMode = DataReadMode::Full;
                    else if (mode == "medium") currentSignal->readMode = DataReadMode::Medium;
                    else if (mode == "thin") currentSignal->readMode = DataReadMode::Thin;
                    else {
                        return parseFailure(
                            path,
                            lineNumber,
                            QStringLiteral("'read_mode' must be thin, medium, or full, got %1")
                                .arg(value),
                            error);
                    }
                    currentSignal->readModeExplicit = true;
                }
            } else if (key == "manual_color" || key == "hidden" || key == "full") {
                bool parsed = false;
                if (!parseTomlBool(value, &parsed)) {
                    return parseFailure(
                        path,
                        lineNumber,
                        QStringLiteral("'%1' must be true or false, got %2").arg(key, value),
                        error);
                }
                if (key == "manual_color") currentSignal->manualColor = parsed;
                else if (key == "hidden") currentSignal->hidden = parsed;
                else {
                    currentSignal->readMode =
                        parsed ? DataReadMode::Full : DataReadMode::Thin;
                    currentSignal->readModeExplicit = true;
                }
            }
        }
    }

    if (panels.isEmpty()) {
        return parseFailure(path,
                            0,
                            QStringLiteral("configuration contains no [[panels]] entries"),
                            error);
    }

    int maxColumn = 0;
    for (const PanelSlot& panel : panels) {
        maxColumn = std::max(maxColumn, panel.column);
    }
    config.columns.resize(std::max(1, maxColumn));
    std::sort(panels.begin(), panels.end(), [](const PanelSlot& a, const PanelSlot& b) {
        if (a.column != b.column) {
            return a.column < b.column;
        }
        return a.row < b.row;
    });
    for (PanelSlot& panel : panels) {
        normalizePresetColors(panel.plot.signalSpecs);
        config.columns[panel.column - 1].push_back(std::move(panel.plot));
    }
    return config;
}

LayoutConfig parseWebscpEnvironment(const QString& path, QString* error)
{
    LayoutConfig config;
    config.filePath = path;
    const auto invalidConfig = [&] {
        LayoutConfig invalid;
        invalid.filePath = path;
        return invalid;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return parseFailure(path,
                            0,
                            QStringLiteral("cannot open file: %1").arg(file.errorString()),
                            error);
    }

    QHash<QString, QString> map;
    QHash<QString, qsizetype> keyLines;
    qsizetype lineNumber = 0;
    while (!file.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        qsizetype separator = line.indexOf(':');
        const qsizetype equals = line.indexOf('=');
        if (equals >= 0 && (separator < 0 || equals < separator)) {
            separator = equals;
        }
        if (separator < 0) {
            return parseFailure(path,
                                lineNumber,
                                QStringLiteral("expected a key/value assignment"),
                                error);
        }
        const QString key = line.left(separator).trimmed();
        if (key.isEmpty()) {
            return parseFailure(path, lineNumber, QStringLiteral("empty key"), error);
        }
        map.insert(key, javaUnescape(line.mid(separator + 1).trimmed()));
        keyLines.insert(key, lineNumber);
    }
    if (map.isEmpty()) {
        return parseFailure(path,
                            0,
                            QStringLiteral("configuration contains no key/value entries"),
                            error);
    }

    auto readInteger = [&](const QString& key,
                           int fallback,
                           int minimum,
                           int* parsed) {
        if (!map.contains(key)) {
            *parsed = fallback;
            return true;
        }
        bool ok = false;
        const int value = map.value(key).trimmed().toInt(&ok);
        if (!ok || value < minimum) {
            parseFailure(path,
                         keyLines.value(key),
                         QStringLiteral("'%1' must be an integer of at least %2, got '%3'")
                             .arg(key)
                             .arg(minimum)
                             .arg(map.value(key)),
                         error);
            return false;
        }
        *parsed = value;
        return true;
    };
    auto readRangeValue = [&](const QString& key,
                              const QString& fallbackKey,
                              double* parsed) {
        const QString sourceKey = map.contains(key) ? key : fallbackKey;
        const QString text = map.value(sourceKey).trimmed();
        if (text.isEmpty()) {
            *parsed = qQNaN();
            return true;
        }
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            parseFailure(path,
                         keyLines.value(sourceKey),
                         QStringLiteral("'%1' must be a finite number, got '%2'")
                             .arg(sourceKey, text),
                         error);
            return false;
        }
        *parsed = value;
        return true;
    };

    if (!map.contains("cols")) {
        return parseFailure(path, 0, QStringLiteral("missing required key 'cols'"), error);
    }
    int cols = 0;
    if (!readInteger("cols", 0, 1, &cols)) {
        return invalidConfig();
    }
    config.columns.resize(cols);
    int panelCount = 0;

    for (int c = 1; c <= cols; ++c) {
        const QString rowsKey = QString::number(c) + ".rows";
        if (!map.contains(rowsKey)) {
            return parseFailure(path,
                                0,
                                QStringLiteral("missing required key '%1'").arg(rowsKey),
                                error);
        }
        int rows = 0;
        if (!readInteger(rowsKey, 0, 0, &rows)) {
            return invalidConfig();
        }
        panelCount += rows;
        config.columns[c - 1].reserve(rows);
        for (int r = 1; r <= rows; ++r) {
            const QString prefix = QString("%1_%2.").arg(c).arg(r);
            PlotSpec plot;
            plot.shot = trimQuotes(map.value(prefix + "shot_txt"));
            plot.title = trimQuotes(map.value(prefix + "title"));
            plot.xLabel = trimQuotes(map.value(prefix + "xlabel"));
            plot.yLabel = trimQuotes(map.value(prefix + "ylabel"));
            int defaultExtractionPoints = 2000;
            if (!readInteger("Extraction_points", 2000, 1, &defaultExtractionPoints)
                || !readInteger(prefix + "extraction_points",
                                defaultExtractionPoints,
                                1,
                                &plot.extractionPoints)) {
                return invalidConfig();
            }
            int gridMode = 1;
            int defaultGridMode = 1;
            int xSettingMode = 1;
            int ySettingMode = 1;
            if (!readInteger("Grid_Mode", 1, 0, &defaultGridMode)
                || !readInteger(prefix + "grid_mode", defaultGridMode, 0, &gridMode)
                || !readInteger(prefix + "xseting_mode", 1, 0, &xSettingMode)
                || !readInteger(prefix + "yseting_mode", 1, 0, &ySettingMode)) {
                return invalidConfig();
            }
            plot.grid = gridMode != 0;
            plot.customXRange = xSettingMode == 0;
            plot.customYRange = ySettingMode == 0;
            if (plot.customXRange
                && (!readRangeValue(prefix + "xmin_custom", "xmin", &plot.xmin)
                    || !readRangeValue(prefix + "xmax_custom", "xmax", &plot.xmax))) {
                return invalidConfig();
            }
            if (plot.customYRange
                && (!readRangeValue(prefix + "ymin_custom", "ymin", &plot.ymin)
                    || !readRangeValue(prefix + "ymax_custom", "ymax", &plot.ymax))) {
                return invalidConfig();
            }

            int signalCount = 1;
            if (!readInteger(prefix + "num_sig", 1, 0, &signalCount)) {
                return invalidConfig();
            }
            plot.signalSpecs.reserve(signalCount);
            for (int s = 1; s <= signalCount; ++s) {
                SignalSpec sig;
                sig.shot = trimQuotes(map.value(prefix + QString("shot_%1").arg(s)));
                sig.yExpr = trimQuotes(map.value(prefix + QString("y_expr_%1").arg(s)));
                sig.xExpr = trimQuotes(map.value(prefix + QString("x_expr_%1").arg(s)));
                sig.experiment = trimQuotes(map.value(prefix + QString("experiment_%1").arg(s)));
                sig.serverIp = trimQuotes(map.value(prefix + QString("server_ip_%1").arg(s)));
                const QString colorName = trimQuotes(map.value(prefix + QString("color_name_%1").arg(s)));
                int manualColor = 0;
                if (!readInteger(prefix + QString("color_manual_%1").arg(s),
                                 0,
                                 0,
                                 &manualColor)) {
                    return invalidConfig();
                }
                sig.manualColor = manualColor != 0;
                if (sig.manualColor && !colorName.isEmpty()) {
                    sig.colorName = colorName;
                } else {
                    sig.colorName = colorForIndex(s - 1);
                    sig.manualColor = false;
                }
                if (!sig.yExpr.isEmpty()) {
                    plot.signalSpecs.push_back(sig);
                }
            }
            normalizePresetColors(plot.signalSpecs);
            config.columns[c - 1].push_back(plot);
        }
    }
    if (panelCount == 0) {
        return parseFailure(path, 0, QStringLiteral("configuration contains no panels"), error);
    }
    return config;
}

LayoutConfig parseEnvironment(const QString& path, QString* error)
{
    if (error) {
        error->clear();
    }
    if (path.endsWith(".toml", Qt::CaseInsensitive)) {
        return parseTomlEnvironment(path, error);
    }
    return parseWebscpEnvironment(path, error);
}

bool writeEnvironmentToml(const LayoutConfig& config, const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }

    QTextStream out(&file);
    out << "version = 1\n\n";

    QHash<QString, int> shotCounts;
    for (const auto& column : config.columns) {
        for (const PlotSpec& plot : column) {
            const QString shot = plot.shot.trimmed();
            if (!shot.isEmpty()) {
                shotCounts[shot] += std::max(1, static_cast<int>(plot.signalSpecs.size()));
            }
        }
    }
    QString defaultShot;
    int defaultShotCount = 0;
    for (auto it = shotCounts.cbegin(); it != shotCounts.cend(); ++it) {
        if (it.value() > defaultShotCount) {
            defaultShot = it.key();
            defaultShotCount = it.value();
        }
    }

    for (int c = 0; c < config.columns.size(); ++c) {
        for (int r = 0; r < config.columns[c].size(); ++r) {
            const PlotSpec& plot = config.columns[c][r];
            const QString panelShot = plot.shot.trimmed();
            out << "[[panels]]\n";
            out << "column = " << c + 1 << '\n';
            out << "row = " << r + 1 << '\n';
            writeTomlStringIfNotEmpty(out, "title", plot.title);
            writeTomlStringIfNotEmpty(out, "x_label", plot.xLabel);
            writeTomlStringIfNotEmpty(out, "y_label", plot.yLabel);
            if (plot.extractionPoints != 2000) {
                out << "extraction_points = " << plot.extractionPoints << '\n';
            }
            writeTomlBoolIfNotDefault(out, "grid", plot.grid, true);
            writeTomlBoolIfNotDefault(out, "custom_x_range", plot.customXRange, false);
            writeTomlBoolIfNotDefault(out, "custom_y_range", plot.customYRange, false);
            writeTomlDouble(out, "xmin", plot.xmin);
            writeTomlDouble(out, "xmax", plot.xmax);
            writeTomlDouble(out, "ymin", plot.ymin);
            writeTomlDouble(out, "ymax", plot.ymax);
            out << '\n';

            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                const SignalSpec& sig = plot.signalSpecs[s];
                out << "[[panels.signals]]\n";
                QString signalShot = sig.shot.trimmed();
                if (signalShot.isEmpty() && !panelShot.isEmpty() && panelShot != defaultShot) {
                    signalShot = panelShot;
                }
                if (!signalShot.isEmpty() && signalShot != defaultShot) {
                    writeTomlString(out, "shot", signalShot);
                }
                writeTomlStringIfNotEmpty(out, "tree", sig.experiment);
                writeTomlStringIfNotEmpty(out, "server", sig.serverIp);
                writeTomlStringIfNotEmpty(out, "y", sig.yExpr);
                writeTomlStringIfNotEmpty(out, "x", sig.xExpr);
                if (sig.manualColor) {
                    writeTomlString(out, "color", sig.colorName.isEmpty() ? colorForIndex(s) : sig.colorName);
                    writeTomlBool(out, "manual_color", true);
                }
                if (sig.hidden) {
                    writeTomlBool(out, "hidden", true);
                }
                if (sig.readModeExplicit) {
                    const char* mode = sig.readMode == DataReadMode::Full ? "full"
                                       : sig.readMode == DataReadMode::Medium ? "medium"
                                                                            : "thin";
                    writeTomlString(out, "read_mode", QString::fromLatin1(mode));
                }
                out << '\n';
            }
        }
    }
    out.flush();
    if (out.status() != QTextStream::Ok) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2")
                         .arg(path, file.errorString());
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Cannot replace %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

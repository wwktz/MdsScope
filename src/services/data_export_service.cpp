// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data_export_service.hpp"

#include "core/mds_helpers.hpp"
#include "mds/mds_client.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace {

QString jsonString(const QString& value)
{
    const QByteArray json =
        QJsonDocument(QJsonArray{value})
            .toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

bool pointInExportRange(double x,
                        bool useXRange,
                        double xmin,
                        double xmax)
{
    return !useXRange || (x >= xmin && x <= xmax);
}

}

QString exportFileToken(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) {
        text = QStringLiteral("unknown");
    }
    text.replace('\\', "");
    text.replace('/', "_");
    text.replace(':', "_");
    text.replace('*', "_");
    text.replace('?', "_");
    text.replace('"', "_");
    text.replace('<', "_");
    text.replace('>', "_");
    text.replace('|', "_");
    text.replace(QRegularExpression("\\s+"), "_");
    return text;
}

QString exportFormatExtension(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Tsv:
        return QStringLiteral("tsv");
    case ExportFormat::Json:
        return QStringLiteral("json");
    case ExportFormat::Text:
    default:
        return QStringLiteral("txt");
    }
}

QString exportFormatSettingValue(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Tsv:
        return QStringLiteral("tsv");
    case ExportFormat::Json:
        return QStringLiteral("json");
    case ExportFormat::Text:
    default:
        return QStringLiteral("text");
    }
}

ExportFormat exportFormatFromSetting(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("csv")) {
        return ExportFormat::Csv;
    }
    if (value == QStringLiteral("tsv")) {
        return ExportFormat::Tsv;
    }
    if (value == QStringLiteral("json")) {
        return ExportFormat::Json;
    }
    return ExportFormat::Text;
}

QString uniqueExportPath(const QDir& dir,
                         const QString& baseName,
                         ExportFormat format)
{
    const QString extension = exportFormatExtension(format);
    QString path = dir.filePath(baseName + "." + extension);
    if (!QFileInfo::exists(path)) {
        return path;
    }
    for (int i = 2; i < 10000; ++i) {
        path = dir.filePath(
            QString("%1_%2.%3")
                .arg(baseName)
                .arg(i)
                .arg(extension));
        if (!QFileInfo::exists(path)) {
            return path;
        }
    }
    return dir.filePath(
        baseName + "_"
        + QString::number(QDateTime::currentMSecsSinceEpoch())
        + "." + extension);
}

QString exportRangeFileSuffix(bool useXRange,
                              double xmin,
                              double xmax)
{
    if (!useXRange) {
        return {};
    }
    return QStringLiteral("x_%1_%2")
        .arg(exportFileToken(QString::number(xmin, 'g', 12)),
             exportFileToken(QString::number(xmax, 'g', 12)));
}

bool writeSeriesDataFile(const QString& path,
                         const SignalSeries& series,
                         ExportFormat format,
                         bool useXRange,
                         double xmin,
                         double xmax,
                         QString* error)
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
    const auto commitOutput = [&out, &file, &path, error] {
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
    };
    if (format == ExportFormat::Json) {
        out << "{\n";
        if (!series.error.isEmpty()) {
            out << "  \"error\": "
                << jsonString(series.error) << ",\n";
        }
        out << "  \"points\": [\n";
        bool first = true;
        auto writeJsonPoint = [&out, &first](double x, double y) {
            if (!first) {
                out << ",\n";
            }
            first = false;
            out << "    ["
                << QString::number(x, 'g', 17) << ", "
                << QString::number(y, 'g', 17) << "]";
        };
        if (series.hasUniformData()) {
            for (int i = 0; i < series.uniformY.size(); ++i) {
                const double x =
                    series.uniformStart
                    + static_cast<double>(i)
                          * series.uniformStep;
                if (pointInExportRange(
                        x, useXRange, xmin, xmax)) {
                    writeJsonPoint(x, series.uniformY[i]);
                }
            }
        } else {
            for (const QPointF& point : series.points) {
                if (pointInExportRange(
                        point.x(), useXRange, xmin, xmax)) {
                    writeJsonPoint(point.x(), point.y());
                }
            }
        }
        out << "\n  ]\n}\n";
        return commitOutput();
    }

    if (format == ExportFormat::Csv) {
        out << "x,y\n";
    } else if (format == ExportFormat::Tsv) {
        out << "x\ty\n";
    } else {
        out << "# x y\n";
    }
    if (!series.error.isEmpty()) {
        if (format == ExportFormat::Csv) {
            out << "# error," << series.error << '\n';
        } else if (format == ExportFormat::Tsv) {
            out << "# error\t" << series.error << '\n';
        } else {
            out << "# error: " << series.error << '\n';
        }
    }
    auto writePoint = [&out, format](double x, double y) {
        if (format == ExportFormat::Csv) {
            out << QString::number(x, 'g', 17)
                << ','
                << QString::number(y, 'g', 17)
                << '\n';
        } else if (format == ExportFormat::Tsv) {
            out << QString::number(x, 'g', 17)
                << '\t'
                << QString::number(y, 'g', 17)
                << '\n';
        } else {
            out << QString::number(x, 'g', 17)
                << ' '
                << QString::number(y, 'g', 17)
                << '\n';
        }
    };
    if (series.hasUniformData()) {
        for (int i = 0; i < series.uniformY.size(); ++i) {
            const double x =
                series.uniformStart
                + static_cast<double>(i) * series.uniformStep;
            if (pointInExportRange(
                    x, useXRange, xmin, xmax)) {
                writePoint(x, series.uniformY[i]);
            }
        }
    } else {
        for (const QPointF& point : series.points) {
            if (pointInExportRange(
                    point.x(), useXRange, xmin, xmax)) {
                writePoint(point.x(), point.y());
            }
        }
    }
    return commitOutput();
}

DataExportResult runDataExport(const DataExportRequest& request)
{
    DataExportResult result;
    QDir baseDir(request.baseDirPath);
    if (!baseDir.exists()
        && !QDir().mkpath(request.baseDirPath)) {
        result.errors.push_back(
            "Cannot create " + request.baseDirPath);
    }
    if (result.errors.isEmpty()
        && !baseDir.mkpath("output")) {
        result.errors.push_back(
            "Cannot create " + baseDir.filePath("output"));
    }

    QDir outputDir(baseDir.filePath("output"));
    result.outputPath = outputDir.absolutePath();
    if (!result.errors.isEmpty()) {
        return result;
    }

    const QVector<LoadedSignal> loaded =
        fetchMdsSignals(request.snapshot, request.readMode);
    for (const LoadedSignal& item : loaded) {
        if (item.column < 0 || item.row < 0 || item.signal < 0
            || item.column >= request.snapshot.columns.size()
            || item.row
                   >= request.snapshot.columns[item.column].size()
            || item.signal
                   >= request.snapshot.columns[item.column][item.row]
                          .signalSpecs.size()) {
            continue;
        }
        const PlotSpec& plot =
            request.snapshot.columns[item.column][item.row];
        const SignalSpec& signalSpec =
            plot.signalSpecs[item.signal];
        if (signalSpec.hidden) {
            continue;
        }

        const QString shot =
            exportFileToken(
                item.shot.isEmpty()
                    ? effectiveSignalShot(plot, signalSpec)
                    : item.shot);
        const QString tree =
            exportFileToken(signalSpec.experiment);
        const QString signal =
            exportFileToken(
                normalizedMdsSignal(signalSpec.yExpr));
        const QRectF viewRange =
            request.viewRanges.value(
                {item.column, item.row});
        bool useXRange = false;
        double xmin = qQNaN();
        double xmax = qQNaN();
        if (request.useCustomRange) {
            useXRange = true;
            xmin = request.customXMin;
            xmax = request.customXMax;
        } else if (request.useCurrentView
                   && viewRange.isValid()) {
            useXRange = true;
            xmin = std::min(
                viewRange.left(), viewRange.right());
            xmax = std::max(
                viewRange.left(), viewRange.right());
        }

        QString baseName =
            QString("%1-%2-%3").arg(shot, tree, signal);
        const QString rangeSuffix =
            exportRangeFileSuffix(useXRange, xmin, xmax);
        if (!rangeSuffix.isEmpty()) {
            baseName += "-" + rangeSuffix;
        }
        const QString path =
            uniqueExportPath(
                outputDir, baseName, request.format);
        QString error;
        if (writeSeriesDataFile(path,
                                item.series,
                                request.format,
                                useXRange,
                                xmin,
                                xmax,
                                &error)) {
            ++result.written;
        } else {
            result.errors.push_back(error);
        }
    }
    return result;
}

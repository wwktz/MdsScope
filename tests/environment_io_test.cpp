// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/environment_io.hpp"
#include "core/mds_helpers.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <utility>

namespace {
SignalSpec signal(QString shot = {})
{
    SignalSpec sig;
    sig.shot = std::move(shot);
    sig.yExpr = QStringLiteral("\\SIG");
    sig.xExpr = QStringLiteral("DIM_OF(\\SIG)");
    sig.experiment = QStringLiteral("east");
    sig.serverIp = QStringLiteral("127.0.0.1");
    return sig;
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(contents) == contents.size();
}
}

int main()
{
    PlotSpec plot;
    plot.shot = QStringLiteral("100");
    SignalSpec inherited = signal();
    SignalSpec duplicate = signal(QStringLiteral("100"));
    duplicate.yExpr = QStringLiteral("\\\\SIG");
    duplicate.readMode = DataReadMode::Full;
    duplicate.manualColor = true;
    duplicate.colorName = QStringLiteral("#ff0000");
    SignalSpec differentShot = signal(QStringLiteral("101"));
    SignalSpec differentTree = signal();
    differentTree.experiment = QStringLiteral("other");
    plot.signalSpecs = {
        inherited,
        duplicate,
        differentShot,
        differentTree,
    };

    deduplicatePlotSignals(&plot);
    if (plot.signalSpecs.size() != 3
        || !plot.signalSpecs[0].shot.isEmpty()
        || plot.signalSpecs[1].shot != QStringLiteral("101")
        || plot.signalSpecs[2].experiment != QStringLiteral("other")) {
        return 1;
    }

    PlotSpec overlapping;
    overlapping.shot = QStringLiteral("100;101");
    overlapping.signalSpecs = {
        signal(),
        signal(QStringLiteral("100")),
    };
    LayoutConfig overlapConfig;
    overlapConfig.columns = {{overlapping}};
    const LayoutConfig expanded = expandedShotLayout(overlapConfig);
    if (expanded.columns[0][0].signalSpecs.size() != 2
        || effectiveSignalShot(expanded.columns[0][0],
                               expanded.columns[0][0].signalSpecs[0])
               != QStringLiteral("100")
        || effectiveSignalShot(expanded.columns[0][0],
                               expanded.columns[0][0].signalSpecs[1])
               != QStringLiteral("101")) {
        return 2;
    }

    QTemporaryDir temp;
    if (!temp.isValid()) {
        return 3;
    }

    PlotSpec duplicatedForSave;
    duplicatedForSave.shot = QStringLiteral("100");
    duplicatedForSave.signalSpecs = {
        signal(),
        signal(QStringLiteral("100")),
        signal(QStringLiteral("101")),
    };
    LayoutConfig saveConfig;
    saveConfig.columns = {{duplicatedForSave}};
    QString error;
    const QString tomlPath = temp.filePath(QStringLiteral("deduplicated.toml"));
    if (!writeEnvironmentToml(saveConfig, tomlPath, &error)) {
        return 4;
    }
    QFile toml(tomlPath);
    if (!toml.open(QIODevice::ReadOnly)
        || QString::fromUtf8(toml.readAll()).count(
               QStringLiteral("[[panels.signals]]")) != 2) {
        return 5;
    }

    const QString webscpPath =
        temp.filePath(QStringLiteral("deduplicated.webscp"));
    if (!writeEnvironmentWebscp(
            saveConfig, webscpPath, temp.path(), &error)) {
        return 6;
    }
    QFile webscp(webscpPath);
    if (!webscp.open(QIODevice::ReadOnly)
        || !QString::fromUtf8(webscp.readAll()).contains(
            QStringLiteral("1_1.num_sig:2"))) {
        return 7;
    }

    const QString rawPath = temp.filePath(QStringLiteral("raw.toml"));
    const QByteArray raw =
        "version = 1\n\n"
        "[[panels]]\n"
        "column = 1\n"
        "row = 1\n"
        "shot = \"100\"\n\n"
        "[[panels.signals]]\n"
        "tree = \"east\"\n"
        "server = \"127.0.0.1\"\n"
        "y = \"\\\\SIG\"\n"
        "x = \"DIM_OF(\\\\SIG)\"\n\n"
        "[[panels.signals]]\n"
        "shot = \"100\"\n"
        "tree = \"east\"\n"
        "server = \"127.0.0.1\"\n"
        "y = \"\\\\SIG\"\n"
        "x = \"DIM_OF(\\\\SIG)\"\n\n"
        "[[panels.signals]]\n"
        "shot = \"101\"\n"
        "tree = \"east\"\n"
        "server = \"127.0.0.1\"\n"
        "y = \"\\\\SIG\"\n"
        "x = \"DIM_OF(\\\\SIG)\"\n";
    if (!writeFile(rawPath, raw)) {
        return 8;
    }
    const LayoutConfig parsed = parseEnvironment(rawPath, &error);
    if (!error.isEmpty()
        || parsed.columns.size() != 1
        || parsed.columns[0].size() != 1
        || parsed.columns[0][0].signalSpecs.size() != 3
        || parsed.columns[0][0].signalSpecs[2].shot
               != QStringLiteral("101")) {
        return 9;
    }
    const LayoutConfig displayed = expandedShotLayout(parsed);
    if (displayed.columns[0][0].signalSpecs.size() != 2
        || displayed.columns[0][0].signalSpecs[1].shot
               != QStringLiteral("101")) {
        return 10;
    }
    return 0;
}

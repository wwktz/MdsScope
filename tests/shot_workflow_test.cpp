// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/main_window/shot_workflow.hpp"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool require(bool condition, const char* message)
{
    if (!condition) {
        qCritical().noquote() << message;
    }
    return condition;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    ShotWorkflow workflow({}, &app, [] {});

    const auto background = workflow.beginLatestFetch(false);
    if (!require(background.started, "background latest request did not start")
        || !require(workflow.latestFetchRunning(), "latest request state was not recorded")
        || !require(!workflow.beginLatestFetch(true).started,
                    "a duplicate latest request was started")) {
        return 1;
    }
    const auto latest = workflow.completeLatestFetch(
        background.generation, QStringLiteral("150000"));
    if (!require(latest.current, "current latest result was rejected")
        || !require(latest.shouldApply, "coalesced apply-latest intent was lost")
        || !require(workflow.latestShot() == QStringLiteral("150000"),
                    "latest shot cache was not updated")) {
        return 1;
    }

    const auto obsolete = workflow.beginLatestFetch(false);
    workflow.invalidateLatest();
    if (!require(!workflow.completeLatestFetch(
                      obsolete.generation, QStringLiteral("150001")).current,
                 "obsolete latest result was accepted")) {
        return 1;
    }

    int summaryGeneration = 0;
    if (!require(workflow.beginSummaryFetch(
                     QStringLiteral("150000"), &summaryGeneration),
                 "summary request did not start")
        || !require(!workflow.beginSummaryFetch(
                        QStringLiteral("150000"), &summaryGeneration),
                    "duplicate summary request was started")) {
        return 1;
    }
    ShotSummary summary;
    summary.ip = QStringLiteral("600");
    summary.pulse = QStringLiteral("10");
    if (!require(workflow.completeSummaryFetch(
                     summaryGeneration, QStringLiteral("150000"), true, summary),
                 "current summary result was rejected")
        || !require(workflow.summary().ip == QStringLiteral("600"),
                    "summary cache was not updated")) {
        return 1;
    }

    return 0;
}

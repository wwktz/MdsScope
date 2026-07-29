// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/shot_metadata_client.hpp"

#include <QString>
#include <QTimer>

#include <functional>

class QObject;

struct ShotSummary {
    QString shot;
    QString ip;
    QString pulse;
    QString it;
    QString time;
};

class ShotWorkflow final {
public:
    struct LatestRequest {
        bool started = false;
        int generation = 0;
    };

    struct LatestCompletion {
        bool current = false;
        bool shouldApply = false;
    };

    ShotWorkflow(QString rootPath,
                 QObject* context,
                 std::function<void()> pollLatest);

    QString fetchLatest(const QString& apiUrl) const;
    bool fetchSummary(const QString& shot,
                      ShotSummary* summary,
                      const QString& apiUrl) const;

    LatestRequest beginLatestFetch(bool applyLatest);
    bool failLatestFetchStart();
    LatestCompletion completeLatestFetch(int generation,
                                         const QString& latestShot);
    bool latestFetchRunning() const;
    const QString& latestShot() const;
    void clearLatest();
    void invalidateLatest();

    bool beginSummaryFetch(const QString& shot, int* generation);
    void failSummaryFetchStart(const QString& shot, int generation);
    bool completeSummaryFetch(int generation,
                              const QString& shot,
                              bool ok,
                              const ShotSummary& summary);
    void clearSummary();
    void invalidateSummary();
    const ShotSummary& summary() const;
    const QString& pendingSummaryShot() const;

private:
    ShotMetadataClient metadata_;
    QTimer pollTimer_;
    QString latestShot_;
    bool latestFetchRunning_ = false;
    bool latestApplyPending_ = false;
    int latestGeneration_ = 0;
    ShotSummary summary_;
    QString pendingSummaryShot_;
    int summaryGeneration_ = 0;
};

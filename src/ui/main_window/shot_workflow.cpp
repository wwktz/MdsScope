// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shot_workflow.hpp"

#include <QObject>

#include <utility>

ShotWorkflow::ShotWorkflow(QString rootPath,
                           QObject* context,
                           std::function<void()> pollLatest)
    : metadata_(std::move(rootPath))
{
    pollTimer_.setInterval(20'000);
    QObject::connect(&pollTimer_,
                     &QTimer::timeout,
                     context,
                     std::move(pollLatest));
    pollTimer_.start();
}

QString ShotWorkflow::fetchLatest(const QString& apiUrl) const
{
    return metadata_.latestShot(apiUrl);
}

bool ShotWorkflow::fetchSummary(const QString& shot,
                                ShotSummary* summary,
                                const QString& apiUrl) const
{
    if (!summary) {
        return false;
    }
    summary->shot = shot;
    return metadata_.loadSummary(shot,
                                 &summary->ip,
                                 &summary->pulse,
                                 &summary->it,
                                 &summary->time,
                                 apiUrl);
}

ShotWorkflow::LatestRequest ShotWorkflow::beginLatestFetch(bool applyLatest)
{
    latestApplyPending_ = latestApplyPending_ || applyLatest;
    if (latestFetchRunning_) {
        return {};
    }
    latestFetchRunning_ = true;
    return {true, ++latestGeneration_};
}

bool ShotWorkflow::failLatestFetchStart()
{
    latestFetchRunning_ = false;
    const bool shouldApply = latestApplyPending_;
    latestApplyPending_ = false;
    return shouldApply;
}

ShotWorkflow::LatestCompletion ShotWorkflow::completeLatestFetch(
    int generation,
    const QString& latestShot)
{
    if (generation != latestGeneration_) {
        latestFetchRunning_ = false;
        latestApplyPending_ = false;
        return {};
    }
    latestFetchRunning_ = false;
    const bool shouldApply = latestApplyPending_;
    latestApplyPending_ = false;
    if (!latestShot.isEmpty()) {
        latestShot_ = latestShot;
    }
    return {true, shouldApply};
}

bool ShotWorkflow::latestFetchRunning() const
{
    return latestFetchRunning_;
}

const QString& ShotWorkflow::latestShot() const
{
    return latestShot_;
}

void ShotWorkflow::clearLatest()
{
    latestShot_.clear();
}

void ShotWorkflow::invalidateLatest()
{
    ++latestGeneration_;
}

bool ShotWorkflow::beginSummaryFetch(const QString& shot, int* generation)
{
    const QString trimmedShot = shot.trimmed();
    if (trimmedShot.isEmpty() || pendingSummaryShot_ == trimmedShot) {
        return false;
    }
    if (trimmedShot != summary_.shot) {
        summary_.ip.clear();
        summary_.pulse.clear();
        summary_.it.clear();
        summary_.time.clear();
    }
    pendingSummaryShot_ = trimmedShot;
    *generation = ++summaryGeneration_;
    return true;
}

void ShotWorkflow::failSummaryFetchStart(const QString& shot, int generation)
{
    if (generation == summaryGeneration_ && pendingSummaryShot_ == shot) {
        pendingSummaryShot_.clear();
    }
}

bool ShotWorkflow::completeSummaryFetch(int generation,
                                        const QString& shot,
                                        bool ok,
                                        const ShotSummary& summary)
{
    if (generation != summaryGeneration_ || pendingSummaryShot_ != shot) {
        return false;
    }
    pendingSummaryShot_.clear();
    summary_ = summary;
    summary_.shot = shot;
    if (!ok) {
        summary_.ip.clear();
        summary_.pulse.clear();
        summary_.it.clear();
        summary_.time.clear();
    }
    return true;
}

void ShotWorkflow::clearSummary()
{
    summary_ = {};
    pendingSummaryShot_.clear();
}

void ShotWorkflow::invalidateSummary()
{
    ++summaryGeneration_;
}

const ShotSummary& ShotWorkflow::summary() const
{
    return summary_;
}

const QString& ShotWorkflow::pendingSummaryShot() const
{
    return pendingSummaryShot_;
}

// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class ShotMetadataClient final {
public:
    explicit ShotMetadataClient(QString rootPath);

    QString latestShot(const QString& apiOverride = {}) const;
    bool loadSummary(const QString& shot,
                     QString* ip,
                     QString* pulse,
                     QString* it,
                     QString* time,
                     const QString& apiOverride = {}) const;

private:
    QString rootPath_;
};

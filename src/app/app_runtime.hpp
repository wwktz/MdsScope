// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

int runMdsScopeBenchmark(const QString& configPath,
                         DataReadMode readMode = DataReadMode::Thin,
                         const QString& shotOverride = {},
                         bool summaryOnly = false,
                         bool prewarm = false);
void shutdownMdsScopeWorkers();
int runMdsScopeApplication(int argc, char* argv[]);

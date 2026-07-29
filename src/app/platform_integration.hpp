// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QWidget;

void preparePlatformProcess();
void installPlatformApplicationIntegration();
void applyPlatformWindowIntegration(QWidget* window);

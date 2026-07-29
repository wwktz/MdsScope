// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/app_types.hpp"

#include <QIcon>
QIcon appIcon();
QIcon layoutIcon();
QIcon appearanceIcon();
QIcon openFileIcon();
QIcon saveIcon();
QIcon exportDataIcon();
QIcon refreshIcon();
QIcon loginIcon(bool loggedIn);
QIcon sshIcon(int state);
QIcon browserIcon();
QIcon modeIcon(InteractionMode mode, bool active);

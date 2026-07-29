// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

struct FontSettings {
    QString family = QStringLiteral("Times New Roman");
    int iconSize = 24;
#ifdef Q_OS_WIN
    int legendSize = 10;
    int axisSize = 10;
    int unitSize = 10;
    int uiSize = 10;
#else
    int legendSize = 14;
    int axisSize = 14;
    int unitSize = 14;
    int uiSize = 14;
#endif
};

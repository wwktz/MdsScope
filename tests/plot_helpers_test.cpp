// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/plot/helpers.hpp"

#include <cmath>

namespace {
bool near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 1e-9;
}
}

int main()
{
    double min = 0.0;
    double max = 10.0;
    fillMissingAxisBounds(&min, &max, 0.0, 1.0);
    if (!near(min, 0.0) || !near(max, 10.0)) {
        return 1;
    }

    min = std::numeric_limits<double>::infinity();
    max = -std::numeric_limits<double>::infinity();
    fillMissingAxisBounds(&min, &max, -1.0, 1.0);
    if (!near(min, -1.0) || !near(max, 1.0)) {
        return 2;
    }

    const QVector<QPointF> descending {
        QPointF(10.0, 2.0),
        QPointF(5.0, 20.0),
        QPointF(0.0, 4.0),
    };
    const QPair<int, int> range = sortedPointIndexRange(descending, 4.0, 6.0);
    if (range.first != 1 || range.second != 1) {
        return 3;
    }
    const QPair<int, int> outside = sortedPointIndexRange(descending, 20.0, 30.0);
    return outside.first == -1 && outside.second == -1 ? 0 : 4;
}

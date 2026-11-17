#pragma once
#include <QString>
#include <cmath>
#include <limits>

namespace smoothmail {
// Counts are actual transfer/reuse work, not the server's candidate count.
inline QString estimatedRemainingTime(qint64 completed, qint64 total, qint64 elapsedMs)
{
    if (total < 0 || completed < 0 || completed > total) return {};
    if (completed == total) return QStringLiteral("0秒");
    if (completed == 0 || elapsedMs <= 0) return {};
    const long double units = std::ceil(static_cast<long double>(elapsedMs)
        * (total - completed) / completed / 10000.0L);
    constexpr qint64 maximumUnits = std::numeric_limits<qint64>::max() / 10;
    const qint64 seconds = qint64(qMin(units, static_cast<long double>(maximumUnits))) * 10;
    if (seconds >= 3600) return QStringLiteral("%1時間%2分%3秒").arg(seconds / 3600).arg(seconds / 60 % 60).arg(seconds % 60);
    if (seconds >= 60) return QStringLiteral("%1分%2秒").arg(seconds / 60).arg(seconds % 60);
    return QStringLiteral("%1秒").arg(seconds);
}
}

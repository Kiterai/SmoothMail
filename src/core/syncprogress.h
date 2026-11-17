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
    const long double remaining = static_cast<long double>(elapsedMs)
        * (total - completed) / completed / 1000.0L;
    // Select the format from the unrounded estimate, then round within that range.
    const qint64 quantum = remaining < 600 ? 10 : remaining < 3600 ? 60 : remaining < 36000 ? 600 : 3600;
    const long double units = std::ceil(remaining / quantum);
    const qint64 maximumUnits = std::numeric_limits<qint64>::max() / quantum;
    const qint64 seconds = qint64(qMin(units, static_cast<long double>(maximumUnits))) * quantum;
    if (remaining < 60) return QStringLiteral("%1秒").arg(seconds);
    if (remaining < 600) return QStringLiteral("%1分%2秒").arg(seconds / 60).arg(seconds % 60);
    if (remaining < 3600) return QStringLiteral("%1分").arg(seconds / 60);
    if (remaining < 36000) return QStringLiteral("%1時間%2分").arg(seconds / 3600).arg(seconds / 60 % 60);
    return QStringLiteral("%1時間").arg(seconds / 3600);
}
}

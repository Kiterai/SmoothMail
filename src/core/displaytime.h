#pragma once
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

namespace smoothmail {
inline QString displayTime(const QDateTime &date, bool detailed = false)
{
    if (!date.isValid()) return {};
    const auto local = date.toTimeZone(QTimeZone::systemTimeZone());
    auto zone = local.timeZoneAbbreviation();
    if (zone.isEmpty()) zone = local.toString(QStringLiteral("ttt"));
    const auto pattern = detailed ? QStringLiteral("yyyy/MM/dd (ddd) HH:mm:ss") : QStringLiteral("yyyy/MM/dd HH:mm");
    return QLocale(QLocale::Japanese).toString(local, pattern) + QStringLiteral(" (%1)").arg(zone);
}
inline QString displayUpdateTime(const QDateTime &date)
{
    if (!date.isValid()) return {};
    const auto local = date.toTimeZone(QTimeZone::systemTimeZone());
    auto zone = local.timeZoneAbbreviation();
    if (zone.isEmpty()) zone = local.toString(QStringLiteral("ttt"));
    return local.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")) + QStringLiteral(" (%1)").arg(zone);
}
}

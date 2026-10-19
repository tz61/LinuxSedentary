#include "format.h"

#include <cstdlib>

namespace Format {

namespace {

QString hoursMinutes(int minutes)
{
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(minutes);
    const int m = minutes % 60;
    if (m == 0)
        return QStringLiteral("%1 h").arg(minutes / 60);
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(m, 2, 10, QLatin1Char('0'));
}

} // namespace

QString duration(int seconds)
{
    seconds = std::abs(seconds);
    if (seconds < 60)
        return QStringLiteral("less than a minute");
    return hoursMinutes(seconds / 60);
}

QString minutesCeil(int seconds)
{
    seconds = std::abs(seconds);
    return hoursMinutes((seconds + 59) / 60);
}

QString clock(int seconds)
{
    seconds = std::abs(seconds);
    const int h = seconds / 3600;
    const int m = (seconds / 60) % 60;
    const int s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace Format

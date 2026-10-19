#pragma once

#include <QString>

namespace Format {

// "1 h 05 min", "12 min", "less than a minute"
QString duration(int seconds);
// Rounded up to whole minutes: "13 min", "1 h 20 min"
QString minutesCeil(int seconds);
// "4:59", "1:02:03"
QString clock(int seconds);

} // namespace Format

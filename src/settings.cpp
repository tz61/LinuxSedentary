#include "settings.h"

#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

int readMinutes(const QSettings &s, const char *key, int fallback, int min, int max)
{
    bool ok = false;
    const int value = s.value(QLatin1String(key), fallback).toInt(&ok);
    return ok ? std::clamp(value, min, max) : fallback;
}

} // namespace

QString Settings::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/" APP_ID "/settings.ini");
}

Settings Settings::load(const QString &path)
{
    const QSettings s(path, QSettings::IniFormat);
    Settings r;
    r.workMinutes = readMinutes(s, "timer/work_minutes", r.workMinutes, 1, 600);
    r.breakMinutes = readMinutes(s, "timer/break_minutes", r.breakMinutes, 1, 120);
    r.snoozeMinutes = readMinutes(s, "timer/snooze_minutes", r.snoozeMinutes, 1, 120);
    r.awayMinutes = readMinutes(s, "timer/away_minutes", r.awayMinutes, 1, 120);
    r.nagMinutes = readMinutes(s, "timer/nag_minutes", r.nagMinutes, 0, 120);
    r.preWarn = s.value(QStringLiteral("timer/pre_warn"), r.preWarn).toBool();
    r.respectInhibit = s.value(QStringLiteral("timer/respect_inhibit"), r.respectInhibit).toBool();
    r.style = styleFromKey(s.value(QStringLiteral("reminder/style")).toString());
    r.sound = s.value(QStringLiteral("reminder/sound"), r.sound).toBool();
    r.strict = s.value(QStringLiteral("reminder/strict"), r.strict).toBool();
    return r;
}

bool Settings::save(const QString &path) const
{
    QSettings s(path, QSettings::IniFormat);
    s.setValue(QStringLiteral("timer/work_minutes"), workMinutes);
    s.setValue(QStringLiteral("timer/break_minutes"), breakMinutes);
    s.setValue(QStringLiteral("timer/snooze_minutes"), snoozeMinutes);
    s.setValue(QStringLiteral("timer/away_minutes"), awayMinutes);
    s.setValue(QStringLiteral("timer/nag_minutes"), nagMinutes);
    s.setValue(QStringLiteral("timer/pre_warn"), preWarn);
    s.setValue(QStringLiteral("timer/respect_inhibit"), respectInhibit);
    s.setValue(QStringLiteral("reminder/style"), styleKey(style));
    s.setValue(QStringLiteral("reminder/sound"), sound);
    s.setValue(QStringLiteral("reminder/strict"), strict);
    s.sync();
    return s.status() == QSettings::NoError;
}

SchedulerConfig Settings::schedulerConfig() const
{
    const int unit = minuteSeconds();
    SchedulerConfig c;
    c.workSeconds = workMinutes * unit;
    c.breakSeconds = breakMinutes * unit;
    c.snoozeSeconds = snoozeMinutes * unit;
    c.awaySeconds = awayMinutes * unit;
    c.nagSeconds = nagMinutes * unit;
    c.preWarnSeconds = preWarn ? unit : 0;
    return c;
}

QString Settings::styleKey(Style style)
{
    switch (style) {
    case Style::Notification:
        return QStringLiteral("notification");
    case Style::Fullscreen:
        return QStringLiteral("fullscreen");
    case Style::Window:
        break;
    }
    return QStringLiteral("window");
}

Settings::Style Settings::styleFromKey(const QString &key)
{
    if (key == QLatin1String("notification"))
        return Style::Notification;
    if (key == QLatin1String("fullscreen"))
        return Style::Fullscreen;
    return Style::Window;
}

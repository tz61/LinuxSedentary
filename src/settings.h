#pragma once

#include "scheduler.h"

#include <QString>

struct Settings {
    enum class Style { Notification, Window, Fullscreen };

    int workMinutes = 45;
    int breakMinutes = 5;
    int snoozeMinutes = 5;
    int awayMinutes = 5;
    int nagMinutes = 5;
    bool preWarn = true;         // one minute ahead
    Style style = Style::Window;
    bool sound = true;
    bool strict = false;         // no Skip, a single Snooze per cycle
    bool respectInhibit = false; // idle time does not count while e.g. a video plays

    // Dev/demo mode: every "minute" lasts one second.
    bool fastMode = false;

    // ~/.config/sedentary-reminder/settings.ini
    static QString defaultPath();
    static Settings load(const QString &path = defaultPath());
    bool save(const QString &path = defaultPath()) const;

    SchedulerConfig schedulerConfig() const;
    int minuteSeconds() const { return fastMode ? 1 : 60; }
    // Scheduler seconds as the user perceives them (fast mode: seconds are minutes).
    int displaySeconds(int seconds) const { return seconds * 60 / minuteSeconds(); }

    static QString styleKey(Style style);
    static Style styleFromKey(const QString &key);
};

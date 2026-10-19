#pragma once

#include <QDate>
#include <QMap>
#include <QString>

struct DayStats {
    qint64 sittingMs = 0;
    int breaks = 0;          // reminder breaks taken, plus "I just took a break"
    int naturalBreaks = 0;   // the user stood up without being asked
    int skipped = 0;
    int snoozed = 0;
    int longestStretch = 0;  // longest continuous sitting, seconds

    int sittingSeconds() const { return int(sittingMs / 1000); }
    int totalBreaks() const { return breaks + naturalBreaks; }
    bool operator==(const DayStats &other) const;
};

// Per-day statistics, persisted as JSON.
class History
{
public:
    explicit History(QString path = QString());

    // Default location: ~/.local/share/sedentary-reminder/history.json
    static QString defaultPath();

    bool load();
    bool save();
    bool isDirty() const { return m_dirty; }
    const QString &path() const { return m_path; }

    DayStats day(const QDate &date) const { return m_days.value(date); }
    const QMap<QDate, DayStats> &days() const { return m_days; }

    void addSitting(const QDate &date, qint64 ms);
    void addBreak(const QDate &date);
    void addNaturalBreak(const QDate &date);
    void addSkip(const QDate &date);
    void addSnooze(const QDate &date);
    void recordStretch(const QDate &date, int seconds);

    // Drops days older than keepDays.
    void prune(const QDate &today, int keepDays = 400);

private:
    DayStats &at(const QDate &date);

    QString m_path;
    QMap<QDate, DayStats> m_days;
    bool m_dirty = false;
};

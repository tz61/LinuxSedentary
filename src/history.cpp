#include "history.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int kFormatVersion = 1;
}

bool DayStats::operator==(const DayStats &other) const
{
    return sittingMs / 1000 == other.sittingMs / 1000 && breaks == other.breaks
        && naturalBreaks == other.naturalBreaks && skipped == other.skipped
        && snoozed == other.snoozed && longestStretch == other.longestStretch;
}

History::History(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path))
{
}

QString History::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/" APP_ID "/history.json");
}

bool History::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return !file.exists();

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject days = root.value(QLatin1String("days")).toObject();
    m_days.clear();
    for (auto it = days.begin(); it != days.end(); ++it) {
        const QDate date = QDate::fromString(it.key(), Qt::ISODate);
        if (!date.isValid())
            continue;
        const QJsonObject o = it.value().toObject();
        DayStats d;
        d.sittingMs = qRound64(o.value(QLatin1String("sitting")).toDouble() * 1000);
        d.breaks = o.value(QLatin1String("breaks")).toInt();
        d.naturalBreaks = o.value(QLatin1String("natural")).toInt();
        d.skipped = o.value(QLatin1String("skipped")).toInt();
        d.snoozed = o.value(QLatin1String("snoozed")).toInt();
        d.longestStretch = o.value(QLatin1String("longest")).toInt();
        m_days.insert(date, d);
    }
    m_dirty = false;
    return true;
}

bool History::save()
{
    QJsonObject days;
    for (auto it = m_days.cbegin(); it != m_days.cend(); ++it) {
        const DayStats &d = it.value();
        QJsonObject o;
        // Seconds with millisecond precision, so frequent saves lose nothing.
        o.insert(QLatin1String("sitting"), double(d.sittingMs) / 1000.0);
        o.insert(QLatin1String("breaks"), d.breaks);
        o.insert(QLatin1String("natural"), d.naturalBreaks);
        o.insert(QLatin1String("skipped"), d.skipped);
        o.insert(QLatin1String("snoozed"), d.snoozed);
        o.insert(QLatin1String("longest"), d.longestStretch);
        days.insert(it.key().toString(Qt::ISODate), o);
    }
    QJsonObject root;
    root.insert(QLatin1String("version"), kFormatVersion);
    root.insert(QLatin1String("days"), days);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;
    m_dirty = false;
    return true;
}

DayStats &History::at(const QDate &date)
{
    m_dirty = true;
    return m_days[date];
}

void History::addSitting(const QDate &date, qint64 ms)
{
    DayStats &d = at(date);
    d.sittingMs = std::max<qint64>(0, d.sittingMs + ms);
}

void History::addBreak(const QDate &date)
{
    ++at(date).breaks;
}

void History::addNaturalBreak(const QDate &date)
{
    ++at(date).naturalBreaks;
}

void History::addSkip(const QDate &date)
{
    ++at(date).skipped;
}

void History::addSnooze(const QDate &date)
{
    ++at(date).snoozed;
}

void History::recordStretch(const QDate &date, int seconds)
{
    if (seconds > m_days.value(date).longestStretch)
        at(date).longestStretch = seconds;
}

void History::prune(const QDate &today, int keepDays)
{
    const QDate oldest = today.addDays(-keepDays);
    for (auto it = m_days.begin(); it != m_days.end();) {
        if (it.key() < oldest) {
            it = m_days.erase(it);
            m_dirty = true;
        } else {
            ++it;
        }
    }
}

#include "history.h"
#include "settings.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class TestHistory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/history.json"));
        const QDate day(2026, 10, 18);

        History h(path);
        QVERIFY(h.load()); // a missing file is not an error
        h.addSitting(day, 1500);
        h.addSitting(day, 1500);
        h.addBreak(day);
        h.addNaturalBreak(day);
        h.addSkip(day);
        h.addSnooze(day);
        h.addSnooze(day);
        h.recordStretch(day, 1200);
        h.recordStretch(day, 600);
        h.addBreak(day.addDays(-1));
        QVERIFY(h.isDirty());
        QVERIFY(h.save());
        QVERIFY(!h.isDirty());

        History loaded(path);
        QVERIFY(loaded.load());
        const DayStats d = loaded.day(day);
        QCOMPARE(d.sittingMs, qint64(3000));
        QCOMPARE(d.breaks, 1);
        QCOMPARE(d.naturalBreaks, 1);
        QCOMPARE(d.totalBreaks(), 2);
        QCOMPARE(d.skipped, 1);
        QCOMPARE(d.snoozed, 2);
        QCOMPARE(d.longestStretch, 1200);
        QCOMPARE(loaded.day(day.addDays(-1)).breaks, 1);
        QCOMPARE(loaded.days().size(), 2);
    }

    void frequentSavesKeepMilliseconds()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("history.json"));
        const QDate day(2026, 10, 18);
        for (int i = 0; i < 10; ++i) {
            History h(path);
            QVERIFY(h.load());
            h.addSitting(day, 1700);
            QVERIFY(h.save());
        }
        History h(path);
        QVERIFY(h.load());
        QCOMPARE(h.day(day).sittingMs, qint64(17000));
    }

    void sittingNeverGoesNegative()
    {
        History h(QStringLiteral("/nonexistent/history.json"));
        const QDate day(2026, 1, 1);
        h.addSitting(day, 1000);
        h.addSitting(day, -5000);
        QCOMPARE(h.day(day).sittingMs, qint64(0));
    }

    void pruneDropsOldDays()
    {
        History h(QStringLiteral("/nonexistent/history.json"));
        const QDate today(2026, 10, 18);
        h.addBreak(today);
        h.addBreak(today.addDays(-30));
        h.addBreak(today.addDays(-31));
        h.prune(today, 30);
        QCOMPARE(h.days().size(), 2);
        QVERIFY(!h.days().contains(today.addDays(-31)));
    }

    void corruptFileLoadsEmpty()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("history.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        History h(path);
        QVERIFY(h.load());
        QVERIFY(h.days().isEmpty());
    }

    void settingsRoundTripAndClamp()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("settings.ini"));
        Settings s;
        s.workMinutes = 50;
        s.breakMinutes = 7;
        s.style = Settings::Style::Fullscreen;
        s.strict = true;
        s.sound = false;
        QVERIFY(s.save(path));

        const Settings loaded = Settings::load(path);
        QCOMPARE(loaded.workMinutes, 50);
        QCOMPARE(loaded.breakMinutes, 7);
        QCOMPARE(loaded.style, Settings::Style::Fullscreen);
        QCOMPARE(loaded.strict, true);
        QCOMPARE(loaded.sound, false);

        const SchedulerConfig c = loaded.schedulerConfig();
        QCOMPARE(c.workSeconds, 50 * 60);
        QCOMPARE(c.preWarnSeconds, 60);

        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("timer/work_minutes"), 0);
        raw.sync();
        QCOMPARE(Settings::load(path).workMinutes, 1);
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "test_history.moc"

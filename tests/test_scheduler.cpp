#include "scheduler.h"

#include <QSignalSpy>
#include <QTest>

// Drives a Scheduler one simulated second at a time.
class Sim
{
public:
    explicit Sim(const SchedulerConfig &config)
    {
        s.setConfig(config);
        s.tick(now, 0, false);
    }

    // The user is at the keyboard for `seconds`.
    void active(int seconds)
    {
        for (int i = 0; i < seconds; ++i) {
            idle = 0;
            step();
        }
    }

    // No input for `seconds`; idle time keeps growing across calls.
    void inactive(int seconds, bool inhibited = false)
    {
        for (int i = 0; i < seconds; ++i) {
            ++idle;
            step(inhibited);
        }
    }

    void lockedFor(int seconds)
    {
        for (int i = 0; i < seconds; ++i) {
            now += 1000;
            s.tick(now, 0, true);
        }
    }

    void jump(int seconds)
    {
        now += qint64(seconds) * 1000;
        s.tick(now, 0, false);
    }

    Scheduler s;
    qint64 now = 1'700'000'000'000;
    int idle = 0;

private:
    void step(bool inhibited = false)
    {
        now += 1000;
        s.tick(now, idle, false, inhibited);
    }
};

class TestScheduler : public QObject
{
    Q_OBJECT

private:
    static SchedulerConfig shortConfig()
    {
        SchedulerConfig c;
        c.workSeconds = 10;
        c.breakSeconds = 3;
        c.snoozeSeconds = 4;
        c.preWarnSeconds = 2;
        c.awaySeconds = 5;
        c.nagSeconds = 6;
        return c;
    }

    static SchedulerConfig realisticConfig()
    {
        SchedulerConfig c;
        c.workSeconds = 600;
        c.breakSeconds = 60;
        c.snoozeSeconds = 120;
        c.preWarnSeconds = 0;
        c.awaySeconds = 60;
        c.nagSeconds = 300;
        return c;
    }

private Q_SLOTS:
    void breakBecomesDueAfterWorkInterval()
    {
        Sim sim(shortConfig());
        QSignalSpy warn(&sim.s, &Scheduler::preWarning);
        QSignalSpy due(&sim.s, &Scheduler::breakDue);

        sim.active(7);
        QCOMPARE(warn.count(), 0);
        QCOMPARE(sim.s.secondsUntilBreak(), 3);

        sim.active(1);
        QCOMPARE(warn.count(), 1);
        QCOMPARE(warn.at(0).at(0).toInt(), 2);
        QCOMPARE(due.count(), 0);

        sim.active(2);
        QCOMPARE(due.count(), 1);
        QCOMPARE(due.at(0).at(0).toInt(), 10);
        QCOMPARE(due.at(0).at(1).toBool(), false);
        QCOMPARE(sim.s.state(), Scheduler::State::Due);
        QCOMPARE(sim.s.cycleProgress(), 1.0);
    }

    void overdueBreakIsRepeated()
    {
        Sim sim(shortConfig());
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        sim.active(10);
        sim.active(5);
        QCOMPARE(due.count(), 1);
        sim.active(1);
        QCOMPARE(due.count(), 2);
        QCOMPARE(due.at(1).at(1).toBool(), true);
        QCOMPARE(sim.s.secondsUntilBreak(), -6);
    }

    void snoozePostponesTheBreak()
    {
        Sim sim(shortConfig());
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        QSignalSpy snoozed(&sim.s, &Scheduler::snoozed);
        sim.active(10);
        sim.s.snooze();
        QCOMPARE(snoozed.count(), 1);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.snoozeCount(), 1);
        sim.active(3);
        QCOMPARE(due.count(), 1);
        sim.active(1);
        QCOMPARE(due.count(), 2);
        QCOMPARE(due.at(1).at(0).toInt(), 14);
    }

    void snoozeDuringPreWarningMovesDeadline()
    {
        Sim sim(shortConfig());
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        sim.active(8);
        sim.s.snooze();
        sim.active(5);
        QCOMPARE(due.count(), 0);
        sim.active(1);
        QCOMPARE(due.count(), 1);
    }

    void skipRestartsTheCycleButNotTheStretch()
    {
        Sim sim(shortConfig());
        QSignalSpy skipped(&sim.s, &Scheduler::breakSkipped);
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        sim.active(10);
        sim.s.skip();
        QCOMPARE(skipped.count(), 1);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 10);
        QCOMPARE(sim.s.secondsUntilBreak(), 10);
        sim.active(10);
        QCOMPARE(due.count(), 2);
        QCOMPARE(due.at(1).at(0).toInt(), 20);
    }

    void breakCountsDownAndStartsANewCycle()
    {
        Sim sim(shortConfig());
        QSignalSpy started(&sim.s, &Scheduler::breakStarted);
        QSignalSpy finished(&sim.s, &Scheduler::breakFinished);
        QSignalSpy stretch(&sim.s, &Scheduler::stretchEnded);
        sim.active(10);
        sim.s.startBreak();
        QCOMPARE(started.count(), 1);
        QCOMPARE(stretch.count(), 1);
        QCOMPARE(stretch.at(0).at(0).toInt(), 10);
        QCOMPARE(sim.s.state(), Scheduler::State::OnBreak);
        QCOMPARE(sim.s.breakRemainingSeconds(), 3);

        // Typing during a break neither counts as sitting nor ends it.
        sim.active(2);
        QCOMPARE(sim.s.breakRemainingSeconds(), 1);
        QCOMPARE(finished.count(), 0);
        sim.inactive(1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 0);
        QCOMPARE(sim.s.secondsUntilBreak(), 10);
    }

    void breakCanEndEarly()
    {
        Sim sim(shortConfig());
        QSignalSpy finished(&sim.s, &Scheduler::breakFinished);
        sim.active(4);
        sim.s.startBreak();
        sim.active(1);
        sim.s.endBreak();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(sim.s.sittingSeconds(), 0);
    }

    void mouseDuringBreakVoidsIt()
    {
        Sim sim(realisticConfig());
        QSignalSpy finished(&sim.s, &Scheduler::breakFinished);
        QSignalSpy voided(&sim.s, &Scheduler::breakVoided);
        sim.active(300);
        sim.s.startBreak();
        sim.active(30);
        sim.s.voidBreak();
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(voided.count(), 1);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(sim.s.sittingSeconds(), 300);

        // Not on a break any more: nothing happens.
        sim.s.voidBreak();
        QCOMPARE(voided.count(), 1);
    }

    void longIdleCountsAsNaturalBreak()
    {
        Sim sim(realisticConfig());
        QSignalSpy natural(&sim.s, &Scheduler::naturalBreak);
        QSignalSpy counted(&sim.s, &Scheduler::sittingCounted);
        QSignalSpy stretch(&sim.s, &Scheduler::stretchEnded);

        sim.active(300);
        sim.inactive(59);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 359);

        sim.inactive(1);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        QCOMPARE(sim.s.sittingSeconds(), 0);
        QCOMPARE(stretch.count(), 1);
        QCOMPARE(stretch.at(0).at(0).toInt(), 300);

        // The idle minute was taken back from the sitting total.
        qint64 total = 0;
        for (const auto &args : counted)
            total += args.at(0).toLongLong();
        QCOMPARE(total, qint64(300'000));

        sim.inactive(120);
        QCOMPARE(natural.count(), 0);
        sim.active(1);
        QCOMPARE(natural.count(), 1);
        QCOMPARE(natural.at(0).at(0).toInt(), 181); // last input to first input
        QCOMPARE(natural.at(0).at(1).toBool(), false);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.secondsUntilBreak(), 600);
    }

    void shortIdleIsStillSitting()
    {
        Sim sim(realisticConfig());
        sim.active(100);
        sim.inactive(45);
        sim.active(5);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 150);
    }

    void briefSittingBeforeLeavingIsNotABreak()
    {
        Sim sim(realisticConfig());
        QSignalSpy natural(&sim.s, &Scheduler::naturalBreak);
        sim.active(20);
        sim.inactive(90);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        sim.active(1);
        QCOMPARE(natural.count(), 0);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
    }

    void dueBreakIsSatisfiedByBreakLengthOfIdle()
    {
        SchedulerConfig c = realisticConfig();
        c.awaySeconds = 300;
        Sim sim(c);
        QSignalSpy natural(&sim.s, &Scheduler::naturalBreak);
        sim.active(600);
        QCOMPARE(sim.s.state(), Scheduler::State::Due);
        sim.inactive(60);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        sim.active(1);
        QCOMPARE(natural.count(), 1);
        QCOMPARE(natural.at(0).at(1).toBool(), true);
    }

    void lockedScreenCountsAsAway()
    {
        Sim sim(realisticConfig());
        QSignalSpy natural(&sim.s, &Scheduler::naturalBreak);
        sim.active(300);
        sim.lockedFor(59);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        sim.lockedFor(1);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        sim.active(1);
        QCOMPARE(natural.count(), 1);
    }

    void suspendCountsAsAway()
    {
        Sim sim(realisticConfig());
        QSignalSpy natural(&sim.s, &Scheduler::naturalBreak);
        QSignalSpy counted(&sim.s, &Scheduler::sittingCounted);
        sim.active(300);
        sim.jump(3600);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        sim.active(1);
        QCOMPARE(natural.count(), 1);
        QVERIFY(natural.at(0).at(0).toInt() >= 3600);
        qint64 total = 0;
        for (const auto &args : counted)
            total += args.at(0).toLongLong();
        QCOMPARE(total, qint64(300'000));
    }

    void shortStallIsCountedAsSitting()
    {
        Sim sim(realisticConfig());
        sim.active(10);
        sim.jump(30);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 40);
    }

    void inhibitedIdleIsNotABreak()
    {
        Sim sim(realisticConfig());
        sim.active(100);
        sim.inactive(200, true);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 300);
    }

    void pauseSuppressesRemindersButKeepsCounting()
    {
        Sim sim(shortConfig());
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        QSignalSpy warn(&sim.s, &Scheduler::preWarning);
        sim.s.pause(0);
        QCOMPARE(sim.s.state(), Scheduler::State::Paused);
        QCOMPARE(sim.s.pauseRemainingSeconds(), -1);
        sim.active(15);
        QCOMPARE(due.count(), 0);
        QCOMPARE(warn.count(), 0);
        QCOMPARE(sim.s.sittingSeconds(), 15);
        sim.s.resume();
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        sim.active(1);
        QCOMPARE(due.count(), 1);
        QCOMPARE(due.at(0).at(0).toInt(), 16);
    }

    void timedPauseEndsByItself()
    {
        Sim sim(shortConfig());
        sim.s.pause(5);
        QCOMPARE(sim.s.pauseRemainingSeconds(), 5);
        sim.active(4);
        QCOMPARE(sim.s.state(), Scheduler::State::Paused);
        QCOMPARE(sim.s.pauseRemainingSeconds(), 1);
        sim.active(1);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QVERIFY(!sim.s.isPaused());
    }

    void awayWhilePausedReturnsToPaused()
    {
        SchedulerConfig c = shortConfig();
        c.workSeconds = 100;
        Sim sim(c);
        sim.active(20);
        sim.s.pause(0);
        sim.inactive(5);
        QCOMPARE(sim.s.state(), Scheduler::State::Away);
        sim.active(1);
        QCOMPARE(sim.s.state(), Scheduler::State::Paused);
        QCOMPARE(sim.s.sittingSeconds(), 0);
    }

    void resetCountsAsManualBreak()
    {
        Sim sim(shortConfig());
        QSignalSpy manual(&sim.s, &Scheduler::manualBreak);
        sim.active(10);
        sim.s.reset();
        QCOMPARE(manual.count(), 1);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.sittingSeconds(), 0);
    }

    void newWorkIntervalAppliesToRunningCycle()
    {
        Sim sim(shortConfig());
        QSignalSpy due(&sim.s, &Scheduler::breakDue);
        sim.active(10);
        QCOMPARE(sim.s.state(), Scheduler::State::Due);
        SchedulerConfig c = shortConfig();
        c.workSeconds = 20;
        sim.s.setConfig(c);
        QCOMPARE(sim.s.state(), Scheduler::State::Working);
        QCOMPARE(sim.s.secondsUntilBreak(), 10);
        sim.active(10);
        QCOMPARE(due.count(), 2);
    }

    void clockGoingBackwardsAddsNothing()
    {
        Sim sim(shortConfig());
        sim.active(3);
        sim.now -= 60'000;
        sim.s.tick(sim.now, 0, false);
        QCOMPARE(sim.s.sittingSeconds(), 3);
        sim.active(1);
        QCOMPARE(sim.s.sittingSeconds(), 4);
    }
};

QTEST_GUILESS_MAIN(TestScheduler)
#include "test_scheduler.moc"

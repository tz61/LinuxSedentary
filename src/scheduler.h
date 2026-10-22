#pragma once

#include <QObject>

// Durations the scheduler works with, all in seconds.
struct SchedulerConfig {
    int workSeconds = 45 * 60;   // sitting time before a break is due
    int breakSeconds = 5 * 60;   // length of a break
    int snoozeSeconds = 5 * 60;  // how far "Snooze" pushes a due break
    int preWarnSeconds = 60;     // early warning before a break, 0 = off
    int awaySeconds = 5 * 60;    // inactivity that counts as having stood up
    int nagSeconds = 5 * 60;     // repeat interval while a break is overdue
};

// Tracks how long the user has been sitting and when a break is due.
//
// The scheduler owns no timers: the caller feeds it the wall clock, the
// session idle time and the screen-lock state once a second via tick().
// That keeps it deterministic and easy to test.
class Scheduler : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Working,  // sitting time accumulates towards the next break
        Due,      // a break is due (or overdue) and the user was told
        OnBreak,  // the user accepted a break; counting it down
        Away,     // the user is inactive long enough to count as a break
        Paused,   // reminders are off; sitting time still accumulates
    };
    Q_ENUM(State)

    explicit Scheduler(QObject *parent = nullptr);

    void setConfig(const SchedulerConfig &config);
    const SchedulerConfig &config() const { return m_config; }

    // nowMs: wall clock in milliseconds. idleSeconds: time since the last
    // keyboard/mouse input. locked: the screen locker is active.
    // idleInhibited: an application (e.g. a video player) blocks the
    // screensaver, so a lack of input does not mean the user left.
    void tick(qint64 nowMs, int idleSeconds, bool locked, bool idleInhibited = false);

    State state() const { return m_state; }
    bool isPaused() const { return m_paused; }
    // Continuous sitting time since the last real break.
    int sittingSeconds() const { return int(m_sittingMs / 1000); }
    // Seconds until the next break; negative while it is overdue.
    int secondsUntilBreak() const;
    // Share of the current cycle that has elapsed, 0..1 (1 when due).
    double cycleProgress() const;
    int breakRemainingSeconds() const;
    // Seconds until a timed pause ends; -1 when paused indefinitely or not paused.
    int pauseRemainingSeconds() const;
    int snoozeCount() const { return m_snoozeCount; }

public Q_SLOTS:
    void startBreak();
    void endBreak();      // stop a running break early
    void voidBreak();     // the break was disturbed (mouse moved): it does not count
    void snooze();
    void skip();
    void reset();         // "I just took a break"
    void pause(int seconds); // 0 = until resume()
    void resume();

Q_SIGNALS:
    void stateChanged(Scheduler::State state);
    void preWarning(int secondsLeft);
    void breakDue(int sittingSeconds, bool repeated);
    void breakStarted(int seconds);
    void breakFinished(bool completed);
    // A break that was disturbed and does not count; the work cycle goes on.
    void breakVoided();
    void breakSkipped();
    void snoozed(int seconds);
    void manualBreak();
    // The user came back after being away (idle, locked or suspended) long
    // enough to count as a break. wasDue: a reminder was showing.
    void naturalBreak(int awaySeconds, bool wasDue);
    // A sitting stretch ended (any kind of break). For "longest stretch" stats.
    void stretchEnded(int seconds);
    // Sitting time added (or, negative, taken back) since the last tick.
    void sittingCounted(qint64 ms);

private:
    void setState(State state);
    void startNewCycle();
    void enterAway(qint64 inactiveMs, qint64 uncountedMs);
    void leaveAway(qint64 nowMs);
    void finishBreak(bool completed);
    State restingState() const { return m_paused ? State::Paused : State::Working; }

    SchedulerConfig m_config;
    State m_state = State::Working;
    bool m_started = false;
    qint64 m_lastTickMs = 0;
    qint64 m_nowMs = 0;

    qint64 m_sittingMs = 0;     // sitting since the last real break
    qint64 m_cycleStartMs = 0;  // sitting value when the reminder cycle began
    qint64 m_dueAtMs = 0;       // sitting value at which the break is due
    qint64 m_lastNagMs = 0;     // sitting value at the last reminder
    bool m_preWarned = false;
    int m_snoozeCount = 0;

    qint64 m_breakLeftMs = 0;
    qint64 m_awaySinceMs = 0;
    qint64 m_awayThresholdMs = 0;
    bool m_awayWasDue = false;
    bool m_awayCounts = false;  // enough sitting before leaving to count as a break
    qint64 m_lockedSinceMs = 0;

    bool m_paused = false;
    qint64 m_pauseUntilMs = 0;  // 0 = indefinitely
};

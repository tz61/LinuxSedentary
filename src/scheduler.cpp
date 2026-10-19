#include "scheduler.h"

#include <QDateTime>

#include <algorithm>

namespace {

// Ticks arrive once a second. A longer gap means the machine was suspended
// or the event loop stalled; such a gap is treated as inactivity.
constexpr qint64 kMaxTickGapMs = 10'000;

qint64 ms(int seconds)
{
    return qint64(seconds) * 1000;
}

int ceilSeconds(qint64 milliseconds)
{
    return milliseconds > 0 ? int((milliseconds + 999) / 1000) : 0;
}

} // namespace

Scheduler::Scheduler(QObject *parent)
    : QObject(parent)
{
    startNewCycle();
}

void Scheduler::setConfig(const SchedulerConfig &config)
{
    m_config = config;
    if (m_state == State::OnBreak || m_state == State::Away)
        return;
    // Apply a new work interval to the running cycle unless the user
    // already moved the deadline with Snooze.
    if (m_snoozeCount == 0)
        m_dueAtMs = m_cycleStartMs + ms(m_config.workSeconds);
    if (m_sittingMs < m_dueAtMs - ms(m_config.preWarnSeconds))
        m_preWarned = false;
    if (m_state == State::Due && m_sittingMs < m_dueAtMs)
        setState(State::Working);
}

void Scheduler::tick(qint64 nowMs, int idleSeconds, bool locked, bool idleInhibited)
{
    if (!m_started) {
        m_started = true;
        m_lastTickMs = m_nowMs = nowMs;
        if (locked)
            m_lockedSinceMs = nowMs;
        return;
    }

    const qint64 dt = std::max<qint64>(0, nowMs - m_lastTickMs);
    m_lastTickMs = m_nowMs = nowMs;

    if (m_paused && m_pauseUntilMs > 0 && nowMs >= m_pauseUntilMs)
        resume();

    // How long the user has shown no sign of being at the computer.
    qint64 inactiveMs = idleInhibited ? 0 : ms(std::max(0, idleSeconds));
    if (locked) {
        if (m_lockedSinceMs == 0)
            m_lockedSinceMs = nowMs - dt;
        inactiveMs = std::max(inactiveMs, nowMs - m_lockedSinceMs);
    } else {
        m_lockedSinceMs = 0;
    }
    const bool gap = dt > kMaxTickGapMs;
    if (gap)
        inactiveMs = std::max(inactiveMs, dt);

    if (m_state == State::OnBreak) {
        // Being inactive is the point of a break; just count it down.
        m_breakLeftMs -= dt;
        if (m_breakLeftMs <= 0)
            finishBreak(true);
        return;
    }

    if (m_state == State::Away) {
        if (inactiveMs < m_awayThresholdMs)
            leaveAway(nowMs);
        return;
    }

    // While a reminder is showing, a lack of input as long as a break
    // already satisfies it.
    qint64 threshold = ms(m_config.awaySeconds);
    if (m_state == State::Due)
        threshold = std::min(threshold, ms(m_config.breakSeconds));
    threshold = std::max<qint64>(threshold, 1000);
    if (inactiveMs >= threshold) {
        m_awayThresholdMs = threshold;
        enterAway(inactiveMs, dt);
        return;
    }

    m_sittingMs += dt;
    if (dt > 0)
        Q_EMIT sittingCounted(dt);

    if (m_state == State::Working) {
        const qint64 preWarnMs = ms(m_config.preWarnSeconds);
        if (!m_preWarned && preWarnMs > 0 && preWarnMs < ms(m_config.workSeconds)
            && m_sittingMs >= m_dueAtMs - preWarnMs && m_sittingMs < m_dueAtMs) {
            m_preWarned = true;
            Q_EMIT preWarning(ceilSeconds(m_dueAtMs - m_sittingMs));
        }
        if (m_sittingMs >= m_dueAtMs) {
            m_lastNagMs = m_sittingMs;
            setState(State::Due);
            Q_EMIT breakDue(sittingSeconds(), false);
        }
    } else if (m_state == State::Due) {
        const qint64 nagMs = ms(m_config.nagSeconds);
        if (nagMs > 0 && m_sittingMs - m_lastNagMs >= nagMs) {
            m_lastNagMs = m_sittingMs;
            Q_EMIT breakDue(sittingSeconds(), true);
        }
    }
}

int Scheduler::secondsUntilBreak() const
{
    const qint64 left = m_dueAtMs - m_sittingMs;
    return left >= 0 ? ceilSeconds(left) : -int(-left / 1000);
}

double Scheduler::cycleProgress() const
{
    if (m_state == State::Due)
        return 1.0;
    if (m_state == State::Away || m_state == State::OnBreak)
        return 0.0;
    const qint64 span = m_dueAtMs - m_cycleStartMs;
    if (span <= 0)
        return 1.0;
    return std::clamp(double(m_sittingMs - m_cycleStartMs) / double(span), 0.0, 1.0);
}

int Scheduler::breakRemainingSeconds() const
{
    return m_state == State::OnBreak ? ceilSeconds(m_breakLeftMs) : 0;
}

int Scheduler::pauseRemainingSeconds() const
{
    if (!m_paused || m_pauseUntilMs == 0)
        return -1;
    return ceilSeconds(m_pauseUntilMs - m_nowMs);
}

void Scheduler::startBreak()
{
    if (m_state == State::OnBreak)
        return;
    if (m_state != State::Away)
        Q_EMIT stretchEnded(sittingSeconds());
    m_breakLeftMs = ms(m_config.breakSeconds);
    setState(State::OnBreak);
    Q_EMIT breakStarted(m_config.breakSeconds);
}

void Scheduler::endBreak()
{
    if (m_state == State::OnBreak)
        finishBreak(false);
}

void Scheduler::snooze()
{
    if (m_state == State::Due)
        m_dueAtMs = m_sittingMs + ms(m_config.snoozeSeconds);
    else if (m_state == State::Working || m_state == State::Paused)
        m_dueAtMs = std::max(m_dueAtMs, m_sittingMs) + ms(m_config.snoozeSeconds);
    else
        return;
    ++m_snoozeCount;
    m_preWarned = true;
    if (m_state == State::Due)
        setState(State::Working);
    Q_EMIT snoozed(m_config.snoozeSeconds);
}

void Scheduler::skip()
{
    if (m_state != State::Due && m_state != State::Working)
        return;
    // The sitting stretch goes on; only the reminder cycle restarts.
    m_cycleStartMs = m_sittingMs;
    m_dueAtMs = m_sittingMs + ms(m_config.workSeconds);
    m_lastNagMs = m_sittingMs;
    m_preWarned = false;
    m_snoozeCount = 0;
    setState(State::Working);
    Q_EMIT breakSkipped();
}

void Scheduler::reset()
{
    if (m_state == State::OnBreak) {
        finishBreak(false);
        return;
    }
    if (m_state != State::Away)
        Q_EMIT stretchEnded(sittingSeconds());
    startNewCycle();
    setState(restingState());
    Q_EMIT manualBreak();
}

void Scheduler::pause(int seconds)
{
    if (m_state == State::OnBreak)
        finishBreak(false);
    const qint64 now = m_started ? m_nowMs : QDateTime::currentMSecsSinceEpoch();
    m_paused = true;
    m_pauseUntilMs = seconds > 0 ? now + ms(seconds) : 0;
    if (m_state == State::Working || m_state == State::Due)
        setState(State::Paused);
}

void Scheduler::resume()
{
    if (!m_paused)
        return;
    m_paused = false;
    m_pauseUntilMs = 0;
    // An overdue break is reported again by the next tick.
    if (m_state == State::Paused)
        setState(State::Working);
}

void Scheduler::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void Scheduler::startNewCycle()
{
    m_sittingMs = 0;
    m_cycleStartMs = 0;
    m_dueAtMs = ms(m_config.workSeconds);
    m_lastNagMs = 0;
    m_preWarned = false;
    m_snoozeCount = 0;
}

void Scheduler::enterAway(qint64 inactiveMs, qint64 uncountedMs)
{
    m_awaySinceMs = m_nowMs - inactiveMs;
    m_awayWasDue = m_state == State::Due;

    // The idle stretch was counted as sitting until it crossed the
    // threshold (reading without touching the mouse is still sitting).
    // Now that it turned out to be a break, take it back. The current
    // tick (uncountedMs) was never added.
    const qint64 overcount = std::clamp<qint64>(inactiveMs - uncountedMs, 0, m_sittingMs);
    m_sittingMs -= overcount;
    if (overcount > 0)
        Q_EMIT sittingCounted(-overcount);

    m_awayCounts = m_awayWasDue || m_sittingMs >= ms(m_config.awaySeconds);
    Q_EMIT stretchEnded(sittingSeconds());
    startNewCycle();
    setState(State::Away);
}

void Scheduler::leaveAway(qint64 nowMs)
{
    const int awaySeconds = int((nowMs - m_awaySinceMs) / 1000);
    setState(restingState());
    if (m_awayCounts)
        Q_EMIT naturalBreak(awaySeconds, m_awayWasDue);
}

void Scheduler::finishBreak(bool completed)
{
    m_breakLeftMs = 0;
    startNewCycle();
    setState(restingState());
    Q_EMIT breakFinished(completed);
}

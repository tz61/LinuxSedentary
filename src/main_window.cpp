#include "main_window.h"

#include "format.h"
#include "history.h"
#include "icons.h"
#include "idle_monitor.h"
#include "scheduler.h"
#include "settings.h"

#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace detail {

QFont scaled(const QFont &base, qreal factor, bool bold)
{
    QFont f = base;
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * factor);
    else
        f.setPixelSize(int(f.pixelSize() * factor));
    f.setBold(bold);
    return f;
}

class StatusRing : public QWidget
{
public:
    explicit StatusRing(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(176, 176);
    }

    void set(double progress, const QColor &color, const QString &big, const QString &small)
    {
        m_progress = progress;
        m_color = color;
        m_big = big;
        m_small = small;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal pen = 14;
        const QRectF r = QRectF(rect()).adjusted(pen / 2 + 2, pen / 2 + 2, -pen / 2 - 2, -pen / 2 - 2);
        QColor track = palette().color(QPalette::Mid);
        track.setAlphaF(0.35);
        p.setPen(QPen(track, pen, Qt::SolidLine, Qt::FlatCap));
        p.drawEllipse(r);
        if (m_progress > 0) {
            p.setPen(QPen(m_color, pen, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(r, 90 * 16, -int(std::clamp(m_progress, 0.0, 1.0) * 360 * 16));
        }

        p.setPen(palette().color(QPalette::WindowText));
        QFont big = scaled(font(), 2.6, true);
        p.setFont(big);
        const QRectF bigBox(r.left(), r.center().y() - r.height() * 0.30, r.width(), r.height() * 0.40);
        p.drawText(bigBox, Qt::AlignHCenter | Qt::AlignBottom, m_big);
        p.setFont(scaled(font(), 0.95, false));
        p.setPen(Icons::mutedText(palette()));
        const QRectF smallBox(r.left(), bigBox.bottom() + 2, r.width(), r.height() * 0.2);
        p.drawText(smallBox, Qt::AlignHCenter | Qt::AlignTop, m_small);
    }

private:
    double m_progress = 0;
    QColor m_color;
    QString m_big;
    QString m_small;
};

class WeekChart : public QWidget
{
public:
    struct Day {
        QDate date;
        int sittingSeconds = 0;
        int breaks = 0;
    };

    explicit WeekChart(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(190);
    }

    void setDays(const QVector<Day> &days)
    {
        m_days = days;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_days.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics fm(font());
        const int labelH = fm.height() * 2 + 6;
        const int valueH = fm.height() + 4;
        const QRect plot = rect().adjusted(4, valueH, -4, -labelH);

        int maxSeconds = 4 * 3600;
        for (const Day &d : m_days)
            maxSeconds = std::max(maxSeconds, d.sittingSeconds);

        const qreal slot = qreal(plot.width()) / m_days.size();
        const qreal barW = std::min<qreal>(slot * 0.56, 44);
        const QColor text = palette().color(QPalette::WindowText);
        const QColor muted = Icons::mutedText(palette());
        QColor gridColor = palette().color(QPalette::Mid);
        gridColor.setAlphaF(0.4);

        p.setPen(QPen(gridColor, 1));
        p.drawLine(plot.bottomLeft(), plot.bottomRight());

        for (int i = 0; i < m_days.size(); ++i) {
            const Day &d = m_days.at(i);
            const bool today = i == m_days.size() - 1;
            const qreal cx = plot.left() + slot * (i + 0.5);
            const qreal h = plot.height() * qreal(d.sittingSeconds) / maxSeconds;
            const QRectF bar(cx - barW / 2, plot.bottom() - h, barW, h);

            QColor fill = Icons::okColor();
            if (!today)
                fill.setAlphaF(0.55);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            if (h > 0.5)
                p.drawRoundedRect(bar, 5, 5);

            p.setPen(today ? text : muted);
            p.setFont(font());
            if (d.sittingSeconds >= 60) {
                const int minutes = d.sittingSeconds / 60;
                const QString value = minutes >= 60
                    ? QStringLiteral("%1h%2").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'))
                    : QStringLiteral("%1m").arg(minutes);
                p.drawText(QRectF(cx - slot / 2, bar.top() - valueH, slot, valueH), Qt::AlignCenter, value);
            }

            QFont dayFont = font();
            dayFont.setBold(today);
            p.setFont(dayFont);
            p.setPen(today ? text : muted);
            const QString name = today ? QStringLiteral("Today") : QLocale().dayName(d.date.dayOfWeek(), QLocale::ShortFormat);
            p.drawText(QRectF(cx - slot / 2, plot.bottom() + 4, slot, fm.height()), Qt::AlignCenter, name);
            p.setFont(font());
            p.setPen(muted);
            const QString breaks = d.breaks == 1 ? QStringLiteral("1 break") : QStringLiteral("%1 breaks").arg(d.breaks);
            p.drawText(QRectF(cx - slot / 2, plot.bottom() + 4 + fm.height(), slot, fm.height()), Qt::AlignCenter,
                       breaks);
        }
    }

private:
    QVector<Day> m_days;
};

} // namespace detail

namespace {

QLabel *addTile(QGridLayout *grid, int column, const QString &caption, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("tile"));
    frame->setStyleSheet(QStringLiteral("QFrame#tile { background: palette(base); border-radius: 10px; }"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(2);
    auto *value = new QLabel(QStringLiteral("–"), frame);
    value->setFont(detail::scaled(parent->font(), 1.45, true));
    auto *label = new QLabel(caption, frame);
    Icons::setMuted(label);
    layout->addWidget(value);
    layout->addWidget(label);
    grid->addWidget(frame, 0, column);
    return value;
}

QLabel *heading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(detail::scaled(parent->font(), 1.1, true));
    return label;
}

} // namespace

MainWindow::MainWindow(Scheduler *scheduler, const History *history, const Settings *settings,
                       const IdleMonitor *idle, bool closeQuits, QWidget *parent)
    : QWidget(parent)
    , m_scheduler(scheduler)
    , m_history(history)
    , m_settings(settings)
    , m_idle(idle)
    , m_closeQuits(closeQuits)
{
    setWindowTitle(QStringLiteral("Sedentary Reminder"));
    setWindowIcon(Icons::appIcon());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 14);
    layout->setSpacing(12);

    // Status: ring + text + actions.
    auto *top = new QHBoxLayout;
    top->setSpacing(22);
    m_ring = new detail::StatusRing(this);
    top->addWidget(m_ring);
    auto *status = new QVBoxLayout;
    status->setSpacing(6);
    status->addStretch();
    m_state = new QLabel(this);
    m_state->setFont(detail::scaled(font(), 1.5, true));
    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    Icons::setMuted(m_detail);
    status->addWidget(m_state);
    status->addWidget(m_detail);
    status->addSpacing(10);

    auto *actions = new QGridLayout;
    actions->setHorizontalSpacing(8);
    actions->setVerticalSpacing(8);
    m_breakButton = new QPushButton(this);
    m_resetButton = new QPushButton(QStringLiteral("I just took a break"), this);
    m_pauseButton = new QPushButton(this);
    auto *settingsButton = new QPushButton(QStringLiteral("Settings…"), this);
    actions->addWidget(m_breakButton, 0, 0);
    actions->addWidget(m_resetButton, 0, 1);
    actions->addWidget(m_pauseButton, 1, 0);
    actions->addWidget(settingsButton, 1, 1);
    status->addLayout(actions);
    status->addStretch();
    top->addLayout(status, 1);
    layout->addLayout(top);

    m_pauseMenu = new QMenu(this);
    const int unit = m_settings->minuteSeconds();
    m_pauseMenu->addAction(QStringLiteral("For 30 minutes"), this, [this, unit] { m_scheduler->pause(30 * unit); });
    m_pauseMenu->addAction(QStringLiteral("For 1 hour"), this, [this, unit] { m_scheduler->pause(60 * unit); });
    m_pauseMenu->addAction(QStringLiteral("For 2 hours"), this, [this, unit] { m_scheduler->pause(120 * unit); });
    m_pauseMenu->addAction(QStringLiteral("Until I resume"), this, [this] { m_scheduler->pause(0); });

    connect(m_breakButton, &QPushButton::clicked, this, [this] {
        if (m_scheduler->state() == Scheduler::State::OnBreak)
            m_scheduler->endBreak();
        else
            m_scheduler->startBreak();
    });
    connect(m_resetButton, &QPushButton::clicked, m_scheduler, &Scheduler::reset);
    connect(m_pauseButton, &QPushButton::clicked, this, [this] {
        if (m_scheduler->isPaused())
            m_scheduler->resume();
    });
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::settingsRequested);

    // Today.
    layout->addSpacing(4);
    layout->addWidget(heading(QStringLiteral("Today"), this));
    auto *tiles = new QGridLayout;
    tiles->setHorizontalSpacing(10);
    m_sittingToday = addTile(tiles, 0, QStringLiteral("sitting"), this);
    m_breaksToday = addTile(tiles, 1, QStringLiteral("breaks"), this);
    m_longestToday = addTile(tiles, 2, QStringLiteral("longest stretch"), this);
    m_skippedToday = addTile(tiles, 3, QStringLiteral("skipped / snoozed"), this);
    layout->addLayout(tiles);

    // Week.
    layout->addSpacing(4);
    layout->addWidget(heading(QStringLiteral("Last 7 days"), this));
    m_chart = new detail::WeekChart(this);
    layout->addWidget(m_chart, 1);

    m_footer = new QLabel(this);
    Icons::setMuted(m_footer);
    m_footer->setFont(detail::scaled(font(), 0.85, false));
    m_footer->setWordWrap(true);
    layout->addWidget(m_footer);

    resize(620, 600);
    refresh();
}

void MainWindow::refresh()
{
    const Scheduler::State state = m_scheduler->state();
    const Settings &s = *m_settings;
    const int sitting = s.displaySeconds(m_scheduler->sittingSeconds());
    const int left = s.displaySeconds(m_scheduler->secondsUntilBreak());

    switch (state) {
    case Scheduler::State::Working: {
        const double progress = m_scheduler->cycleProgress();
        m_ring->set(progress, progress < 0.75 ? Icons::okColor() : Icons::warnColor(),
                    QString::number((left + 59) / 60), QStringLiteral("min to break"));
        m_state->setText(sitting < 60 ? QStringLiteral("Fresh start")
                                      : QStringLiteral("Sitting for %1").arg(Format::duration(sitting)));
        m_detail->setText(QStringLiteral("Next break in %1.").arg(Format::minutesCeil(left)));
        break;
    }
    case Scheduler::State::Due:
        m_ring->set(1.0, Icons::dueColor(), QStringLiteral("Now"), QStringLiteral("break due"));
        m_state->setText(QStringLiteral("Time for a break"));
        m_detail->setText(left < -59 ? QStringLiteral("Sitting for %1, %2 overdue.")
                                           .arg(Format::duration(sitting), Format::duration(-left))
                                     : QStringLiteral("Sitting for %1.").arg(Format::duration(sitting)));
        break;
    case Scheduler::State::OnBreak: {
        const int total = std::max(1, m_scheduler->config().breakSeconds);
        const int remaining = m_scheduler->breakRemainingSeconds();
        m_ring->set(1.0 - double(remaining) / total, Icons::breakColor(),
                    Format::clock(s.displaySeconds(remaining)), QStringLiteral("break left"));
        m_state->setText(QStringLiteral("On a break"));
        m_detail->setText(QStringLiteral("Stand up, stretch and step away from the screen."));
        break;
    }
    case Scheduler::State::Away:
        m_ring->set(0.0, Icons::idleColor(), QStringLiteral("–"), QStringLiteral("away"));
        m_state->setText(QStringLiteral("Away"));
        m_detail->setText(QStringLiteral("No activity for a while, so this counts as a break."));
        break;
    case Scheduler::State::Paused: {
        const int pauseLeft = m_scheduler->pauseRemainingSeconds();
        m_ring->set(m_scheduler->cycleProgress(), Icons::idleColor(), QStringLiteral("II"), QStringLiteral("paused"));
        m_state->setText(QStringLiteral("Reminders paused"));
        const QString until = pauseLeft < 0 ? QStringLiteral("Until you resume")
                                            : QStringLiteral("Resuming in %1")
                                                  .arg(Format::minutesCeil(s.displaySeconds(pauseLeft)));
        m_detail->setText(QStringLiteral("%1. Sitting time still counts (%2 so far).")
                              .arg(until, Format::duration(sitting)));
        break;
    }
    }

    m_breakButton->setText(state == Scheduler::State::OnBreak ? QStringLiteral("End break")
                                                              : QStringLiteral("Take a break now"));
    m_resetButton->setEnabled(state != Scheduler::State::OnBreak && state != Scheduler::State::Away);
    if (m_scheduler->isPaused()) {
        m_pauseButton->setText(QStringLiteral("Resume reminders"));
        m_pauseButton->setMenu(nullptr);
    } else {
        m_pauseButton->setText(QStringLiteral("Pause"));
        m_pauseButton->setMenu(m_pauseMenu);
    }

    // Statistics.
    const QDate today = QDate::currentDate();
    const DayStats d = m_history->day(today);
    m_sittingToday->setText(Format::duration(d.sittingSeconds()));
    m_breaksToday->setText(QString::number(d.totalBreaks()));
    m_breaksToday->setToolTip(QStringLiteral("%1 from reminders, %2 on your own").arg(d.breaks).arg(d.naturalBreaks));
    const bool sittingNow = state == Scheduler::State::Working || state == Scheduler::State::Due
        || state == Scheduler::State::Paused;
    const int longest = std::max(d.longestStretch, sittingNow ? m_scheduler->sittingSeconds() : 0);
    m_longestToday->setText(Format::duration(s.displaySeconds(longest)));
    m_skippedToday->setText(QStringLiteral("%1 / %2").arg(d.skipped).arg(d.snoozed));

    QVector<detail::WeekChart::Day> days;
    for (int i = 6; i >= 0; --i) {
        const QDate date = today.addDays(-i);
        const DayStats ds = m_history->day(date);
        days.append({date, ds.sittingSeconds(), ds.totalBreaks()});
    }
    m_chart->setDays(days);

    m_footer->setText(QStringLiteral("Idle detection: %1%2")
                          .arg(m_idle->backendName(),
                               s.fastMode ? QStringLiteral(" · fast mode (minutes last one second)") : QString()));
}

void MainWindow::showEvent(QShowEvent *event)
{
    refresh();
    QWidget::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeQuits)
        Q_EMIT quitRequested();
    QWidget::closeEvent(event);
}

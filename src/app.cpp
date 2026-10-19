#include "app.h"

#include "autostart.h"
#include "break_window.h"
#include "format.h"
#include "icons.h"
#include "idle_monitor.h"
#include "main_window.h"
#include "notifier.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QDateTime>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>

namespace {

constexpr int kTickMs = 1000;
constexpr int kSaveMs = 60'000;
constexpr int kTrayRetryMs = 2000;
constexpr int kTrayRetries = 30;
// A break hint about typing appears only after the break has run this long.
constexpr int kActivityHintAfterSeconds = 10;

QDate today()
{
    return QDate::currentDate();
}

} // namespace

App::App(bool fastMode, QObject *parent)
    : QObject(parent)
{
    m_settings = Settings::load();
    m_settings.fastMode = fastMode;
    m_scheduler.setConfig(m_settings.schedulerConfig());
    m_history.load();
    m_history.prune(today());

    m_idle = new IdleMonitor(this);
    m_notifier = new Notifier(this);

    connect(&m_scheduler, &Scheduler::stateChanged, this, &App::onStateChanged);
    connect(&m_scheduler, &Scheduler::preWarning, this, &App::onPreWarning);
    connect(&m_scheduler, &Scheduler::breakDue, this, &App::onBreakDue);
    connect(&m_scheduler, &Scheduler::breakStarted, this, &App::onBreakStarted);
    connect(&m_scheduler, &Scheduler::breakFinished, this, &App::onBreakFinished);
    connect(&m_scheduler, &Scheduler::breakSkipped, this, &App::onBreakSkipped);
    connect(&m_scheduler, &Scheduler::snoozed, this, &App::onSnoozed);
    connect(&m_scheduler, &Scheduler::manualBreak, this, &App::onManualBreak);
    connect(&m_scheduler, &Scheduler::naturalBreak, this, &App::onNaturalBreak);
    connect(&m_scheduler, &Scheduler::stretchEnded, this,
            [this](int seconds) { m_history.recordStretch(today(), seconds); });
    connect(&m_scheduler, &Scheduler::sittingCounted, this,
            [this](qint64 ms) { m_history.addSitting(today(), ms); });
    connect(m_notifier, &Notifier::actionInvoked, this, &App::onNotificationAction);

    m_tickTimer.setInterval(kTickMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &App::tick);
    m_saveTimer.setInterval(kSaveMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &App::saveHistory);
    m_trayRetry.setInterval(kTrayRetryMs);
    connect(&m_trayRetry, &QTimer::timeout, this, &App::waitForTray);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &App::saveHistory);
}

App::~App()
{
    saveHistory();
    delete m_breakWindow.data();
    delete m_mainWindow.data();
    delete m_menu;
}

void App::start(bool showWindow)
{
    tick();
    m_tickTimer.start();
    m_saveTimer.start();

    if (QSystemTrayIcon::isSystemTrayAvailable())
        createTray();
    else
        m_trayRetry.start(); // at login the panel may come up after us

    if (showWindow)
        showMainWindow();
}

void App::waitForTray()
{
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayRetry.stop();
        createTray();
    } else if (++m_trayRetries >= kTrayRetries) {
        m_trayRetry.stop();
        showMainWindow();
    }
}

// --- Ticking -----------------------------------------------------------------

void App::tick()
{
    const int idle = m_idle->idleSeconds();
    const int inhibitCheckFrom = std::min(30, m_scheduler.config().awaySeconds / 2);
    const bool inhibited = m_settings.respectInhibit && idle >= inhibitCheckFrom && m_idle->isIdleInhibited();
    m_scheduler.tick(QDateTime::currentMSecsSinceEpoch(), idle, m_idle->isLocked(), inhibited);

    if (m_scheduler.state() == Scheduler::State::OnBreak && m_breakWindow && m_breakWindow->isVisible()) {
        const int remaining = m_scheduler.breakRemainingSeconds();
        const int elapsed = m_scheduler.config().breakSeconds - remaining;
        m_breakWindow->updateBreak(m_settings.displaySeconds(remaining),
                                   idle < 2 && elapsed >= kActivityHintAfterSeconds);
    }
    updateTray();
    if (m_mainWindow && m_mainWindow->isVisible())
        m_mainWindow->refresh();
}

QString App::statusText() const
{
    const Settings &s = m_settings;
    const QString sitting = Format::duration(s.displaySeconds(m_scheduler.sittingSeconds()));
    switch (m_scheduler.state()) {
    case Scheduler::State::Working: {
        const QString next = Format::minutesCeil(s.displaySeconds(m_scheduler.secondsUntilBreak()));
        if (s.displaySeconds(m_scheduler.sittingSeconds()) < 60)
            return QStringLiteral("Next break in %1").arg(next);
        return QStringLiteral("Sitting for %1 · break in %2").arg(sitting, next);
    }
    case Scheduler::State::Due:
        return QStringLiteral("Break due · sitting for %1").arg(sitting);
    case Scheduler::State::OnBreak:
        return QStringLiteral("On a break · %1 left")
            .arg(Format::minutesCeil(s.displaySeconds(m_scheduler.breakRemainingSeconds())));
    case Scheduler::State::Away:
        return QStringLiteral("Away · counts as a break");
    case Scheduler::State::Paused: {
        const int left = m_scheduler.pauseRemainingSeconds();
        if (left < 0)
            return QStringLiteral("Paused · sitting for %1").arg(sitting);
        return QStringLiteral("Paused for %1 more · sitting for %2")
            .arg(Format::minutesCeil(s.displaySeconds(left)), sitting);
    }
    }
    return {};
}

// --- Scheduler events ------------------------------------------------------------

void App::onStateChanged()
{
    const Scheduler::State state = m_scheduler.state();
    // Leaving "due" other than by taking a break (snooze, skip, pause, a
    // longer work interval): the reminder is no longer current.
    if (m_prevState == Scheduler::State::Due
        && (state == Scheduler::State::Working || state == Scheduler::State::Paused))
        closeReminders();
    m_prevState = state;

    updateTray(true);
    updateMenu();
    if (m_mainWindow && m_mainWindow->isVisible())
        m_mainWindow->refresh();
}

void App::onPreWarning(int secondsLeft)
{
    playSound(Notifier::Sound::PreWarning);
    QVector<Notifier::Action> actions;
    if (m_notifier->supportsActions()) {
        actions.append({QStringLiteral("start"), QStringLiteral("Take it now")});
        if (snoozeAllowed())
            actions.append({QStringLiteral("snooze"), snoozeLabel()});
    }
    m_notifier->show(QStringLiteral("Break in %1").arg(Format::minutesCeil(m_settings.displaySeconds(secondsLeft))),
                     QStringLiteral("Time to wrap up what you are doing."), actions, Notifier::Urgency::Normal,
                     15'000);
}

void App::onBreakDue(int sittingSeconds, bool repeated)
{
    Q_UNUSED(repeated)
    playSound(Notifier::Sound::Reminder);

    const int sitting = m_settings.displaySeconds(sittingSeconds);
    if (useWindow()) {
        m_notifier->close();
        breakWindow()->showReminder(sitting, m_settings.style == Settings::Style::Fullscreen, snoozeAllowed(),
                                    snoozeLabel(), !m_settings.strict);
        return;
    }

    QVector<Notifier::Action> actions;
    if (m_notifier->supportsActions()) {
        actions.append({QStringLiteral("start"), QStringLiteral("Start break")});
        if (snoozeAllowed())
            actions.append({QStringLiteral("snooze"), snoozeLabel()});
        if (!m_settings.strict)
            actions.append({QStringLiteral("skip"), QStringLiteral("Skip")});
    }
    m_notifier->show(QStringLiteral("Time to stand up"),
                     QStringLiteral("You have been sitting for %1. Get up and move for a few minutes.")
                         .arg(Format::duration(sitting)),
                     actions, Notifier::Urgency::Critical, 0);
}

void App::onBreakStarted(int seconds)
{
    playSound(Notifier::Sound::BreakStart);
    const int length = m_settings.displaySeconds(seconds);
    if (useWindow()) {
        m_notifier->close();
        breakWindow()->showBreak(length, m_settings.style == Settings::Style::Fullscreen);
        return;
    }
    QVector<Notifier::Action> actions;
    if (m_notifier->supportsActions())
        actions.append({QStringLiteral("end"), QStringLiteral("End break")});
    m_notifier->show(QStringLiteral("Break started"),
                     QStringLiteral("Stand up and move. Back in %1.").arg(Format::minutesCeil(length)), actions,
                     Notifier::Urgency::Normal, 10'000);
}

void App::onBreakFinished(bool completed)
{
    m_history.addBreak(today());
    saveHistory();
    if (completed)
        playSound(Notifier::Sound::BreakOver);

    const QString next = Format::minutesCeil(m_settings.displaySeconds(m_scheduler.config().workSeconds));
    if (m_breakWindow && m_breakWindow->isVisible()) {
        if (completed)
            m_breakWindow->showDone(next);
        else
            m_breakWindow->dismiss();
    } else if (completed) {
        m_notifier->show(QStringLiteral("Break over"), QStringLiteral("Welcome back! Next break in %1.").arg(next),
                         {}, Notifier::Urgency::Normal, 8000);
    } else {
        m_notifier->close();
    }
}

void App::onBreakSkipped()
{
    m_history.addSkip(today());
    closeReminders();
}

void App::onSnoozed()
{
    m_history.addSnooze(today());
    closeReminders();
}

void App::onManualBreak()
{
    m_history.addBreak(today());
    closeReminders();
    saveHistory();
}

void App::onNaturalBreak(int awaySeconds, bool wasDue)
{
    m_history.addNaturalBreak(today());
    saveHistory();
    if (!wasDue)
        return;
    closeReminders();
    playSound(Notifier::Sound::BreakOver);
    m_notifier->show(QStringLiteral("Welcome back"),
                     QStringLiteral("Your %1 away from the computer counted as a break.")
                         .arg(Format::duration(m_settings.displaySeconds(awaySeconds))),
                     {}, Notifier::Urgency::Low, 6000);
}

void App::onNotificationAction(const QString &key)
{
    if (key == QLatin1String("start"))
        m_scheduler.startBreak();
    else if (key == QLatin1String("snooze"))
        m_scheduler.snooze();
    else if (key == QLatin1String("skip"))
        m_scheduler.skip();
    else if (key == QLatin1String("end"))
        m_scheduler.endBreak();
    else if (key == QLatin1String("default"))
        showMainWindow();
}

void App::closeReminders()
{
    m_notifier->close();
    if (m_breakWindow && m_breakWindow->isVisible() && m_breakWindow->mode() == BreakWindow::Mode::Reminder)
        m_breakWindow->dismiss();
}

void App::playSound(Notifier::Sound sound)
{
    if (m_settings.sound)
        Notifier::play(sound);
}

bool App::useWindow() const
{
    return m_settings.style != Settings::Style::Notification || !m_notifier->serverAvailable();
}

bool App::snoozeAllowed() const
{
    return !m_settings.strict || m_scheduler.snoozeCount() == 0;
}

QString App::snoozeLabel() const
{
    return QStringLiteral("Snooze %1")
        .arg(Format::minutesCeil(m_settings.displaySeconds(m_scheduler.config().snoozeSeconds)));
}

BreakWindow *App::breakWindow()
{
    if (!m_breakWindow) {
        m_breakWindow = new BreakWindow;
        connect(m_breakWindow.data(), &BreakWindow::startBreakClicked, &m_scheduler, &Scheduler::startBreak);
        connect(m_breakWindow.data(), &BreakWindow::snoozeClicked, &m_scheduler, &Scheduler::snooze);
        connect(m_breakWindow.data(), &BreakWindow::skipClicked, &m_scheduler, &Scheduler::skip);
        connect(m_breakWindow.data(), &BreakWindow::endBreakClicked, &m_scheduler, &Scheduler::endBreak);
    }
    return m_breakWindow;
}

// --- Tray ------------------------------------------------------------------------

void App::createTray()
{
    if (m_tray)
        return;

    m_menu = new QMenu;
    m_statusAction = m_menu->addAction(QString());
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();
    m_breakAction = m_menu->addAction(QStringLiteral("Take a break now"), &m_scheduler, &Scheduler::startBreak);
    m_endBreakAction = m_menu->addAction(QStringLiteral("End break"), &m_scheduler, &Scheduler::endBreak);
    m_snoozeAction = m_menu->addAction(QString(), &m_scheduler, &Scheduler::snooze);
    m_skipAction = m_menu->addAction(QStringLiteral("Skip this break"), &m_scheduler, &Scheduler::skip);
    m_resetAction = m_menu->addAction(QStringLiteral("I just took a break"), &m_scheduler, &Scheduler::reset);

    QMenu *pause = m_menu->addMenu(QStringLiteral("Pause reminders"));
    pause->addAction(QStringLiteral("For 30 minutes"), this, [this] { pauseMinutes(30); });
    pause->addAction(QStringLiteral("For 1 hour"), this, [this] { pauseMinutes(60); });
    pause->addAction(QStringLiteral("For 2 hours"), this, [this] { pauseMinutes(120); });
    pause->addAction(QStringLiteral("Until I resume"), this, [this] { pauseMinutes(0); });
    m_pauseMenuAction = pause->menuAction();
    m_resumeAction = m_menu->addAction(QStringLiteral("Resume reminders"), &m_scheduler, &Scheduler::resume);

    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Status and statistics…"), this, &App::showMainWindow);
    m_menu->addAction(QStringLiteral("Settings…"), this, &App::showSettings);
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Quit"), this, &App::quit);
    connect(m_menu, &QMenu::aboutToShow, this, &App::updateMenu);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &App::onTrayActivated);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &App::showMainWindow);
    m_notifier->setTray(m_tray);
    updateTray(true);
    updateMenu();
    m_tray->show();

    if (m_mainWindow)
        m_mainWindow->setCloseQuits(false);
}

void App::updateTray(bool force)
{
    if (!m_tray)
        return;

    using Kind = Icons::TrayLook::Kind;
    Icons::TrayLook look;
    const auto minutesLabel = [](int seconds) {
        const int minutes = (std::max(0, seconds) + 59) / 60;
        return minutes > 99 ? QStringLiteral("%1h").arg(minutes / 60) : QString::number(minutes);
    };
    switch (m_scheduler.state()) {
    case Scheduler::State::Working:
        look.kind = Kind::Progress;
        look.progress = m_scheduler.cycleProgress();
        look.color = look.progress < 0.75 ? Icons::okColor() : Icons::warnColor();
        look.label = minutesLabel(m_settings.displaySeconds(m_scheduler.secondsUntilBreak()));
        break;
    case Scheduler::State::Due:
        look.kind = Kind::Due;
        look.progress = 1.0;
        look.color = Icons::dueColor();
        break;
    case Scheduler::State::OnBreak: {
        const int total = std::max(1, m_scheduler.config().breakSeconds);
        const int remaining = m_scheduler.breakRemainingSeconds();
        look.kind = Kind::Break;
        look.progress = 1.0 - double(remaining) / total;
        look.color = Icons::breakColor();
        look.label = minutesLabel(m_settings.displaySeconds(remaining));
        break;
    }
    case Scheduler::State::Away:
        look.kind = Kind::Away;
        look.color = Icons::idleColor();
        break;
    case Scheduler::State::Paused:
        look.kind = Kind::Paused;
        look.progress = m_scheduler.cycleProgress();
        look.color = Icons::idleColor();
        break;
    }

    const QString key = look.key();
    if (force || key != m_trayKey) {
        m_trayKey = key;
        m_tray->setIcon(Icons::trayIcon(look));
    }
    const QString tooltip = QStringLiteral("Sedentary Reminder\n") + statusText();
    if (tooltip != m_tooltip) {
        m_tooltip = tooltip;
        m_tray->setToolTip(tooltip);
    }
}

void App::updateMenu()
{
    if (!m_menu)
        return;
    const Scheduler::State state = m_scheduler.state();
    m_statusAction->setText(statusText());
    m_breakAction->setVisible(state != Scheduler::State::OnBreak);
    m_endBreakAction->setVisible(state == Scheduler::State::OnBreak);
    m_snoozeAction->setText(snoozeLabel());
    m_snoozeAction->setVisible(state == Scheduler::State::Due && snoozeAllowed());
    m_skipAction->setVisible(state == Scheduler::State::Due && !m_settings.strict);
    m_resetAction->setVisible(state != Scheduler::State::OnBreak && state != Scheduler::State::Away);
    m_pauseMenuAction->setVisible(!m_scheduler.isPaused());
    m_resumeAction->setVisible(m_scheduler.isPaused());
}

void App::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason != QSystemTrayIcon::Trigger)
        return;
    if (m_mainWindow && m_mainWindow->isVisible() && m_mainWindow->isActiveWindow())
        m_mainWindow->hide();
    else
        showMainWindow();
}

// --- Windows ---------------------------------------------------------------------

void App::showMainWindow()
{
    if (!m_mainWindow) {
        m_mainWindow = new MainWindow(&m_scheduler, &m_history, &m_settings, m_idle, m_tray == nullptr);
        connect(m_mainWindow.data(), &MainWindow::settingsRequested, this, &App::showSettings);
        connect(m_mainWindow.data(), &MainWindow::quitRequested, this, &App::quit);
    }
    m_mainWindow->refresh();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void App::showSettings()
{
    if (m_settingsDialog) {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }
    QWidget *parent = m_mainWindow && m_mainWindow->isVisible() ? m_mainWindow.data() : nullptr;
    SettingsDialog dialog(m_settings, parent);
    m_settingsDialog = &dialog;
    if (dialog.exec() != QDialog::Accepted)
        return;

    applySettings(dialog.settings());
    if (dialog.autostart() != Autostart::isEnabled()
        && !Autostart::setEnabled(dialog.autostart(), QCoreApplication::applicationFilePath())) {
        QMessageBox::warning(parent, QStringLiteral("Sedentary Reminder"),
                             QStringLiteral("Could not update %1.").arg(Autostart::entryPath()));
    }
}

void App::applySettings(const Settings &settings)
{
    m_settings = settings;
    if (!m_settings.save())
        qWarning("Could not save settings to %s", qPrintable(Settings::defaultPath()));
    m_scheduler.setConfig(m_settings.schedulerConfig());
    updateTray(true);
    updateMenu();
    if (m_mainWindow && m_mainWindow->isVisible())
        m_mainWindow->refresh();
}

void App::pauseMinutes(int minutes)
{
    m_scheduler.pause(minutes > 0 ? minutes * m_settings.minuteSeconds() : 0);
}

void App::quit()
{
    saveHistory();
    if (m_breakWindow)
        m_breakWindow->dismiss();
    if (m_tray)
        m_tray->hide();
    QCoreApplication::quit();
}

void App::saveHistory()
{
    if (m_history.isDirty() && !m_history.save())
        qWarning("Could not save statistics to %s", qPrintable(m_history.path()));
}

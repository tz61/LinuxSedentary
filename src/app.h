#pragma once

#include "history.h"
#include "scheduler.h"
#include "settings.h"

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>

class BreakWindow;
class IdleMonitor;
class MainWindow;
class Notifier;
class QAction;
class QMenu;
class SettingsDialog;

// Wires the scheduler to the idle monitor, the tray icon, the windows,
// notifications and the statistics.
class App : public QObject
{
    Q_OBJECT

public:
    explicit App(bool fastMode, QObject *parent = nullptr);
    ~App() override;

    void start(bool showWindow);
    Scheduler *scheduler() { return &m_scheduler; }
    // One line, e.g. "Sitting for 32 min · break in 13 min".
    QString statusText() const;

public Q_SLOTS:
    void showMainWindow();
    void showSettings();
    void pauseMinutes(int minutes);
    void quit();

private Q_SLOTS:
    void tick();
    void onStateChanged();
    void onPreWarning(int secondsLeft);
    void onBreakDue(int sittingSeconds, bool repeated);
    void onBreakStarted(int seconds);
    void onBreakFinished(bool completed);
    void onBreakSkipped();
    void onSnoozed();
    void onManualBreak();
    void onNaturalBreak(int awaySeconds, bool wasDue);
    void onNotificationAction(const QString &key);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void saveHistory();

private:
    void createTray();
    void waitForTray();
    void updateTray(bool force = false);
    void updateMenu();
    void applySettings(const Settings &settings);
    void closeReminders();
    bool useWindow() const;
    bool snoozeAllowed() const;
    QString snoozeLabel() const;
    BreakWindow *breakWindow();

    Settings m_settings;
    Scheduler m_scheduler;
    History m_history;
    IdleMonitor *m_idle = nullptr;
    Notifier *m_notifier = nullptr;

    QTimer m_tickTimer;
    QTimer m_saveTimer;
    QTimer m_trayRetry;
    int m_trayRetries = 0;
    Scheduler::State m_prevState = Scheduler::State::Working;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_breakAction = nullptr;
    QAction *m_endBreakAction = nullptr;
    QAction *m_snoozeAction = nullptr;
    QAction *m_skipAction = nullptr;
    QAction *m_resetAction = nullptr;
    QAction *m_pauseMenuAction = nullptr;
    QAction *m_resumeAction = nullptr;
    QString m_trayKey;
    QString m_tooltip;

    QPointer<BreakWindow> m_breakWindow;
    QPointer<MainWindow> m_mainWindow;
    QPointer<SettingsDialog> m_settingsDialog;
};

#pragma once

#include <QWidget>

class History;
class IdleMonitor;
class QLabel;
class QMenu;
class QPushButton;
class Scheduler;
struct Settings;

namespace detail {
class StatusRing;
class WeekChart;
} // namespace detail

// Current status, today's numbers and the last seven days.
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(Scheduler *scheduler, const History *history, const Settings *settings,
               const IdleMonitor *idle, bool closeQuits, QWidget *parent = nullptr);

    void refresh();
    // Without a tray icon, closing the window is the only way to quit.
    void setCloseQuits(bool on) { m_closeQuits = on; }

Q_SIGNALS:
    void settingsRequested();
    void quitRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    Scheduler *m_scheduler;
    const History *m_history;
    const Settings *m_settings;
    const IdleMonitor *m_idle;
    bool m_closeQuits;

    detail::StatusRing *m_ring = nullptr;
    QLabel *m_state = nullptr;
    QLabel *m_detail = nullptr;
    QPushButton *m_breakButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QMenu *m_pauseMenu = nullptr;
    QLabel *m_sittingToday = nullptr;
    QLabel *m_breaksToday = nullptr;
    QLabel *m_longestToday = nullptr;
    QLabel *m_skippedToday = nullptr;
    detail::WeekChart *m_chart = nullptr;
    QLabel *m_footer = nullptr;
};

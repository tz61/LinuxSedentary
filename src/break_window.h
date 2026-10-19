#pragma once

#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QScreen;

// The reminder card: asks the user to stand up, then counts the break down.
// Shown either as a small always-on-top window or over a dimmed screen.
class BreakWindow : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Reminder, Break, Done };

    explicit BreakWindow(QWidget *parent = nullptr);
    ~BreakWindow() override;

    void showReminder(int sittingSeconds, bool fullscreen, bool allowSnooze, const QString &snoozeLabel,
                      bool allowSkip);
    void showBreak(int breakSeconds, bool fullscreen);
    // userActive: keyboard/mouse input in the last moments.
    void updateBreak(int remainingSeconds, bool userActive);
    void showDone(const QString &nextBreak);
    void dismiss();

    Mode mode() const { return m_mode; }

Q_SIGNALS:
    void startBreakClicked();
    void snoozeClicked();
    void skipClicked();
    void endBreakClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void present(bool fullscreen);
    void clearDimmers();
    void showTip(int index);
    void armButtons();
    void onPrimary();

    Mode m_mode = Mode::Reminder;
    bool m_fullscreen = false;
    int m_breakSeconds = 0;
    int m_tip = -1;

    QFrame *m_card = nullptr;
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_message = nullptr;
    QLabel *m_tipTitle = nullptr;
    QLabel *m_tipText = nullptr;
    QPushButton *m_nextTip = nullptr;
    QLabel *m_countdown = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_skip = nullptr;
    QPushButton *m_snooze = nullptr;
    QPushButton *m_primary = nullptr;

    QVector<QPointer<QWidget>> m_dimmers;
    QTimer m_armTimer;
    QTimer m_doneTimer;
};

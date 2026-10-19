#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QSystemTrayIcon;

// Desktop notifications (org.freedesktop.Notifications) with action buttons,
// plus sounds. Keeps a single notification on screen, replacing it as the
// situation changes.
class Notifier : public QObject
{
    Q_OBJECT

public:
    struct Action {
        QString key;
        QString label;
    };
    enum class Urgency { Low = 0, Normal = 1, Critical = 2 };
    enum class Sound { PreWarning, Reminder, BreakStart, BreakOver };

    explicit Notifier(QObject *parent = nullptr);

    // Fallback when no notification server is running.
    void setTray(QSystemTrayIcon *tray);
    bool serverAvailable() const { return m_serverAvailable; }
    bool supportsActions() const { return m_supportsActions; }

    // timeoutMs: -1 = server default, 0 = stays until dismissed.
    void show(const QString &summary, const QString &body, const QVector<Action> &actions = {},
              Urgency urgency = Urgency::Normal, int timeoutMs = -1);
    void close();

    // Plays one of the built-in chimes (data/sounds) without blocking.
    static void play(Sound sound);
    // The chime as a file on disk (players cannot read Qt resources), or
    // empty if it could not be written.
    static QString soundFile(Sound sound);

Q_SIGNALS:
    // "default" when the notification body was clicked.
    void actionInvoked(const QString &key);

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &key);
    void onClosed(uint id, uint reason);

private:
    QPointer<QSystemTrayIcon> m_tray;
    uint m_id = 0;
    bool m_serverAvailable = false;
    bool m_supportsActions = false;
};

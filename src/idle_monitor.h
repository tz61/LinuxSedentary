#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

// Reports how long the session has had no keyboard/mouse input, whether the
// screen is locked, and whether some application inhibits the screensaver.
//
// Backends, in order of preference:
//   X11:     the XScreenSaver extension (libXss, loaded at runtime)
//   GNOME:   org.gnome.Mutter.IdleMonitor
//   Others:  org.freedesktop.ScreenSaver.GetSessionIdleTime (KDE answers in
//            milliseconds although the spec says seconds)
class IdleMonitor : public QObject
{
    Q_OBJECT

public:
    explicit IdleMonitor(QObject *parent = nullptr);
    ~IdleMonitor() override;

    // Seconds since the last input; 0 when no backend works.
    int idleSeconds();
    bool isLocked() const { return m_locked; }
    // Cached for a few seconds: the query is a D-Bus round trip.
    bool isIdleInhibited();
    bool hasIdleSource() const { return m_backend != Backend::None; }
    QString backendName() const;

Q_SIGNALS:
    void lockedChanged(bool locked);

private Q_SLOTS:
    void onScreenSaverActiveChanged(bool active);

private:
    enum class Backend { None, X11, Mutter, FreedesktopMs, FreedesktopSeconds };
    struct X11;

    bool probeX11();
    bool queryMutter(qint64 *ms) const;
    bool queryFreedesktop(qint64 *value) const;
    void watchLockState();

    Backend m_backend = Backend::None;
    std::unique_ptr<X11> m_x11;
    bool m_locked = false;
    bool m_inhibited = false;
    QElapsedTimer m_inhibitCheck;
};

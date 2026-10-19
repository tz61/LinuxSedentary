#include "idle_monitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLibrary>

namespace {

constexpr int kDBusTimeoutMs = 500;
constexpr int kInhibitCacheMs = 10'000;

const QString kFdoService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kFdoPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const QString kFdoKdePath = QStringLiteral("/ScreenSaver");
const QString kGnomeService = QStringLiteral("org.gnome.ScreenSaver");
const QString kGnomePath = QStringLiteral("/org/gnome/ScreenSaver");

// Synchronous call returning the first reply argument, or an invalid QVariant.
QVariant callFirst(const QString &service, const QString &path, const QString &interface,
                   const QString &method, const QVariantList &args = {})
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, interface, method);
    msg.setArguments(args);
    const QDBusMessage reply = bus.call(msg, QDBus::Block, kDBusTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst();
}

bool isWaylandSession()
{
    return qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

bool isKde()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive);
}

} // namespace

// libX11 + libXss resolved at runtime, so neither is a build dependency.
struct IdleMonitor::X11 {
    struct Info { // XScreenSaverInfo
        unsigned long window;
        int state;
        int kind;
        unsigned long tilOrSince;
        unsigned long idle; // milliseconds
        unsigned long eventMask;
    };

    QLibrary libX11;
    QLibrary libXss;
    void *(*openDisplay)(const char *) = nullptr;
    int (*closeDisplay)(void *) = nullptr;
    unsigned long (*defaultRootWindow)(void *) = nullptr;
    int (*xFree)(void *) = nullptr;
    int (*queryExtension)(void *, int *, int *) = nullptr;
    Info *(*allocInfo)() = nullptr;
    int (*queryInfo)(void *, unsigned long, Info *) = nullptr;

    void *display = nullptr;
    unsigned long root = 0;
    Info *info = nullptr;

    template<typename F>
    static bool resolve(QLibrary &lib, const char *name, F *fn)
    {
        *fn = reinterpret_cast<F>(lib.resolve(name));
        return *fn != nullptr;
    }

    bool init()
    {
        libX11.setFileNameAndVersion(QStringLiteral("X11"), 6);
        libXss.setFileNameAndVersion(QStringLiteral("Xss"), 1);
        if (!libX11.load() || !libXss.load())
            return false;
        if (!resolve(libX11, "XOpenDisplay", &openDisplay) || !resolve(libX11, "XCloseDisplay", &closeDisplay)
            || !resolve(libX11, "XDefaultRootWindow", &defaultRootWindow) || !resolve(libX11, "XFree", &xFree)
            || !resolve(libXss, "XScreenSaverQueryExtension", &queryExtension)
            || !resolve(libXss, "XScreenSaverAllocInfo", &allocInfo)
            || !resolve(libXss, "XScreenSaverQueryInfo", &queryInfo))
            return false;

        display = openDisplay(nullptr);
        if (!display)
            return false;
        int eventBase = 0;
        int errorBase = 0;
        if (!queryExtension(display, &eventBase, &errorBase))
            return false;
        root = defaultRootWindow(display);
        info = allocInfo();
        return info != nullptr;
    }

    bool idleMs(qint64 *ms) const
    {
        if (!queryInfo(display, root, info))
            return false;
        *ms = qint64(info->idle);
        return true;
    }

    ~X11()
    {
        if (info)
            xFree(info);
        if (display)
            closeDisplay(display);
    }
};

IdleMonitor::IdleMonitor(QObject *parent)
    : QObject(parent)
{
    qint64 value = 0;
    if (!isWaylandSession() && QGuiApplication::platformName() == QLatin1String("xcb") && probeX11())
        m_backend = Backend::X11;
    else if (queryMutter(&value))
        m_backend = Backend::Mutter;
    else if (queryFreedesktop(&value))
        m_backend = isKde() ? Backend::FreedesktopMs : Backend::FreedesktopSeconds;

    watchLockState();
}

IdleMonitor::~IdleMonitor() = default;

bool IdleMonitor::probeX11()
{
    auto x11 = std::make_unique<X11>();
    qint64 ms = 0;
    if (!x11->init() || !x11->idleMs(&ms))
        return false;
    m_x11 = std::move(x11);
    return true;
}

bool IdleMonitor::queryMutter(qint64 *ms) const
{
    const QVariant v = callFirst(QStringLiteral("org.gnome.Mutter.IdleMonitor"),
                                 QStringLiteral("/org/gnome/Mutter/IdleMonitor/Core"),
                                 QStringLiteral("org.gnome.Mutter.IdleMonitor"),
                                 QStringLiteral("GetIdletime"));
    if (!v.isValid())
        return false;
    *ms = v.toLongLong();
    return true;
}

bool IdleMonitor::queryFreedesktop(qint64 *value) const
{
    const QVariant v = callFirst(kFdoService, kFdoPath, kFdoService, QStringLiteral("GetSessionIdleTime"));
    if (!v.isValid())
        return false;
    *value = v.toLongLong();
    return true;
}

int IdleMonitor::idleSeconds()
{
    qint64 value = 0;
    switch (m_backend) {
    case Backend::X11:
        if (m_x11->idleMs(&value))
            return int(value / 1000);
        break;
    case Backend::Mutter:
        if (queryMutter(&value))
            return int(value / 1000);
        break;
    case Backend::FreedesktopMs:
        if (queryFreedesktop(&value))
            return int(value / 1000);
        break;
    case Backend::FreedesktopSeconds:
        if (queryFreedesktop(&value))
            return int(value);
        break;
    case Backend::None:
        break;
    }
    return 0;
}

bool IdleMonitor::isIdleInhibited()
{
    if (m_inhibitCheck.isValid() && m_inhibitCheck.elapsed() < kInhibitCacheMs)
        return m_inhibited;
    m_inhibitCheck.start();

    // KDE (PowerDevil) and Xfce implement the freedesktop interface.
    QVariant v = callFirst(QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
                           QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
                           QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
                           QStringLiteral("HasInhibit"));
    if (!v.isValid()) {
        // GNOME: flag 8 = "inhibit the session being marked as idle".
        v = callFirst(QStringLiteral("org.gnome.SessionManager"), QStringLiteral("/org/gnome/SessionManager"),
                      QStringLiteral("org.gnome.SessionManager"), QStringLiteral("IsInhibited"),
                      {QVariant::fromValue(uint(8))});
    }
    m_inhibited = v.isValid() && v.toBool();
    return m_inhibited;
}

QString IdleMonitor::backendName() const
{
    switch (m_backend) {
    case Backend::X11:
        return QStringLiteral("X11 screensaver extension");
    case Backend::Mutter:
        return QStringLiteral("GNOME Mutter idle monitor");
    case Backend::FreedesktopMs:
    case Backend::FreedesktopSeconds:
        return QStringLiteral("org.freedesktop.ScreenSaver");
    case Backend::None:
        break;
    }
    return QStringLiteral("none (idle time unavailable)");
}

void IdleMonitor::watchLockState()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    const char *slot = SLOT(onScreenSaverActiveChanged(bool));
    const QString signal = QStringLiteral("ActiveChanged");
    bus.connect(kFdoService, kFdoPath, kFdoService, signal, this, slot);
    if (isKde())
        bus.connect(kFdoService, kFdoKdePath, kFdoService, signal, this, slot);
    bus.connect(kGnomeService, kGnomePath, kGnomeService, signal, this, slot);

    QVariant active = callFirst(kFdoService, kFdoPath, kFdoService, QStringLiteral("GetActive"));
    if (!active.isValid())
        active = callFirst(kGnomeService, kGnomePath, kGnomeService, QStringLiteral("GetActive"));
    m_locked = active.isValid() && active.toBool();
}

void IdleMonitor::onScreenSaverActiveChanged(bool active)
{
    if (m_locked == active)
        return;
    m_locked = active;
    Q_EMIT lockedChanged(active);
}

#include "notifier.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace {

const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

QString soundFile(const QString &name)
{
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : dirs) {
        for (const char *ext : {".oga", ".ogg", ".wav"}) {
            const QString path = dir + QStringLiteral("/sounds/freedesktop/stereo/") + name + QLatin1String(ext);
            if (QFileInfo::exists(path))
                return path;
        }
    }
    return {};
}

} // namespace

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    m_serverAvailable = bus.interface()->isServiceRegistered(kService);
    if (!m_serverAvailable) {
        // Activatable servers (e.g. dunst, mako) start on the first call.
        m_serverAvailable = bus.interface()->activatableServiceNames().value().contains(kService);
    }
    if (!m_serverAvailable)
        return;

    const QDBusMessage reply = bus.call(
        QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("GetCapabilities")),
        QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        m_supportsActions = reply.arguments().constFirst().toStringList().contains(QLatin1String("actions"));

    bus.connect(kService, kPath, kService, QStringLiteral("ActionInvoked"), this,
                SLOT(onActionInvoked(uint, QString)));
    bus.connect(kService, kPath, kService, QStringLiteral("NotificationClosed"), this,
                SLOT(onClosed(uint, uint)));
}

void Notifier::show(const QString &summary, const QString &body, const QVector<Action> &actions,
                    Urgency urgency, int timeoutMs)
{
    if (!m_serverAvailable) {
        if (m_tray && m_tray->isVisible()) {
            const auto icon = urgency == Urgency::Critical ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information;
            m_tray->showMessage(summary, body, icon, timeoutMs > 0 ? timeoutMs : 10'000);
        }
        return;
    }

    QStringList actionList;
    for (const Action &a : actions)
        actionList << a.key << a.label;

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral(APP_ID));
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue(uchar(urgency)));
    // We play our own sounds.
    hints.insert(QStringLiteral("suppress-sound"), true);

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("Notify"));
    msg.setArguments({QStringLiteral("Sedentary Reminder"), m_id, QStringLiteral(APP_ID), summary, body,
                      actionList, hints, timeoutMs});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        m_id = reply.arguments().constFirst().toUInt();
}

void Notifier::close()
{
    if (m_id == 0)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("CloseNotification"));
    msg.setArguments({m_id});
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
    m_id = 0;
}

void Notifier::onActionInvoked(uint id, const QString &key)
{
    if (id != 0 && id == m_id)
        Q_EMIT actionInvoked(key);
}

void Notifier::onClosed(uint id, uint)
{
    if (id == m_id)
        m_id = 0;
}

void Notifier::play(Sound sound)
{
    const QString name = sound == Sound::Reminder ? QStringLiteral("bell") : QStringLiteral("complete");
    const QString file = soundFile(name);
    if (!file.isEmpty()) {
        for (const char *player : {"pw-play", "paplay", "ogg123"}) {
            const QString exe = QStandardPaths::findExecutable(QLatin1String(player));
            if (!exe.isEmpty() && QProcess::startDetached(exe, {file}))
                return;
        }
    }
    const QString canberra = QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play"));
    if (!canberra.isEmpty() && QProcess::startDetached(canberra, {QStringLiteral("-i"), name}))
        return;
    QApplication::beep();
}

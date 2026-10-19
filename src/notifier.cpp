#include "notifier.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace {

const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

const char *soundName(Notifier::Sound sound)
{
    switch (sound) {
    case Notifier::Sound::PreWarning:
        return "prewarn";
    case Notifier::Sound::Reminder:
        return "reminder";
    case Notifier::Sound::BreakStart:
        return "break-start";
    case Notifier::Sound::BreakOver:
        break;
    }
    return "break-over";
}

struct Player {
    const char *program;
    QStringList args;
};

// paplay first: it works with PulseAudio and with PipeWire's Pulse layer,
// whereas pw-play fails on a plain PulseAudio system.
const QVector<Player> &players()
{
    static const QVector<Player> list = {
        {"paplay", {}},
        {"pw-play", {}},
        {"aplay", {QStringLiteral("-q")}},
        {"canberra-gtk-play", {QStringLiteral("-f")}},
    };
    return list;
}

// Tries the players in turn until one exits successfully.
void playWith(const QString &file, int index)
{
    for (; index < players().size(); ++index) {
        const QString exe = QStandardPaths::findExecutable(QLatin1String(players().at(index).program));
        if (exe.isEmpty())
            continue;
        auto *process = new QProcess(qApp);
        process->setStandardOutputFile(QProcess::nullDevice());
        process->setStandardErrorFile(QProcess::nullDevice());
        QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), process,
                         [process, file, index](int code, QProcess::ExitStatus status) {
                             process->deleteLater();
                             if (status != QProcess::NormalExit || code != 0)
                                 playWith(file, index + 1);
                         });
        QObject::connect(process, &QProcess::errorOccurred, process, [process, file, index](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                process->deleteLater();
                playWith(file, index + 1);
            }
        });
        process->start(exe, players().at(index).args + QStringList{file});
        return;
    }
    QApplication::beep();
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

void Notifier::setTray(QSystemTrayIcon *tray)
{
    m_tray = tray;
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

QString Notifier::soundFile(Sound sound)
{
    const QString name = QLatin1String(soundName(sound));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/" APP_ID "/sounds");
    const QString path = dir + QLatin1Char('/') + name + QStringLiteral(".wav");

    // Checked once per run; rewritten when missing or from another version.
    static QSet<QString> verified;
    if (verified.contains(name) && QFileInfo::exists(path))
        return path;

    QFile resource(QStringLiteral(":/sounds/") + name + QStringLiteral(".wav"));
    if (!resource.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = resource.readAll();
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != data) {
        existing.close();
        QDir().mkpath(dir);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
            return {};
    }
    verified.insert(name);
    return path;
}

void Notifier::play(Sound sound)
{
    const QString file = soundFile(sound);
    if (file.isEmpty())
        QApplication::beep();
    else
        playWith(file, 0);
}

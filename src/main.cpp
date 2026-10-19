#include "app.h"
#include "autostart.h"
#include "control_service.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QTimer>

#include <cstdio>
#include <cstring>

namespace {

// Options that only talk to the running instance (or edit a file) and must
// work without a display.
const char *const kCoreOnlyOptions[] = {"--status", "--quit", "--reset", "--resume", "--autostart",
                                        "--help", "-h", "--version", "-v"};

bool needsGui(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        for (const char *option : kCoreOnlyOptions) {
            const size_t n = std::strlen(option);
            if (std::strncmp(argv[i], option, n) == 0 && (argv[i][n] == '\0' || argv[i][n] == '='))
                return false;
        }
    }
    return true;
}

struct Options {
    QCommandLineOption hidden{QStringLiteral("hidden"), QStringLiteral("Start in the system tray without opening the window.")};
    QCommandLineOption show{QStringLiteral("show"), QStringLiteral("Open the status window.")};
    QCommandLineOption settings{QStringLiteral("settings"), QStringLiteral("Open the settings.")};
    QCommandLineOption takeBreak{QStringLiteral("take-break"), QStringLiteral("Start a break now.")};
    QCommandLineOption reset{QStringLiteral("reset"), QStringLiteral("Record that you just took a break.")};
    QCommandLineOption pause{QStringLiteral("pause"), QStringLiteral("Pause reminders for <minutes> (0 = until resumed)."),
                             QStringLiteral("minutes")};
    QCommandLineOption resume{QStringLiteral("resume"), QStringLiteral("Resume reminders.")};
    QCommandLineOption status{QStringLiteral("status"), QStringLiteral("Print the current status.")};
    QCommandLineOption quit{QStringLiteral("quit"), QStringLiteral("Quit the running instance.")};
    QCommandLineOption autostart{QStringLiteral("autostart"), QStringLiteral("Start at login: on or off."),
                                 QStringLiteral("on|off")};
    QCommandLineOption fast{QStringLiteral("fast"), QStringLiteral("Demo mode: every minute lasts one second.")};

    void addTo(QCommandLineParser &parser) const
    {
        parser.setApplicationDescription(QStringLiteral("Reminds you to stand up and move after sitting too long."));
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOptions({hidden, show, settings, takeBreak, reset, pause, resume, status, quit, autostart, fast});
    }
};

void setIdentity()
{
    QCoreApplication::setApplicationName(QStringLiteral(APP_ID));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral(APP_ID));
}

// Calls a method on the running instance. Returns false if none runs.
bool callInstance(const QString &method, const QVariantList &args = {}, QString *result = nullptr)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(ControlService::serviceName, ControlService::objectPath,
                                                      ControlService::interfaceName, method);
    msg.setArguments(args);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;
    if (result && !reply.arguments().isEmpty())
        *result = reply.arguments().constFirst().toString();
    return true;
}

bool instanceRunning()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface()->isServiceRegistered(ControlService::serviceName);
}

int runCommand(const QCommandLineParser &parser, const Options &o)
{
    if (parser.isSet(o.autostart)) {
        const QString value = parser.value(o.autostart).toLower();
        const bool on = value == QLatin1String("on") || value == QLatin1String("true") || value == QLatin1String("1");
        if (!on && value != QLatin1String("off") && value != QLatin1String("false") && value != QLatin1String("0")) {
            std::fprintf(stderr, "--autostart expects on or off\n");
            return 2;
        }
        if (!Autostart::setEnabled(on, QCoreApplication::applicationFilePath())) {
            std::fprintf(stderr, "Could not update %s\n", qPrintable(Autostart::entryPath()));
            return 1;
        }
        std::printf("Start at login: %s\n", on ? "on" : "off");
        return 0;
    }

    const bool running = instanceRunning();
    if (parser.isSet(o.quit)) {
        if (running)
            callInstance(QStringLiteral("Quit"));
        return 0;
    }
    if (!running) {
        std::fprintf(stderr, "Sedentary Reminder is not running.\n");
        return 1;
    }
    QString status;
    if (parser.isSet(o.reset))
        callInstance(QStringLiteral("Reset"));
    if (parser.isSet(o.resume))
        callInstance(QStringLiteral("Resume"));
    if (parser.isSet(o.status) && callInstance(QStringLiteral("Status"), {}, &status))
        std::printf("%s\n", qPrintable(status));
    return 0;
}

// Forwards a GUI request to the instance that is already running.
void forward(const QCommandLineParser &parser, const Options &o)
{
    bool acted = false;
    if (parser.isSet(o.takeBreak))
        acted = callInstance(QStringLiteral("TakeBreak"));
    if (parser.isSet(o.pause))
        acted = callInstance(QStringLiteral("Pause"), {parser.value(o.pause).toInt()});
    if (parser.isSet(o.settings))
        acted = callInstance(QStringLiteral("ShowSettings"));
    // Starting it again from the menu brings up the window.
    if (parser.isSet(o.show) || (!acted && !parser.isSet(o.hidden)))
        callInstance(QStringLiteral("Show"));
}

} // namespace

int main(int argc, char **argv)
{
    const Options options;

    if (!needsGui(argc, argv)) {
        QCoreApplication app(argc, argv);
        setIdentity();
        QCommandLineParser parser;
        options.addTo(parser);
        parser.process(app);
        return runCommand(parser, options);
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    setIdentity();
    QApplication::setApplicationDisplayName(QStringLiteral("Sedentary Reminder"));
    QApplication::setDesktopFileName(QStringLiteral(APP_ID));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    options.addTo(parser);
    parser.process(app);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(ControlService::serviceName)) {
        forward(parser, options);
        return 0;
    }
    if (!bus.isConnected())
        qWarning("No D-Bus session bus: notifications, lock detection and single-instance mode are unavailable.");

    App sedentary(parser.isSet(options.fast));
    ControlService service(&sedentary);
    if (bus.isConnected())
        bus.registerObject(ControlService::objectPath, &service, QDBusConnection::ExportScriptableSlots);

    const bool action = parser.isSet(options.takeBreak) || parser.isSet(options.pause) || parser.isSet(options.settings);
    sedentary.start(parser.isSet(options.show) || (!parser.isSet(options.hidden) && !action));
    if (parser.isSet(options.takeBreak))
        sedentary.scheduler()->startBreak();
    if (parser.isSet(options.pause))
        sedentary.pauseMinutes(parser.value(options.pause).toInt());
    if (parser.isSet(options.settings))
        QTimer::singleShot(0, &sedentary, &App::showSettings);

    return app.exec();
}

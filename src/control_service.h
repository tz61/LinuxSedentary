#pragma once

#include <QObject>
#include <QString>

class App;

// D-Bus interface of the running instance. Keeps the app single-instance
// and lets the command line (and the launcher's actions) control it:
//   sedentary-reminder --status | --take-break | --pause 30 | ...
class ControlService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sedentary.Reminder")

public:
    static const QString serviceName;
    static const QString objectPath;
    static const QString interfaceName;

    explicit ControlService(App *app);

public Q_SLOTS:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void ShowSettings();
    Q_SCRIPTABLE void TakeBreak();
    Q_SCRIPTABLE void Reset();
    Q_SCRIPTABLE void Pause(int minutes);
    Q_SCRIPTABLE void Resume();
    Q_SCRIPTABLE void Quit();
    Q_SCRIPTABLE QString Status();

private:
    App *m_app;
};

#include "control_service.h"

#include "app.h"

#include <QTimer>

const QString ControlService::serviceName = QStringLiteral("org.sedentary.Reminder");
const QString ControlService::objectPath = QStringLiteral("/Reminder");
const QString ControlService::interfaceName = QStringLiteral("org.sedentary.Reminder");

ControlService::ControlService(App *app)
    : QObject(app)
    , m_app(app)
{
}

void ControlService::Show()
{
    m_app->showMainWindow();
}

void ControlService::ShowSettings()
{
    // Reply first: the dialog runs a nested event loop.
    QTimer::singleShot(0, m_app, &App::showSettings);
}

void ControlService::TakeBreak()
{
    m_app->scheduler()->startBreak();
}

void ControlService::Reset()
{
    m_app->scheduler()->reset();
}

void ControlService::Pause(int minutes)
{
    m_app->pauseMinutes(minutes);
}

void ControlService::Resume()
{
    m_app->scheduler()->resume();
}

void ControlService::Quit()
{
    QTimer::singleShot(0, m_app, &App::quit);
}

QString ControlService::Status()
{
    return m_app->statusText();
}

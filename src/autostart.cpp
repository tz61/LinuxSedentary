#include "autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace Autostart {

QString entryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/" APP_ID ".desktop");
}

bool isEnabled()
{
    QFile file(entryPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    // Desktops let users switch an entry off without deleting it.
    const QByteArray content = file.readAll();
    return !content.contains("\nHidden=true") && !content.contains("X-GNOME-Autostart-enabled=false");
}

bool setEnabled(bool enabled, const QString &executable)
{
    const QString path = entryPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    QString exec = executable;
    exec.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));

    const QString entry = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Sedentary Reminder\n"
        "Comment=Reminds you to stand up and move\n"
        "Exec=\"%1\" --hidden\n"
        "TryExec=%2\n"
        "Icon=" APP_ID "\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-GNOME-Autostart-Delay=5\n"
        "X-KDE-autostart-phase=2\n")
        .arg(exec, executable);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(entry.toUtf8());
    return file.commit();
}

} // namespace Autostart

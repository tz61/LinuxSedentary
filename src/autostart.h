#pragma once

#include <QString>

// Start-at-login via the XDG autostart spec (~/.config/autostart/*.desktop),
// honoured by KDE, GNOME, Xfce, Cinnamon, MATE, LXQt and others.
namespace Autostart {

QString entryPath();
bool isEnabled();
// executable: absolute path of the binary to start.
bool setEnabled(bool enabled, const QString &executable);

} // namespace Autostart

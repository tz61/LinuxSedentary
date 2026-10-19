#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QPalette;
class QWidget;

namespace Icons {

struct TrayLook {
    enum class Kind { Progress, Due, Break, Paused, Away };
    Kind kind = Kind::Progress;
    double progress = 0.0;   // ring fill, 0..1
    QString label;           // text in the middle, e.g. minutes left
    QColor color;

    // Identifies the rendered image; the icon is only rebuilt when it changes.
    QString key() const;
};

QIcon appIcon();
QIcon trayIcon(const TrayLook &look);

// Shared palette.
QColor okColor();
QColor warnColor();
QColor dueColor();
QColor breakColor();
QColor idleColor();

// Secondary text: the text colour blended towards the background.
// (QPalette::PlaceholderText renders invisible in some Qt 5 labels.)
QColor mutedText(const QPalette &palette);
void setMuted(QWidget *widget);

} // namespace Icons

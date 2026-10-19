#include "icons.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QWidget>

#include <algorithm>

namespace Icons {

QColor okColor() { return QColor(0x2f, 0xb3, 0x6b); }
QColor warnColor() { return QColor(0xf0, 0xa0, 0x30); }
QColor dueColor() { return QColor(0xe5, 0x4b, 0x3c); }
QColor breakColor() { return QColor(0x2d, 0x9c, 0xdb); }
QColor idleColor() { return QColor(0x8a, 0x92, 0x9c); }

namespace {

QColor blend(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

constexpr qreal kMuted = 0.42;

} // namespace

QColor mutedText(const QPalette &palette)
{
    return blend(palette.color(QPalette::WindowText), palette.color(QPalette::Window), kMuted);
}

void setMuted(QWidget *widget)
{
    QPalette p = widget->palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::WindowText,
                   blend(p.color(group, QPalette::WindowText), p.color(group, QPalette::Window), kMuted));
    }
    widget->setPalette(p);
}

QString TrayLook::key() const
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(int(kind))
        .arg(int(progress * 64))
        .arg(label, color.name());
}

QIcon appIcon()
{
    return QIcon::fromTheme(QStringLiteral(APP_ID), QIcon(QStringLiteral(":/icons/" APP_ID ".svg")));
}

namespace {

void drawCentredText(QPainter &p, const QRectF &box, const QString &text)
{
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(std::max(6, int(box.height() * 0.86)));
    // Shrink until it fits the disc.
    while (font.pixelSize() > 6 && QFontMetricsF(font).horizontalAdvance(text) > box.width())
        font.setPixelSize(font.pixelSize() - 1);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, text);
}

QPixmap render(const TrayLook &look, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Small icons get a relatively thinner ring so the label stays legible.
    const qreal ring = std::max<qreal>(1.5, size <= 24 ? size / 9.0 : size / 7.5);
    const QRectF outer = QRectF(0, 0, size, size).adjusted(ring / 2, ring / 2, -ring / 2, -ring / 2);
    const QRectF inner = outer.adjusted(ring * 0.75, ring * 0.75, -ring * 0.75, -ring * 0.75);

    // Track + progress arc, clockwise from twelve o'clock.
    QColor track = look.color;
    track.setAlphaF(0.30);
    p.setPen(QPen(track, ring, Qt::SolidLine, Qt::FlatCap));
    p.drawEllipse(outer);
    if (look.progress > 0) {
        p.setPen(QPen(look.color, ring, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(outer, 90 * 16, -int(std::clamp(look.progress, 0.0, 1.0) * 360 * 16));
    }

    // A dark disc keeps the label readable on light and dark panels alike.
    const bool alert = look.kind == TrayLook::Kind::Due;
    p.setPen(Qt::NoPen);
    p.setBrush(alert ? look.color : QColor(0x26, 0x2a, 0x2f, 235));
    p.drawEllipse(inner);

    const QRectF textBox = inner.adjusted(inner.width() * 0.06, inner.height() * 0.08,
                                          -inner.width() * 0.06, -inner.height() * 0.08);
    switch (look.kind) {
    case TrayLook::Kind::Paused: {
        const qreal w = inner.width() * 0.14;
        const qreal h = inner.height() * 0.46;
        const QPointF c = inner.center();
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(c.x() - w * 1.6, c.y() - h / 2, w, h), w / 3, w / 3);
        p.drawRoundedRect(QRectF(c.x() + w * 0.6, c.y() - h / 2, w, h), w / 3, w / 3);
        break;
    }
    case TrayLook::Kind::Away:
        drawCentredText(p, textBox, QStringLiteral("z"));
        break;
    case TrayLook::Kind::Due:
        drawCentredText(p, textBox, look.label.isEmpty() ? QStringLiteral("!") : look.label);
        break;
    case TrayLook::Kind::Progress:
    case TrayLook::Kind::Break:
        drawCentredText(p, textBox, look.label);
        break;
    }
    return pm;
}

} // namespace

QIcon trayIcon(const TrayLook &look)
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 44, 48, 64})
        icon.addPixmap(render(look, size));
    return icon;
}

} // namespace Icons

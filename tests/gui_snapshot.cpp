// Renders the windows and tray icons in every state to PNG files.
// Run offscreen: QT_QPA_PLATFORM=offscreen gui_snapshot <output dir>

#include "break_window.h"
#include "history.h"
#include "icons.h"
#include "idle_monitor.h"
#include "main_window.h"
#include "notifier.h"
#include "scheduler.h"
#include "settings.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

QString g_outDir;
int g_failures = 0;

void save(const QImage &image, const QString &name)
{
    const QString path = g_outDir + QLatin1Char('/') + name + QStringLiteral(".png");
    if (image.isNull() || !image.save(path)) {
        std::fprintf(stderr, "FAIL %s\n", qPrintable(path));
        ++g_failures;
    } else {
        std::printf("wrote %s\n", qPrintable(path));
    }
}

// Composites a (possibly translucent) window onto a desktop-like backdrop.
void saveWidget(QWidget *w, const QString &name)
{
    QApplication::processEvents();
    const QPixmap pm = w->grab();
    QImage canvas(pm.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(0x5b, 0x6b, 0x7d));
    QPainter p(&canvas);
    p.drawPixmap(0, 0, pm);
    p.end();
    save(canvas, name);
}

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void trayStrip(const QString &name)
{
    using Kind = Icons::TrayLook::Kind;
    const QVector<Icons::TrayLook> looks = {
        {Kind::Progress, 0.15, QStringLiteral("38"), Icons::okColor()},
        {Kind::Progress, 0.85, QStringLiteral("7"), Icons::warnColor()},
        {Kind::Due, 1.0, QString(), Icons::dueColor()},
        {Kind::Break, 0.4, QStringLiteral("3"), Icons::breakColor()},
        {Kind::Paused, 0.5, QString(), Icons::idleColor()},
        {Kind::Away, 0.0, QString(), Icons::idleColor()},
        {Kind::Progress, 0.02, QStringLiteral("2h"), Icons::okColor()},
    };
    const QVector<int> sizes = {16, 22, 32, 64};
    const int cell = 72;
    for (const QColor &panel : {QColor(0xef, 0xf0, 0xf1), QColor(0x23, 0x26, 0x29)}) {
        QImage strip(cell * looks.size(), cell * sizes.size(), QImage::Format_ARGB32);
        strip.fill(panel);
        QPainter p(&strip);
        for (int i = 0; i < looks.size(); ++i) {
            const QIcon icon = Icons::trayIcon(looks.at(i));
            for (int j = 0; j < sizes.size(); ++j) {
                const int s = sizes.at(j);
                p.drawPixmap(i * cell + (cell - s) / 2, j * cell + (cell - s) / 2, icon.pixmap(s, s));
            }
        }
        p.end();
        save(strip, name + (panel.lightness() > 128 ? QStringLiteral("-on-light-panel") : QStringLiteral("-on-dark-panel")));
    }
}

void fillHistory(History &h)
{
    const QDate today = QDate::currentDate();
    const int sitting[] = {5 * 3600, 6 * 3600 + 1200, 2 * 3600, 0, 7 * 3600 + 300, 4 * 3600, 3 * 3600 + 900};
    for (int i = 0; i < 7; ++i) {
        const QDate d = today.addDays(i - 6);
        h.addSitting(d, qint64(sitting[i]) * 1000);
        for (int b = 0; b < (sitting[i] / 3600); ++b)
            h.addBreak(d);
        h.addNaturalBreak(d);
        h.recordStretch(d, 50 * 60);
    }
    h.addSkip(today);
    h.addSnooze(today);
}

void tickActive(Scheduler &s, qint64 &now, int seconds)
{
    for (int i = 0; i < seconds; ++i) {
        now += 1000;
        s.tick(now, 0, false);
    }
}

void snapshots(const QString &theme)
{
    trayStrip(QStringLiteral("tray-") + theme);

    BreakWindow breakWindow;
    breakWindow.showReminder(47 * 60, false, true, QStringLiteral("Snooze 5 min"), true);
    waitMs(1000); // buttons ignore input for a moment after appearing
    saveWidget(&breakWindow, QStringLiteral("break-reminder-") + theme);
    breakWindow.showBreak(5 * 60, false);
    breakWindow.updateBreak(3 * 60 + 12, true);
    saveWidget(&breakWindow, QStringLiteral("break-countdown-") + theme);
    const int breakHeight = breakWindow.height();
    breakWindow.showDone(QStringLiteral("45 min"));
    saveWidget(&breakWindow, QStringLiteral("break-done-") + theme);
    if (breakWindow.height() >= breakHeight) {
        std::fprintf(stderr, "FAIL the done card did not shrink (%d >= %d)\n", breakWindow.height(), breakHeight);
        ++g_failures;
    }
    breakWindow.dismiss();

    QTemporaryDir dir;
    History history(dir.filePath(QStringLiteral("history.json")));
    fillHistory(history);
    Settings settings;
    Scheduler scheduler;
    scheduler.setConfig(settings.schedulerConfig());
    IdleMonitor idle;
    qint64 now = 1'700'000'000'000;
    scheduler.tick(now, 0, false);
    tickActive(scheduler, now, 32 * 60);

    MainWindow main(&scheduler, &history, &settings, &idle, false);
    main.show();
    saveWidget(&main, QStringLiteral("main-working-") + theme);
    tickActive(scheduler, now, 16 * 60);
    main.refresh();
    saveWidget(&main, QStringLiteral("main-due-") + theme);
    scheduler.startBreak();
    tickActive(scheduler, now, 100);
    main.refresh();
    saveWidget(&main, QStringLiteral("main-break-") + theme);
    scheduler.endBreak();
    scheduler.pause(25 * 60);
    main.refresh();
    saveWidget(&main, QStringLiteral("main-paused-") + theme);

    SettingsDialog dialog(settings);
    dialog.show();
    saveWidget(&dialog, QStringLiteral("settings-") + theme);
}

void checkSounds()
{
    for (Notifier::Sound sound : {Notifier::Sound::PreWarning, Notifier::Sound::Reminder,
                                  Notifier::Sound::BreakStart, Notifier::Sound::BreakOver}) {
        const QString path = Notifier::soundFile(sound);
        QFile file(path);
        const QByteArray header = file.open(QIODevice::ReadOnly) ? file.read(12) : QByteArray();
        if (!header.startsWith("RIFF") || header.mid(8, 4) != "WAVE" || file.size() < 10'000) {
            std::fprintf(stderr, "FAIL sound %d not usable at '%s'\n", int(sound), qPrintable(path));
            ++g_failures;
        } else {
            std::printf("sound ok %s\n", qPrintable(path));
        }
    }
}

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2a, 0x2e, 0x32));
    p.setColor(QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::Base, QColor(0x1b, 0x1e, 0x20));
    p.setColor(QPalette::AlternateBase, QColor(0x23, 0x26, 0x29));
    p.setColor(QPalette::Text, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::Button, QColor(0x31, 0x36, 0x3b));
    p.setColor(QPalette::ButtonText, QColor(0xfc, 0xfc, 0xfc));
    p.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    p.setColor(QPalette::Mid, QColor(0x5a, 0x60, 0x66));
    p.setColor(QPalette::PlaceholderText, QColor(0xa1, 0xa9, 0xb1));
    return p;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    g_outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("snapshots");
    QDir().mkpath(g_outDir);

    checkSounds();
    snapshots(QStringLiteral("light"));
    QApplication::setPalette(darkPalette());
    snapshots(QStringLiteral("dark"));

    return g_failures == 0 ? 0 : 1;
}

#include "settings_dialog.h"

#include "autostart.h"
#include "icons.h"
#include "notifier.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QSpinBox *minutesBox(int min, int max, QWidget *parent, const QString &zeroText = QString())
{
    auto *box = new QSpinBox(parent);
    box->setRange(min, max);
    box->setSuffix(QStringLiteral(" min"));
    if (!zeroText.isEmpty())
        box->setSpecialValueText(zeroText);
    return box;
}

} // namespace

SettingsDialog::SettingsDialog(const Settings &settings, QWidget *parent)
    : QDialog(parent)
    , m_base(settings)
{
    setWindowTitle(QStringLiteral("Sedentary Reminder Settings"));
    setWindowIcon(Icons::appIcon());

    auto *layout = new QVBoxLayout(this);

    auto *timing = new QGroupBox(QStringLiteral("Timing"), this);
    auto *timingForm = new QFormLayout(timing);
    m_work = minutesBox(1, 600, timing);
    m_break = minutesBox(1, 120, timing);
    m_snooze = minutesBox(1, 120, timing);
    m_away = minutesBox(1, 120, timing);
    m_away->setToolTip(QStringLiteral("No keyboard or mouse input (or a locked screen) for this long "
                                      "means you got up, and the sitting timer starts over."));
    m_nag = minutesBox(0, 120, timing, QStringLiteral("Never"));
    m_preWarn = new QCheckBox(QStringLiteral("Warn me one minute before a break"), timing);
    timingForm->addRow(QStringLiteral("Remind me after sitting for"), m_work);
    timingForm->addRow(QStringLiteral("Break length"), m_break);
    timingForm->addRow(QStringLiteral("Snooze for"), m_snooze);
    timingForm->addRow(QStringLiteral("Count as a break when away for"), m_away);
    timingForm->addRow(QStringLiteral("Repeat an ignored reminder every"), m_nag);
    timingForm->addRow(QString(), m_preWarn);
    layout->addWidget(timing);

    auto *reminder = new QGroupBox(QStringLiteral("Reminder"), this);
    auto *reminderForm = new QFormLayout(reminder);
    m_style = new QComboBox(reminder);
    m_style->addItem(QStringLiteral("Notification only"), int(Settings::Style::Notification));
    m_style->addItem(QStringLiteral("Window with exercise tip"), int(Settings::Style::Window));
    m_style->addItem(QStringLiteral("Dim the screen"), int(Settings::Style::Fullscreen));
    m_sound = new QCheckBox(QStringLiteral("Play sounds"), reminder);
    m_sound->setToolTip(QStringLiteral("Chimes for the early warning, the reminder, and the start and end of a break."));
    auto *preview = new QPushButton(QStringLiteral("Listen"), reminder);
    preview->setToolTip(QStringLiteral("Play each sound once"));
    connect(preview, &QPushButton::clicked, this, [] {
        // Spaced out so they do not overlap.
        const Notifier::Sound sounds[] = {Notifier::Sound::PreWarning, Notifier::Sound::Reminder,
                                          Notifier::Sound::BreakStart, Notifier::Sound::BreakOver};
        int delay = 0;
        for (Notifier::Sound sound : sounds) {
            QTimer::singleShot(delay, [sound] { Notifier::play(sound); });
            delay += 1800;
        }
    });
    auto *soundRow = new QHBoxLayout;
    soundRow->addWidget(m_sound);
    soundRow->addStretch();
    soundRow->addWidget(preview);
    m_strict = new QCheckBox(QStringLiteral("Strict mode: no skipping, one snooze per break"), reminder);
    reminderForm->addRow(QStringLiteral("Style"), m_style);
    reminderForm->addRow(QString(), soundRow);
    reminderForm->addRow(QString(), m_strict);
    layout->addWidget(reminder);

    auto *general = new QGroupBox(QStringLiteral("General"), this);
    auto *generalLayout = new QVBoxLayout(general);
    m_inhibit = new QCheckBox(QStringLiteral("Idle time is not a break while an app blocks the screensaver"), general);
    m_inhibit->setToolTip(QStringLiteral("Useful while watching videos. Some apps block the screensaver all the "
                                         "time, which would stop breaks from being detected."));
    m_autostart = new QCheckBox(QStringLiteral("Start automatically when I log in"), general);
    generalLayout->addWidget(m_inhibit);
    generalLayout->addWidget(m_autostart);
    layout->addWidget(general);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        Settings defaults;
        defaults.fastMode = m_base.fastMode;
        load(defaults);
    });
    layout->addWidget(buttons);

    load(settings);
    m_autostart->setChecked(Autostart::isEnabled());
}

void SettingsDialog::load(const Settings &s)
{
    m_work->setValue(s.workMinutes);
    m_break->setValue(s.breakMinutes);
    m_snooze->setValue(s.snoozeMinutes);
    m_away->setValue(s.awayMinutes);
    m_nag->setValue(s.nagMinutes);
    m_preWarn->setChecked(s.preWarn);
    m_style->setCurrentIndex(m_style->findData(int(s.style)));
    m_sound->setChecked(s.sound);
    m_strict->setChecked(s.strict);
    m_inhibit->setChecked(s.respectInhibit);
}

Settings SettingsDialog::settings() const
{
    Settings s = m_base;
    s.workMinutes = m_work->value();
    s.breakMinutes = m_break->value();
    s.snoozeMinutes = m_snooze->value();
    s.awayMinutes = m_away->value();
    s.nagMinutes = m_nag->value();
    s.preWarn = m_preWarn->isChecked();
    s.style = Settings::Style(m_style->currentData().toInt());
    s.sound = m_sound->isChecked();
    s.strict = m_strict->isChecked();
    s.respectInhibit = m_inhibit->isChecked();
    return s;
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}

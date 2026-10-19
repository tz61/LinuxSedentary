#pragma once

#include "settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings &settings, QWidget *parent = nullptr);

    // The edited settings; fastMode is carried over unchanged.
    Settings settings() const;
    bool autostart() const;

private:
    void load(const Settings &settings);

    Settings m_base;
    QSpinBox *m_work = nullptr;
    QSpinBox *m_break = nullptr;
    QSpinBox *m_snooze = nullptr;
    QSpinBox *m_away = nullptr;
    QSpinBox *m_nag = nullptr;
    QCheckBox *m_preWarn = nullptr;
    QComboBox *m_style = nullptr;
    QCheckBox *m_sound = nullptr;
    QCheckBox *m_strict = nullptr;
    QCheckBox *m_inhibit = nullptr;
    QCheckBox *m_autostart = nullptr;
};

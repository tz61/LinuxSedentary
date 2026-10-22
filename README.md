# Sedentary Reminder

A small Linux tray app that tells you to stand up after you have been sitting
at the computer for too long, and stays out of the way otherwise.

- **Counts real sitting time.** Keyboard/mouse idle time, a locked screen or a
  suspended laptop count as having stood up, so the timer starts over by itself
  when you come back from lunch. Short pauses (reading, thinking) still count
  as sitting.
- **Reminds you the way you like:** a desktop notification with buttons, a
  small always-on-top card with an exercise suggestion, or a dimmed screen.
  A gentle warning comes one minute ahead; an ignored reminder repeats.
- **Sounds:** its own soft chimes for the one-minute warning, the reminder,
  the start of a break, its end, and the next countdown starting (Settings →
  *Listen* plays them).
- **Break countdown** with stretching and eye-rest tips. The card ignores input
  for a moment after it appears, so a keystroke meant for your editor cannot
  dismiss it.
- **Tray icon** shows the minutes left until the next break as a ring;
  it turns amber, then red, and blue during a break.
- **Statistics:** sitting time, breaks taken (prompted or on your own),
  skips, snoozes and the longest stretch, for today and the last 7 days.
- Snooze, skip, pause (30 min / 1 h / 2 h / until resumed), strict mode,
  start at login.

Built with C++17 and Qt (6, or 5.15). Tested on KDE Plasma 5 (X11); the idle
detection also covers GNOME and other desktops (see below).

## Install

No root needed: everything goes to `~/.local` and `~/.config`.

```bash
./install.sh
```

This builds a release binary, runs the tests and installs

| File | Path |
| --- | --- |
| program | `~/.local/bin/sedentary-reminder` |
| menu launcher | `~/.local/share/applications/sedentary-reminder.desktop` |
| icon | `~/.local/share/icons/hicolor/scalable/apps/sedentary-reminder.svg` |
| desktop shortcut | `~/Desktop/sedentary-reminder.desktop` (your XDG desktop folder) |
| start at login (optional) | `~/.config/autostart/sedentary-reminder.desktop` |

then starts the app. It asks whether to start at login; `--autostart` /
`--no-autostart` answer up front. You can change it any time with *Start at
login* in the tray menu, in Settings, or with `sedentary-reminder --autostart on|off`. Other options: `--prefix DIR`, `--qt DIR`,
`--no-desktop-icon`, `--no-launch`, `--skip-tests`; see `./install.sh --help`.

Running it again upgrades in place and restarts a running instance.

**Build requirements:** a C++17 compiler, CMake ≥ 3.16 and Qt ≥ 5.15 with the
Widgets and DBus modules. If they are missing, the script prints the command
for your distribution, for example on Debian/Ubuntu:

```bash
sudo apt install g++ cmake make qt6-base-dev
```

(`qtbase5-dev` works too.) A Qt from the Qt online installer in your home
directory is picked up automatically if there is no system Qt, or pass
`--qt ~/Qt/<version>/gcc_64`.

### Uninstall

```bash
./uninstall.sh
```

Keeps your settings and statistics; add `--purge` to delete those too.

## Use

Start **Sedentary Reminder** from the application menu. It lives in the
system tray: left-click for the status window, right-click for the menu.
Starting it again while it runs brings up the status window. Right-clicking
the launcher offers *Take a Break Now*, *Pause Reminders for 1 Hour*,
*Resume* and *Quit*.

From a terminal (talks to the running instance over D-Bus):

```bash
sedentary-reminder --status
```

Other commands: `--take-break`, `--reset` (record that you just took a
break), `--pause MINUTES` (0 = until resumed), `--resume`, `--show`,
`--settings`, `--quit`, `--autostart on|off`, `--hidden` (start in the tray
only).

### Defaults

| Setting | Default |
| --- | --- |
| Remind after sitting for | 45 min |
| Break length | 5 min |
| Snooze | 5 min |
| Away time that counts as a break | 5 min |
| Repeat an ignored reminder every | 5 min |
| Warning one minute ahead | on |
| Style | window with exercise tip |
| Sounds | on |

While a reminder is showing, being away for one break length is enough to
count as the break. A timer break is void if you move the mouse during it.
Pausing silences reminders but sitting time keeps counting, so you get
reminded when the pause ends if you are overdue.

Settings live in `~/.config/sedentary-reminder/settings.ini`, statistics in
`~/.local/share/sedentary-reminder/history.json`.

Sounds are built into the program and played with the first of `paplay`,
`pw-play`, `aplay` or `canberra-gtk-play` that works (the players need a
file, so the chimes are copied to `~/.cache/sedentary-reminder/sounds`).

## How idle detection works

| Session | Source |
| --- | --- |
| X11 (any desktop) | XScreenSaver extension (`libXss`, loaded at runtime) |
| GNOME Wayland | `org.gnome.Mutter.IdleMonitor` |
| KDE Wayland and others | `org.freedesktop.ScreenSaver.GetSessionIdleTime` |

Screen locking is followed through the `ActiveChanged` signal of the
freedesktop and GNOME screensaver services. A suspend shows up as a gap
between timer ticks and counts as time away.

*Idle time is not a break while an app blocks the screensaver* (off by
default) keeps a movie from counting as a break. It is off because some
programs block the screensaver permanently, which would stop breaks from ever
being detected; the status window shows which idle source is in use.

## Development

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cd build && ctest --output-on-failure
```

- `test_scheduler`, `test_history`: unit tests for the timer state machine
  and the statistics/settings files.
- `gui_snapshot`: renders every window and tray-icon state offscreen into
  `build/snapshots/*.png` (light and dark); it also runs as a smoke test.
- `tools/make_sounds.py` synthesizes the chimes in `data/sounds/` (plain
  Python, no dependencies); edit a recipe there and rerun it to change one.
- `./build/sedentary-reminder --fast` makes every "minute" last a second,
  handy for watching a whole cycle. Use it with
  `XDG_CONFIG_HOME`/`XDG_DATA_HOME` pointing elsewhere to keep your real
  statistics clean.

Source layout:

| File | Role |
| --- | --- |
| `src/scheduler.*` | tick-driven state machine: working, due, on break, away, paused |
| `src/history.*`, `src/settings.*` | per-day statistics (JSON), settings (INI) |
| `src/idle_monitor.*` | idle time, screen lock and inhibit detection |
| `src/app.*` | wires the scheduler to tray, windows, notifications, stats |
| `src/break_window.*` | reminder / countdown card, dimmed-screen mode |
| `src/main_window.*` | status window with today's numbers and the 7-day chart |
| `src/notifier.*` | freedesktop notifications with actions, sound playback |
| `src/control_service.*`, `src/main.cpp` | D-Bus single instance and command line |
| `install.sh` | per-user build and install |

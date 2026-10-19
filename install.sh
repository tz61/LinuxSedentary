#!/usr/bin/env bash
# Builds Sedentary Reminder and installs it for the current user, including a
# launcher in the application menu. No root privileges are needed: files go
# under ~/.local (or --prefix) and ~/.config.
set -euo pipefail

APP=sedentary-reminder
DBUS_NAME=org.sedentary.Reminder
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build-release}"
PREFIX="${PREFIX:-$HOME/.local}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"

QT_DIR=""
AUTOSTART=""   # on, off or empty (ask when interactive)
LAUNCH=1
DESKTOP_ICON=1
RUN_TESTS=1
MODE=install
PURGE=0

usage() {
    cat <<EOF
Usage: ./install.sh [options]

Builds Sedentary Reminder and installs it for the current user, with a
launcher in the application menu. No root privileges needed.

Options:
  --prefix DIR     install under DIR (default: ~/.local)
  --qt DIR         Qt to build against, e.g. ~/Qt/6.8.0/gcc_64
                   (default: the system Qt, then the newest ~/Qt/*/gcc_64)
  --autostart      start it automatically at login
  --no-autostart   do not start it at login (removes an existing entry)
  --no-launch      do not start it after installing
  --no-desktop-icon  do not put a shortcut on the desktop
  --skip-tests     do not build or run the tests
  --uninstall      remove the installed files (keeps settings and statistics)
  --purge          with --uninstall: also delete settings and statistics
  -h, --help       show this help
EOF
}

if [ -t 1 ]; then
    BOLD=$'\e[1m' GREEN=$'\e[32m' YELLOW=$'\e[33m' RED=$'\e[31m' RESET=$'\e[0m'
else
    BOLD="" GREEN="" YELLOW="" RED="" RESET=""
fi
step() { printf '%s==>%s %s%s%s\n' "$GREEN" "$RESET" "$BOLD" "$*" "$RESET"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '%swarning:%s %s\n' "$YELLOW" "$RESET" "$*" >&2; }
die() { printf '%serror:%s %s\n' "$RED" "$RESET" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

need_arg() { [ $# -ge 2 ] && [ -n "$2" ] || die "$1 needs a value"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) need_arg "$@"; PREFIX="$2"; shift ;;
        --prefix=*) PREFIX="${1#*=}" ;;
        --qt) need_arg "$@"; QT_DIR="$2"; shift ;;
        --qt=*) QT_DIR="${1#*=}" ;;
        --autostart) AUTOSTART=on ;;
        --no-autostart) AUTOSTART=off ;;
        --no-launch) LAUNCH=0 ;;
        --no-desktop-icon) DESKTOP_ICON=0 ;;
        --skip-tests) RUN_TESTS=0 ;;
        --uninstall) MODE=uninstall ;;
        --purge) PURGE=1 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

[ "$(id -u)" -ne 0 ] || die "run this as your normal user, not root: it installs into your home directory"
case "$PREFIX" in /*) ;; *) PREFIX="$PWD/$PREFIX" ;; esac
PREFIX="${PREFIX%/}"

BIN="$PREFIX/bin/$APP"
DESKTOP_FILE="$PREFIX/share/applications/$APP.desktop"
ICON_FILE="$PREFIX/share/icons/hicolor/scalable/apps/$APP.svg"
AUTOSTART_FILE="$CONFIG_HOME/autostart/$APP.desktop"
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
[ -n "$DESKTOP_DIR" ] && [ "$DESKTOP_DIR" != "$HOME" ] || DESKTOP_DIR="$HOME/Desktop"
SHORTCUT_FILE="$DESKTOP_DIR/$APP.desktop"

# --- Running instance ------------------------------------------------------------

# PID of the running instance (whoever owns the D-Bus name), if any.
instance_pid() {
    local out=""
    if have gdbus; then
        out="$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
            --method org.freedesktop.DBus.GetConnectionUnixProcessID "$DBUS_NAME" 2>/dev/null)" || return 1
        out="${out#*uint32 }"
        out="${out%%,*}"
    elif have dbus-send; then
        out="$(dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
            org.freedesktop.DBus.GetConnectionUnixProcessID string:"$DBUS_NAME" 2>/dev/null)" || return 1
        out="${out##*uint32 }"
    else
        out="$(pgrep -f -- "^$BIN( |\$)" | head -n 1)"
    fi
    case "$out" in '' | *[!0-9]*) return 1 ;; esac
    echo "$out"
}

# PID of a running instance started from *this* installation's binary.
# An instance from another prefix or a development build is left alone.
our_instance_pid() {
    local pid exe
    pid="$(instance_pid)" || return 1
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null)" || return 1
    exe="${exe% (deleted)}"
    [ "$exe" = "$(readlink -f "$BIN" 2>/dev/null)" ] || return 1
    echo "$pid"
}

stop_instance() {
    local pid
    pid="$(our_instance_pid)" || return 1
    info "Stopping the running instance"
    if have gdbus; then
        gdbus call --session --dest "$DBUS_NAME" --object-path /Reminder --method "$DBUS_NAME.Quit" >/dev/null 2>&1 || true
    elif have dbus-send; then
        dbus-send --session --dest="$DBUS_NAME" /Reminder "$DBUS_NAME.Quit" >/dev/null 2>&1 || true
    else
        kill "$pid" 2>/dev/null || true
    fi
    for _ in $(seq 50); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.1
    done
    warn "the running instance did not quit; it keeps running the old version until you restart it"
    return 0
}

launch() {
    if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
        info "No graphical session found; start it from the application menu."
        return
    fi
    if have setsid; then
        setsid -f "$BIN" "$@" >/dev/null 2>&1 </dev/null
    else
        nohup "$BIN" "$@" >/dev/null 2>&1 </dev/null &
    fi
}

refresh_menus() {
    if have update-desktop-database; then
        update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    fi
    # GTK only reads a per-directory icon cache if one exists; keep it current.
    local hicolor="$PREFIX/share/icons/hicolor"
    if [ -f "$hicolor/icon-theme.cache" ] && have gtk-update-icon-cache; then
        gtk-update-icon-cache -q -f -t "$hicolor" 2>/dev/null || true
    fi
    [ -d "$hicolor" ] && touch "$hicolor"
    # KDE rebuilds its menu cache on its own, but this makes it immediate.
    for sycoca in kbuildsycoca6 kbuildsycoca5; do
        if have "$sycoca"; then
            "$sycoca" >/dev/null 2>&1 || true
            break
        fi
    done
}

# --- Uninstall -------------------------------------------------------------------

uninstall() {
    step "Uninstalling Sedentary Reminder from $PREFIX"
    stop_instance || true
    local removed=0
    for f in "$BIN" "$DESKTOP_FILE" "$ICON_FILE" "$AUTOSTART_FILE" "$SHORTCUT_FILE"; do
        if [ -e "$f" ]; then
            rm -f -- "$f"
            info "removed $f"
            removed=1
        fi
    done
    [ "$removed" = 1 ] || info "nothing was installed there"
    refresh_menus

    if [ "$PURGE" = 1 ]; then
        for d in "$CONFIG_HOME/$APP" "$DATA_HOME/$APP"; do
            if [ -d "$d" ]; then
                rm -rf -- "$d"
                info "removed $d"
            fi
        done
    else
        info "Kept settings ($CONFIG_HOME/$APP) and statistics ($DATA_HOME/$APP)."
        info "Run ./install.sh --uninstall --purge to delete them as well."
    fi
    step "Done"
}

# --- Install -------------------------------------------------------------------

deps_hint() {
    echo "Install a C++ compiler, CMake and the Qt development files, for example:"
    if have apt-get; then
        echo "    sudo apt install g++ cmake make qt6-base-dev      # or qtbase5-dev on older releases"
    elif have dnf; then
        echo "    sudo dnf install gcc-c++ cmake make qt6-qtbase-devel"
    elif have pacman; then
        echo "    sudo pacman -S --needed gcc cmake make qt6-base"
    elif have zypper; then
        echo "    sudo zypper install gcc-c++ cmake make qt6-base-devel"
    else
        echo "    a C++17 compiler, CMake >= 3.16 and Qt >= 5.15 (Widgets, DBus)"
    fi
    echo "Or use a Qt from the online installer in your home directory: ./install.sh --qt ~/Qt/<version>/gcc_64"
    echo "(This script never uses sudo itself.)"
}

check_tools() {
    local missing=()
    have cmake || missing+=(cmake)
    have c++ || have g++ || have clang++ || missing+=("a C++ compiler")
    have make || have ninja || missing+=(make)
    if [ ${#missing[@]} -gt 0 ]; then
        printf '%serror:%s missing build tools: %s\n' "$RED" "$RESET" "${missing[*]}" >&2
        deps_hint >&2
        exit 1
    fi
}

# Newest Qt from the Qt online installer, if any.
home_qt() {
    local dir
    for dir in $(ls -d "$HOME"/Qt/[0-9]*/gcc_64 "$HOME"/Qt/[0-9]*/gcc_arm64 2>/dev/null | sort -V -r); do
        if [ -d "$dir/lib/cmake/Qt6" ] || [ -d "$dir/lib/cmake/Qt5" ]; then
            echo "$dir"
            return
        fi
    done
}

configure() {
    local qt="$1" log="$BUILD_DIR/configure.log"
    local args=(-S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX")
    [ "$RUN_TESTS" = 1 ] || args+=(-DSEDENTARY_BUILD_TESTS=OFF)
    [ -z "$qt" ] || args+=(-DCMAKE_PREFIX_PATH="$qt")
    mkdir -p "$BUILD_DIR"
    # A fresh cache, so a different Qt or prefix takes effect.
    rm -f "$BUILD_DIR/CMakeCache.txt"
    if cmake "${args[@]}" >"$log" 2>&1; then
        info "$(grep -m1 'Building against Qt' "$log" | sed 's/^-- //')${qt:+ ($qt)}"
        return 0
    fi
    return 1
}

install_app() {
    step "Checking build tools"
    check_tools

    step "Configuring"
    if [ -n "$QT_DIR" ]; then
        case "$QT_DIR" in /*) ;; *) QT_DIR="$PWD/$QT_DIR" ;; esac
        configure "$QT_DIR" || { tail -n 25 "$BUILD_DIR/configure.log" >&2; die "could not configure with Qt from $QT_DIR"; }
    elif ! configure ""; then
        local fallback
        fallback="$(home_qt)"
        if [ -z "$fallback" ] || ! configure "$fallback"; then
            tail -n 25 "$BUILD_DIR/configure.log" >&2
            echo >&2
            printf '%serror:%s Qt (>= 5.15, with Widgets and DBus) was not found.\n' "$RED" "$RESET" >&2
            deps_hint >&2
            exit 1
        fi
    fi

    step "Building"
    local jobs
    jobs="$(nproc 2>/dev/null || echo 2)"
    cmake --build "$BUILD_DIR" --parallel "$jobs"

    if [ "$RUN_TESTS" = 1 ]; then
        step "Running tests"
        (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure) >"$BUILD_DIR/tests.log" 2>&1 || {
            cat "$BUILD_DIR/tests.log" >&2
            die "tests failed; nothing was installed (use --skip-tests to install anyway)"
        }
        info "$(grep -m1 'tests passed' "$BUILD_DIR/tests.log")"
    fi

    local was_running=0
    step "Installing into $PREFIX"
    if stop_instance; then
        was_running=1
    fi
    cmake --install "$BUILD_DIR" | sed 's/^-- /    /'
    if have desktop-file-validate; then
        desktop-file-validate "$DESKTOP_FILE" || warn "the launcher did not validate cleanly"
    fi
    refresh_menus
    info "Launcher: $DESKTOP_FILE"

    if [ "$DESKTOP_ICON" = 1 ]; then
        if [ -d "$DESKTOP_DIR" ]; then
            # Desktops only run launchers that are executable (KDE, Xfce)
            # or marked trusted (GNOME).
            install -m 755 "$DESKTOP_FILE" "$SHORTCUT_FILE"
            if have gio; then
                gio set "$SHORTCUT_FILE" metadata::trusted true >/dev/null 2>&1 || true
            fi
            info "Desktop shortcut: $SHORTCUT_FILE"
        else
            info "No desktop folder ($DESKTOP_DIR); skipped the desktop shortcut."
        fi
    elif [ -f "$SHORTCUT_FILE" ]; then
        rm -f -- "$SHORTCUT_FILE"
        info "Removed the desktop shortcut"
    fi

    # Start at login?
    if [ -z "$AUTOSTART" ]; then
        if [ -f "$AUTOSTART_FILE" ]; then
            AUTOSTART=on # refresh it so it points at this installation
        elif [ -t 0 ] && [ -t 1 ]; then
            printf '    Start Sedentary Reminder automatically when you log in? [Y/n] '
            read -r answer || answer=n
            case "$answer" in [nN]*) AUTOSTART=off ;; *) AUTOSTART=on ;; esac
        fi
    fi
    if [ "$AUTOSTART" = on ]; then
        "$BIN" --autostart on >/dev/null && info "Starts at login: $AUTOSTART_FILE"
    elif [ "$AUTOSTART" = off ] && [ -f "$AUTOSTART_FILE" ]; then
        "$BIN" --autostart off >/dev/null && info "Removed the start-at-login entry"
    fi

    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *) info "Note: $PREFIX/bin is not on your PATH. The menu launcher works regardless;"
           info "add it to PATH to run '$APP --status' and friends from a terminal." ;;
    esac

    if [ "$was_running" = 1 ]; then
        step "Restarting Sedentary Reminder"
        launch --hidden
    elif [ "$LAUNCH" = 1 ]; then
        step "Starting Sedentary Reminder"
        launch
    fi
    step "Done. Find \"Sedentary Reminder\" in your application menu."
}

if [ "$MODE" = uninstall ]; then
    uninstall
else
    install_app
fi

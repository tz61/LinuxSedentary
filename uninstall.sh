#!/usr/bin/env bash
# Removes Sedentary Reminder for the current user. Pass --purge to also
# delete settings and statistics, --prefix DIR if it was installed elsewhere.
exec "$(dirname "${BASH_SOURCE[0]}")/install.sh" --uninstall "$@"

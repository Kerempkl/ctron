#!/bin/sh
# ctron-toggle — M4/Armoury Crate key helper (KDE "Launch (3)" binding).
# The desktop shortcut only spawns; it has no toggle sense of its own,
# so every key press opened ANOTHER konsole/ctron window. Toggle here:
# a running ctron gets closed (konsole -e exits with its child), none
# running means open one.
#
# Targets the TUI by process name; short-lived headless CLI calls
# (ctron --status etc.) are gone before anyone can press a key twice.

if pgrep -x ctron >/dev/null 2>&1; then
    pkill -x ctron
    exit 0
fi

if [ -n "$TERMINAL" ] && command -v "$TERMINAL" >/dev/null 2>&1; then
    exec "$TERMINAL" -e "$HOME/.local/bin/ctron"
elif command -v konsole >/dev/null 2>&1; then
    exec konsole -e "$HOME/.local/bin/ctron"
else
    echo "ctron-toggle: no terminal found (set \$TERMINAL or install konsole)" >&2
    exit 1
fi

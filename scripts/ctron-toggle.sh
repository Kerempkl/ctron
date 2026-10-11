#!/bin/sh
# ctron-toggle — M4/Armoury Crate key helper (KDE "Launch (3)" binding).
# The desktop shortcut only spawns; it has no toggle sense of its own,
# so every key press opened ANOTHER konsole/ctron window. Toggle here:
# a running ctron gets closed (konsole -e exits with its child), none
# running means open one.
#
# The open is guarded against double presses: the terminal takes up to
# ~1 s to produce the ctron process, and a second press inside that
# window used to spawn a duplicate. The launch is claimed atomically
# (noclobber stamp); the claim self-clears once the TUI is alive (or
# after 8 s), and a stale claim older than 12 s is stolen, so a killed
# toggle can never wedge the key permanently.
#
# Targets the TUI by process name; short-lived headless CLI calls
# (ctron --status etc.) are gone before anyone can press a key twice.

STAMP="${XDG_RUNTIME_DIR:-/tmp}/ctron-toggle.launching"
LOG="${XDG_RUNTIME_DIR:-/tmp}/ctron-toggle.log"
log() { printf '%s %s\n' "$(date +%H:%M:%S)" "$*" >> "$LOG"; }

log "invoked pid=$$ ctron=$(pgrep -x ctron | wc -l) stamp=$(cat "$STAMP" 2>/dev/null || echo none)"

if pgrep -x ctron >/dev/null 2>&1; then
    log "branch: close"
    pkill -x ctron
    exit 0
fi

now=$(date +%s)
if ! (set -C; printf '%s\n' "$now" > "$STAMP") 2>/dev/null; then
    old=$(cat "$STAMP" 2>/dev/null)
    case "$old" in ''|*[!0-9]*) old=0 ;; esac
    if [ $((now - old)) -lt 12 ]; then
        log "branch: in-flight swallow"
        exit 0 # launch already in flight
    fi
    rm -f "$STAMP"
    (set -C; printf '%s\n' "$now" > "$STAMP") 2>/dev/null || exit 0
fi
log "branch: open (term follows)"

if [ -n "$TERMINAL" ] && command -v "$TERMINAL" >/dev/null 2>&1; then
    term=$TERMINAL
elif command -v konsole >/dev/null 2>&1; then
    term=konsole
else
    rm -f "$STAMP"
    echo "ctron-toggle: no terminal found (set \$TERMINAL or install konsole)" >&2
    exit 1
fi

# Not exec: the toggle stays alive briefly to watch for the TUI and
# clear the stamp itself. The terminal is backgrounded and survives.
"$term" -e "$HOME/.local/bin/ctron" &
i=0
while [ "$i" -lt 8 ]; do
    pgrep -x ctron >/dev/null 2>&1 && break
    sleep 1
    i=$((i+1))
done
rm -f "$STAMP"
exit 0

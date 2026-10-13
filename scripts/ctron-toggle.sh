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
    # Collect ctron PID and its ancestor PIDs (terminal emulator, subshells)
    ancestors=""
    for cpid in $(pgrep -x ctron); do
        cur=$cpid
        while [ -n "$cur" ] && [ "$cur" -gt 1 ] 2>/dev/null; do
            ancestors="${ancestors}${ancestors:+, }$cur"
            parent=$(ps -o ppid= -p "$cur" 2>/dev/null | tr -d ' ')
            [ -z "$parent" ] && break
            [ "$parent" -le 1 ] 2>/dev/null && break
            cur=$parent
        done
    done

    # 1. KDE Plasma (KWin via DBus scripting)
    QDBUS=""
    if command -v qdbus6 >/dev/null 2>&1; then
        QDBUS="qdbus6"
    elif command -v qdbus >/dev/null 2>&1; then
        QDBUS="qdbus"
    fi

    if [ -n "$QDBUS" ] && "$QDBUS" org.kde.KWin /KWin >/dev/null 2>&1; then
        tag="ctron_toggle_$$"
        js="${XDG_RUNTIME_DIR:-/tmp}/ctron_kwin_$$.js"
        cat << EOF > "$js"
var pids = [${ancestors}];
var wins = workspace.windowList();
var found = null;
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    for (var j = 0; j < pids.length; j++) {
        if (w.pid === pids[j]) {
            found = w;
            break;
        }
    }
    if (found) break;
}
if (found) {
    if (found === workspace.activeWindow && !found.minimized) {
        console.log("$tag ACTIVE");
    } else {
        workspace.activeWindow = found;
        console.log("$tag RAISED");
    }
} else {
    console.log("$tag NOT_FOUND");
}
EOF
        id=$("$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$js" "$tag" 2>/dev/null)
        if [ -n "$id" ]; then
            "$QDBUS" org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run >/dev/null 2>&1
            "$QDBUS" org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.stop >/dev/null 2>&1
            "$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$tag" >/dev/null 2>&1
            rm -f "$js"

            res=$(journalctl --user -n 3 -o cat -g "$tag" 2>/dev/null | tail -n 1 | awk '{print $2}')
            case "$res" in
                RAISED)
                    log "branch: raise (KWin window brought to front)"
                    exit 0
                    ;;
                ACTIVE)
                    log "branch: close (KWin window was active)"
                    pkill -x ctron
                    exit 0
                    ;;
                *)
                    ;;
            esac
        else
            rm -f "$js"
        fi
    fi

    # 2. Hyprland
    if [ -n "$HYPRLAND_INSTANCE_SIGNATURE" ] && command -v hyprctl >/dev/null 2>&1; then
        active_pid=$(hyprctl activewindow -j 2>/dev/null | grep -o '"pid": *[0-9]*' | head -n 1 | grep -o '[0-9]*')
        is_active=0
        for p in $(echo "$ancestors" | tr -d ','); do
            if [ "$p" = "$active_pid" ]; then
                is_active=1
                break
            fi
        done
        if [ "$is_active" -eq 1 ]; then
            log "branch: close (Hyprland window active)"
            pkill -x ctron
            exit 0
        else
            log "branch: raise (Hyprland focusing window)"
            for p in $(echo "$ancestors" | tr -d ','); do
                if hyprctl dispatch focuswindow "pid:$p" >/dev/null 2>&1; then
                    exit 0
                fi
            done
        fi
    fi

    # 3. Fallback: window already active or untracked -> close
    log "branch: close (fallback)"
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

#!/bin/sh
# Single-instance 512² applet for daetron on_add. Do not kill on unplug.
set -eu
if pgrep -f 'kitty --class ctron_widget' >/dev/null 2>&1; then
    exit 0
fi
if pgrep -x ctron >/dev/null 2>&1; then
    exit 0
fi
bin="${HOME}/.local/bin/ctron"
if [ ! -x "$bin" ]; then
    bin="${HOME}/Documents/ctron/build/ctron"
fi
exec "$bin"

#!/usr/bin/env bash
# Launch Ctron in 512x512 floating applet window
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/ctron"

if [[ ! -x "$BIN" ]]; then
  if command -v notcurses-info >/dev/null 2>&1; then
    make -C "$DIR"
  else
    nix-shell -p notcurses gcc pkg-config --run "make -C '$DIR'"
  fi
fi

# Run inside Kitty with 512x512 floating applet dimensions
exec kitty \
  --class ctron_widget \
  --title "Ctron" \
  -o hide_window_decorations=yes \
  -o remember_window_size=no \
  -o initial_window_width=512 \
  -o initial_window_height=512 \
  -o window_padding_width=4 \
  -e env CTRON_APPLET=1 "$BIN" "$@" 2>/dev/null

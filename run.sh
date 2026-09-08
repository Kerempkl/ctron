#!/usr/bin/env bash
# Build if needed, then run ctron with the given args (headless by default).
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/ctron"
if [[ ! -x "$BIN" ]]; then
  make -C "$DIR"
fi
exec "$BIN" "$@"

#!/bin/sh
# make check-help — the flag block in README.md is a verbatim copy of
# `ctron --help`. It is the one piece of the help text that lives in
# two places, so this compares them. Exit 1 and show the difference on
# the first character that drifted.
set -eu

cd "$(dirname "$0")/.."

[ -x build/ctron ] || {
    echo "check-help: build ctron first (make)" >&2
    exit 1
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ctron-check-help.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# argv[0] is whatever path the binary was started with; README says ctron
./build/ctron --help | sed "s#\./build/ctron#ctron#g" > "$tmp/help"
# the first fenced block under the "Full flag list" summary
awk '/^<summary><b>Full flag list<\/b>/ { f = 1; next }
     f && /^```$/ { c++; if (c == 2) exit; next }
     f && c == 1 { print }' README.md > "$tmp/readme"

if cmp -s "$tmp/help" "$tmp/readme"; then
    printf 'check-help: the README flag block is identical to ctron --help\n'
else
    echo "check-help: README.md flag block differs from ctron --help:" >&2
    echo "  (< help output   > README)" >&2
    diff "$tmp/help" "$tmp/readme" >&2 || true
    exit 1
fi

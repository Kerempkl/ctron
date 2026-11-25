#!/bin/sh
# make check-version — the version is written down once, in the VERSION
# file, and handed to the compiler by the Makefile. The copies that
# cannot read that file (README badge, flake.nix, PKGBUILD) and the
# CHANGELOG heading must agree with it. Run before a release commit and
# in CI; exits 1 on the first mismatch.
set -eu

cd "$(dirname "$0")/.."

die() {
    printf 'check-version: %s\n' "$*" >&2
    exit 1
}

[ -f VERSION ] || die "no VERSION file in the repository root"
version=$(cat VERSION) || die "cannot read VERSION"
[ -n "$version" ] || die "VERSION is empty"
# major.minor.patch, optionally -suffix: digits first, at least two
# dots, and nothing outside [0-9A-Za-z.-] (a second line fails here too)
case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) die "VERSION is not major.minor.patch: '$version'" ;;
esac
case "$version" in
    *[!0-9A-Za-z.-]*) die "VERSION has characters outside [0-9A-Za-z.-]: '$version'" ;;
esac

expect() {
    # expect <label> <file> <grep -E pattern>
    grep -Eq -- "$3" "$2" || die "$1 ($2) does not say $version"
}

expect "README version badge" README.md "badge/version-v$version-"
expect "flake.nix version" flake.nix "^[[:space:]]*version = \"$version\";"
expect "PKGBUILD pkgver" contrib/PKGBUILD "^pkgver=$version\$"
# the newest dated heading must be this version; [Unreleased] may sit above it
top=$(grep -m1 '^## \[[0-9]' CHANGELOG.md) || die "CHANGELOG.md has no version heading"
# "## [0.13.0] — 2026-11-25" -> "0.13.0"
top=$(printf '%s\n' "$top" | sed 's/^## \[\([0-9A-Za-z.-]*\)\].*/\1/')
[ "$top" = "$version" ] \
    || die "the newest CHANGELOG heading is $top, not $version"
grep -q '^#define VERSION CTRON_VERSION' src/main.c \
    || die "src/main.c must take the version from CTRON_VERSION, not a literal"

# A binary newer than VERSION must carry this version; an older one is
# just a stale build and says nothing.
if [ -x build/ctron ] && [ build/ctron -nt VERSION ]; then
    got=$(./build/ctron --version) || die "build/ctron --version failed"
    case "$got" in
        "ctron $version "*) ;;
        *) die "build/ctron reports '$got'" ;;
    esac
fi

printf 'check-version: %s — VERSION, README badge, flake.nix, PKGBUILD, CHANGELOG agree\n' \
    "$version"

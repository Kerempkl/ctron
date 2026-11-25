# Contributing to ctron

ctron is a small C11 program on purpose — one binary, one dependency
(`notcurses`), no daemon — and most of what keeps it trustworthy is
discipline rather than code. This page is the short version of that
discipline; the long versions live in the documents it points to.

## Read first

| Document | Why |
| --- | --- |
| [AGENTS.md](AGENTS.md) | what ctron is, where things live, and the five hard-won rules that must not regress |
| [HANDOFF.md](HANDOFF.md) | the latest sessions: what changed, what was verified, what is next |
| [NEXT.md](NEXT.md) / [REVIEW.md](REVIEW.md) | open work, the version policy, the engineering roadmap |
| [SECURITY.md](SECURITY.md) | the privilege model — read it before touching anything that writes to hardware |
| [CHANGELOG.md](CHANGELOG.md) | release-level history (the per-session log is in `docs/history/`) |

The rules in AGENTS.md come from real failures on real machines (a
Turkish locale that made `strtod` read `59.87` as `59`, tools that exit
`0` without applying anything, a stale kernel cache that reports `0`).
They are not style preferences.

## Build and verify

```bash
make                 # must stay warning-free (-Wall -Wextra)
make test            # unit tests (pure functions, no hardware)
./build/ctron --doctor
./build/ctron --status
make tuitest         # pty smoke test of the TUI (needs a terminal-like pty)
make comptest        # shell completion regression test (bash, zsh, fish)
make check-help      # the README flag block is the real `ctron --help`
make check-version   # VERSION == README badge == flake.nix == PKGBUILD == CHANGELOG
```

More thorough, before a release or after a risky change:

```bash
make HARDEN=1 LTO=1          # exploit mitigations and link-time optimisation
make CC=clang                # a second compiler
CFLAGS="-O1 -g -fsanitize=undefined,leak -fno-sanitize-recover=all" \
LDFLAGS="-fsanitize=undefined,leak" make test
clang --analyze src/*.c      # static analysis
```

Pass build flags through the environment, as above. `make CFLAGS=...`
on the command line replaces the flags ctron cannot build without
(`-std=c11 -Isrc -D_GNU_SOURCE`, the notcurses flags).

Hardware cannot be tested in CI. Whatever touches a write path, a
sensor read or a display backend is verified on a real machine and the
measurement is written down — machine, kernel, what was observed. If
you cannot test on the hardware a change is for, say so in the pull
request; an untested write path does not ship as "verified".

Safe ways to exercise the CLI without changing anything:

```bash
ctron --ppt 45,55,55 --dry-run      # validate, print, never write
CTRON_CONFIG=$(mktemp -d) ctron --status   # a throwaway config directory
```

## Working on the code

- **No writes from the poll loop.** Every write is a user action and
  goes through `control.c`: asusctl, direct sysfs, `sudo -n`.
- **Honest reads.** Unknown is `--`. Never a plausible fallback.
- **Verify writes by reading back.** `kscreen-doctor`, Hyprland's
  `keyword monitor`, `asusctl aura effect` and `asusctl fan-curve` all
  exit `0` without doing anything on failure.
- **One place for each thing.** `ui_layout()` places regions, the
  `SECTIONS[]` registry lists telemetry sections, `cmds.c` is the single
  key/value table behind the CLI, modes and snapshots.
- **Pure logic is unit-tested.** Parsers and model functions live in
  functions that take arguments and return values, so `tests/` can pin
  them without hardware. Put new logic there and add the test in the
  same change.
- **Help and docs travel with the feature.** A new key needs its line in
  the TUI keybar/Help in the same commit, its flag in `ctron --help`,
  the README flag block (byte-identical to the help output), the man
  page and the three shell completions (`make comptest` takes the flag
  list from `--help` and fails when one is missing, `make check-help`
  fails when the README block drifts).

## Commits and pull requests

Commits follow the prefixes already in the log:

```
feat(scope): …    fix(scope): …    docs: …    release: vX.Y.Z — …
```

with `scope` the area, as in the log: `fan`, `snapshots`, `hw`, `ui`,
`cli`, `profile`, `util`, `daeboard`, `smoke`, `security` … . A commit
builds and passes `make test`; a release commit is the only place the
version changes.

A pull request says what changed, why, and **how it was verified** —
including what could not be verified. Screenshots for TUI changes
(`scripts/tui_screenshot.py`) are welcome.

When you finish a session, append a short entry to `HANDOFF.md`, signed
with the name you want to be known by, and update `CHANGELOG.md` for
anything user-visible. Several people and agents work in this tree;
append, do not rewrite.

## Versions

The version is written once, in the `VERSION` file at the repository
root; the Makefile hands it to the compiler. README's badge, `flake.nix`
and `contrib/PKGBUILD` carry copies that `make check-version` compares.
A release is one commit that moves all of them, with the changelog
entry, and an annotated tag `vX.Y.Z` on that commit. The scheme is
semver; the policy is in NEXT.md.

## Reporting problems

For anything that behaves differently on your machine, open a *Hardware
report* issue: it asks for the laptop model, the kernel, the `ctron
--doctor` output and what you saw. For a privilege or security concern,
see SECURITY.md for the model that is supposed to hold, and say which
part does not.

## Adding a display backend or a sensor

One file plus one registry line: `src/display/` holds the compositor
backends behind `display_ops_t`. Sensors are read in `src/hw.c`; a new
telemetry series is an entry in the `TELEM_SERIES` table and one in the
`SECTIONS[]` registry of `src/ui/panel_telem_full.c`. In both cases the
rule from above applies: read what the hardware really reports, and
print `--` when it reports nothing usable.

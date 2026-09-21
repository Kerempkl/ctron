# ctron — Agent Guidelines

Lightweight ASUS laptop control center for Linux. One C binary, one
dependency (notcurses). Verified machines: ASUS TUF Gaming **A16 FA608PP**
(Ryzen 9 8940HX + RTX 5070 Max-Q, CachyOS, KDE Wayland, kernel 7.2,
Kerempkl) and **A15 FA507NVR** (Ryzen 7 7435HS + RTX 4060, NixOS,
Hyprland 0.55, Arcioth).

**Before doing anything, read `HANDOFF.md`** (latest session state) and
`CHANGELOG.md` (what changed when). Append to both when you finish a
session: a signed HANDOFF section + one CHANGELOG entry per change.

## What ctron is (v2)

- No-args `ctron` → fullscreen notcurses TUI in the current terminal.
- Any argument → headless CLI (`--status`, `--hz 60`, `--mode turbo`, ...);
  piped output never opens the TUI.
- Layout: left column profiles + quick controls, right-top workspace
  (fan curve editor / power / light / settings / mode editor), right-bottom
  live telemetry. Panels are independent widgets; `ui_layout()` in
  `src/ui/ui.c` is the only place that places them.

## Architecture map

```
src/util.c     sysfs/exec/log helpers; ut_priv_write() = the only write door
src/hw.c       read layer: hw_refresh_fast (poll) / hw_refresh_live (snapshot)
src/control.c  write layer (asusctl -> direct sysfs -> sudo -n)
src/cmds.c     key/value command table shared by CLI, modes and .ctr profiles
src/fan.c      8-point fan curve model (pure, unit-tested)
src/modes.c    user shortcut bundles (~/.config/ctron/modes.ini)
src/profile.c  .ctr profiles (free-form names)
src/display/   compositor backends behind display_ops_t (kde done,
               hyprland handed to Arcioth); add one = one file + registry line
tests/         make test
```

## Hard-won rules (do not regress)

1. **LC_NUMERIC stays "C"** after `setlocale(LC_ALL, "")` in the TUI.
   On tr_TR, `strtod("59.87")` returns 59 and refresh lists corrupt
   (ghost 59/164 Hz, FAILED writes). Symptom seen 2026-09-21.
2. **No writes from poll loops.** Every write is a user action through
   `control.c`'s chain: asusctl → direct sysfs → `sudo -n` (clean error,
   never a prompt/hang).
3. **Honest reads.** Stale kernel cache (PPT `0`/`5`) prints `--`;
   never fabricate fallback data.
4. **Verify writes by reading back.** kscreen-doctor and Hyprland's
   `keyword monitor` both exit 0 without applying. kscreen-doctor needs
   **integer** refresh (`@60`, not `@60.00`).
5. Quit key is `q`/`Q` only (not ESC — Kitty focus-out sends CSI).

## Build & verify

```bash
make            # must stay warning-free (-Wall -Wextra)
make test
./build/ctron --doctor     # capability report
./build/ctron --status     # live snapshot
make install               # ~/.local/bin/ctron
```

TUI smoke test needs a real terminal or a pty that answers terminal
queries (DA/cursor/sync/OSC color). A plain pty with no responder hangs
inside `notcurses_init` — that is the test harness's fault, not ctron's.

## Scope decisions

- TUF + Ryzen + NVIDIA only; no device-plugin framework.
- Out of scope (parked): Waybar sync, MUX/dGPU switch,
  `throttle_thermal_policy` (overlaps platform profiles).
- Hyprland backend open items live in README "Hyprland handoff notes".

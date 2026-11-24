# ctron — Agent Guidelines

Lightweight ASUS laptop control center for Linux. One C binary, one
dependency (notcurses). Verified machines: ASUS TUF Gaming **A16 FA608PP**
(Ryzen 9 8940HX + RTX 5070 Max-Q, CachyOS, KDE Wayland, kernels 7.2–7.3
& 6.18-lts, Kerempkl) and **A15 FA507NVR** (Ryzen 7 7435HS + RTX 4060,
NixOS, Hyprland 0.55, Arcioth).

**Before doing anything, read `HANDOFF.md`** (latest session state) and
`CHANGELOG.md` (release-level changes; full engineering archive lives in
`docs/history/`). Open work is tracked in `NEXT.md` and `REVIEW.md`;
audit state lives in `AUDIT.md`. Append to `HANDOFF.md` (signed) and
`CHANGELOG.md` when you finish a session.

## What ctron is (v2)

- No-args `ctron` → fullscreen notcurses TUI in the current terminal.
- Any argument → headless CLI (`--status`, `--hz 60`, `--mode turbo`, ...);
  piped output never opens the TUI.
- Layout (Konsept C): thin top live summary header, slim 22-col left rail
  (`VIEWS` 1–5 + compact `LIVE` summary, auto-collapsing to inline top tabs
  below 96 cols), full-height right workspace (`1 Power`, `2 Fans`,
  `3 Display & Light`, `4 Snapshots`, `5 Telemetry`; startup tab selectable
  in `,` Preferences), and unified 2-row status/log + contextual keybar.
  Preferences (`,`), Event Log (`L`), Help (`?`), Fan Presets (`m`), and
  daeboard editor (`b`) run as clean single-line modals (`Esc` strictly backs
  out/closes); per-core limits (`c`) sit inline inside `1 Power`. `ui_layout()`
  in `src/ui/ui.c` is the only place that places regions.

## Architecture map

```
src/main.c     CLI parsing + VERSION; any arg = headless, none = TUI
src/util.c     sysfs/exec/log/atomic-write helpers; ut_priv_write /
               ut_write_via_argv = the write doors (fork+execve argv)
src/hw.c       read layer: hw_refresh_fast (poll) / hw_refresh_live
               (snapshot); telemetry sources (gpu_metrics retired)
src/control.c  write layer (asusctl -> direct sysfs -> sudo -n)
src/cmds.c     key/value command table shared by CLI, modes and .ctr profiles
src/fan.c      8-point fan curve model + asusd RON preset parser (unit-tested)
src/modes.c    user shortcut bundles (~/.config/ctron/modes.ini)
src/profile.c  .ctr snapshots (free-form names, metadata, live diff)
src/settings.c prefs + hardware persistence (~/.config/ctron/settings.ini)
src/telem.c    pure sample rings + window/since-reset stats (unit-tested)
src/daeboard.c optional LED daemon client (AF_UNIX seqpacket socket)
src/display/   compositor backends behind display_ops_t (kde done,
               hyprland handed to Arcioth); add one = one file + registry line
src/ui/        Konsept C layout + views; panel_telem_full.c = 5 Telemetry view
               (SECTIONS[] registry: section add/move = one line there)
tests/         make test; scripts/tui_smoke.py = make tuitest
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
- Hyprland backend open items live in `README.md` ("Display backends" →
  Hyprland handoff state) and `NEXT.md`.

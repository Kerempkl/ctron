# ctron v2 — approved plan (2026-09-19)

Hyprland 0.55 on FA507NVR: detect/modes/`set_hz` via `hl.monitor` eval — 2026-09-20. See HANDOFF.md.

Single C binary for ASUS laptops. No-args `ctron` opens a fullscreen TUI;
any argument (`ctron --status`, `ctron --mode turbo`) runs headless and exits.
Notcurses is the only build dependency.

Target machine for verification: ASUS TUF Gaming A16 **FA608PP**
(Ryzen 9 8940HX + RTX 5070 Max-Q, CachyOS, KDE Plasma Wayland, kernel 7.2).
Primary development session: KDE. Hyprland backend is prepared for handoff.

## File layout

```
src/
  main.c          CLI parsing (multi-flag, --flag=val), tty check, dispatch
  util.c/.h       sysfs read/write, exec helper, parse utils, log ring
  hw.c/.h         hardware state + read layer (fast poll / full live)
  control.c/.h    write layer: profile/EPP/PPT/battery/fan/kbd/panel-od
  fan.c/.h        8-point fan curve model (X=°C, Y=pwm)
  modes.c/.h      user modes (modes.ini) + applier
  cmds.c/.h       key/value command table shared by CLI, modes and profiles
  profile.c/.h    .ctr profiles (free-form names)
  settings.c/.h   ~/.config/ctron prefs + hardware persist
  ui/             fullscreen TUI
    ui.c/.h           main loop, layout engine, input dispatch
    ui_internal.h     shared panel contract, palette, targets, text input
    panel_profiles.c  left-top: profile list + save/apply/delete/import/export
    panel_controls.c  left-bottom: quick controls (modes, profile, fan, battery)
    panel_workspace.c right-top: workspace views (FAN/POWER/LIGHT/SETTINGS/HELP)
    panel_telemetry.c right-bottom: always-on live telemetry strip
    panel_settings.c  Settings view incl. CLI Shortcuts editor
    editor_fan.c      large fan curve editor (graph, points, write)
  display/
    display.h/.c   backend interface + auto-detect (DISPLAY_BACKEND override)
    display_kde.c  kscreen-doctor backend — fully working (verified on KDE)
    display_hypr.c hyprctl backend — basic port from v1; see README handoff note
tests/test_core.c  fan CSV round-trip, settings/modes/profile persist, parsers
Makefile           all / test / install / clean
```

## Layout of the fullscreen TUI

```
+--------------+---------------------------------------------+
| PROFILES     |  WORKSPACE (fan curve editor by default;     [Settings]
|  list +      |   POWER / LIGHT / SETTINGS / MODE EDIT       [Help]  q:quit
|  save/load   |   views are switched by left-column buttons) |
+--------------+                                             |
| CONTROLS     +---------------------------------------------+
|  mode,       | LIVE: 62°C 47°C GPU 3300/3600rpm 5.2GHz     |
|  profile,    | BAT 58% Performance  | last action log       |
|  fan, batt   |                                             |
+--------------+---------------------------------------------+
```

- Every panel is an independent widget (draw + key + mouse-action); the
  layout engine only places rectangles. Changing the layout later means
  re-placing widgets, not rewriting them (requested by user).
- Keyboard first (vim-style), full mouse support (click; fan graph
  point select/move). Focus travels with Tab / 1..4.

## Modes ("CLI shortcuts")

- Defaults in `~/.config/ctron/modes.ini`: turbo, performance, balanced,
  quiet, silent. Each mode is a list of steps executed through the shared
  command table (e.g. `profile performance, ppt P80, fan cool, hz max`).
- CLI: `ctron --mode turbo`, `ctron mode list|show|add|delete`.
- TUI Settings panel: list / create / edit / delete modes (name + steps).

## Write layer

Priority chain in one place (`control.c`): **asusctl** → direct sysfs write
→ `sudo -n` (clean error if passwordless sudo is unavailable; never blocks).
No writes from the poll loop; writes happen only on user action.

## Read layer

- Fast poll (default 250 ms): k10temp Tctl, scaling freq, battery, fan RPM
  (hwmon name `asus`, fan1/fan2_input with labels). GPU temp via nvidia-smi,
  cached 2 s (never inside the poll hot path).
- Full snapshot: + PPT (armoury attribute → asusctl → nb-wmi; print `--`
  when the kernel reports stale 0/5), panel OD, charge threshold, platform
  profile, EPP, kbd brightness, fan curve + enable state, display Hz.
- No fabricated fallback data (v1 synthesized default curves when reads
  failed; v2 prints `--` instead).

## PPT limits

No more hardcoded FA507 numbers. Limits come from firmware-attributes
`min`/`max` files when present; otherwise a generous physical ceiling
(SPL 15–90 W, SPPT/FPPT 35–120 W) and kernel rejection (EIO) is surfaced
to the user. Invariant `fppt >= sppt >= spl` kept; presets Q45/B60/P80 kept.

## Display backend

`display.h` contract: `detect / current_hz / modes_hz[] / set_hz`.
KDE backend parses `kscreen-doctor -o` and applies
`output.<name>.mode.<W>x<H>@<refresh>` — verified live. Hyprland backend
carries the working v1 hyprctl code (read monitors, keyword monitor); mode
list parsing and edge cases are listed in README as the handoff checklist.
Adding a backend (wlr-randr, xrandr) = one new file + one line in the list.

## Explicitly out of scope

MUX/dGPU switch · throttle_thermal_policy (overlaps platform profiles) ·
Waybar sync (parked for later, README roadmap) · wlr-randr/xrandr backends.

## Verification

1. `make` compiles warning-free; `make test` passes.
2. Live on the FA608PP: `--doctor`, `--status` (RPM, GPU °C, PPT, Hz),
   `--watch` observed for a few seconds.
3. TUI smoke test under a pseudo-tty (opens, renders, quits).
4. Writers are only exercised when explicitly invoked by the user.

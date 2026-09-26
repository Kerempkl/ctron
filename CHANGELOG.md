# Changelog

## 2026-09-26 — POWER: exact-value typing + range hints

- `t` on a numeric row (SPL/SPPT/FPPT, NV boost/temp, CPU clock)
  starts exact-value typing, seeded with the staged value. Enter
  stages it — clamped exactly like a nudge, and digits only: anything
  else is dropped with a log line instead of staging the minimum.
  Esc cancels; switching views or any click ends typing; starting to
  type dismisses the apply toast.
- Plain value rows now show their allowed window, e.g. `65 W (15–90)`.
  The firmware windows come from `ctrl_ppt_limits` cached for one
  second in the draw pass, so frames do not re-read the armoury
  attrs six times each. Dirty rows keep the `live → staged ●` form
  (the arrow wins over the range); the typing line shows the window
  too: `set SPL (sustained) (15–90): 65_`.
- Help view (POWER section) documents `t`.
- `make` warning-free on the ctron side, `make test` ok, installed to
  `~/.local/bin/ctron`, `--status` live on FA608PP.

## 2026-09-24 — POWER apply toast (what changed, in colour)

- After Apply, the POWER panel shows a toast above the buttons for
  5 s: green `✓ SPL 65→80 W · EPP performance` listing every field
  that changed (old→new; `→80 W` when the live read was stale), red
  `⚠ … · N failed` when writes failed. New staging or Revert dismisses
  it; the plain `ut_log` line stays as history in the telemetry panel.
- Semantic, theme-independent colors: ok `0x33FF66`, fail `0xFF4D5E`.
  While the toast is up, one list row yields its place so small
  terminals do not overlap.
- **Stale-read fix found on the way:** Apply no longer writes watt/NV
  defaults just because the live nb-wmi read is 0/stale. A new
  `pw_touched` bitmask tracks the fields the user actually edited;
  writes happen only for touched fields or known-different values —
  staging only an EPP no longer rewrites PPT.
- `make` warning-free (ctron side), `make test` ok, installed to
  `~/.local/bin/ctron`.

## 2026-09-24 — POWER: profile/EPP rows, Enter applies, q guard

- Two new staged rows at the top: **Platform profile**
  (Quiet/Balanced/Performance) and **EPP preference** — EPP finally has
  a TUI editor (was CLI/mode-only). Both ride the same staging: h/l
  stage, one Apply writes everything; the profile write goes first so
  an explicitly staged EPP wins over the profile-implied one.
- **Enter now applies** the staged bundle (like every other panel);
  the old "Enter steps the value up like right-arrow" behaviour is
  gone. h/l and ←/→ remain the only staging keys, `w` stays as an
  apply alias. That also answers "the Apply button has no keyboard
  path": Enter is it.
- **q guard**: quitting with staged edits pending warns once
  ("staged edits pending — q again to quit") and quits on the second
  press; the warning state resets on apply/revert. Works from any
  panel, not just the POWER view.
- `make` warning-free (ctron side), `make test` ok, installed to
  `~/.local/bin/ctron`, `--status` live on FA608PP.

## 2026-09-24 — POWER view follow-ups (critique fixes)

- View-switch keys no longer shadow editor keys: lowercase f/p/l/e
  yield to the active view's bindings (fan editor 'p'/'l', POWER and
  LIGHT 'l'); uppercase always switches. POWER staging answers 'l'
  like 'h', and the fan editor's 'p' (type pwm) and 'l' (nudge) work
  again after being shadowed by the workspace shortcuts.
- Staged watt triples are clamped and ordered at stage time via the
  new shared `ctrl_ppt_order()` (extracted from `ctrl_set_ppt`), so
  the staged numbers are exactly what Apply writes — unit-tested in
  `test_core` (clamp, order, clamp-then-order, passthrough).
- Staging "PPT limits → removed" now stages the platform maxima,
  mirroring `ctrl_ppt_off`, so pending watts never show values that
  cannot apply while the limits are off.
- Entering the POWER view (with nothing staged) runs `hw_refresh_live`
  + restage, picking up external changes (CLI, asusctl) instead of a
  stale "live" column.
- Rows show `value (?)` when the live read is unknown/stale but a
  staged/written value exists (was a bare `--`). Apply logs a one-line
  ok/FAILED summary; the cpu-clock row skips the no-op privileged
  write when no limit is set and staging equals the cpuinfo max.
- `make` warning-free outside `editor_daeboard.c` (its warnings came
  with the daeboard ship, left for the FA507NVR line); `make test` ok;
  `--status` live on FA608PP.

## 2026-09-23 — daeboard editor

- LIGHT view, **b**, opens the macro editor. Down and Up are side by side.
  Presets append. Add key and Change key take the next keypress.
  Del key removes the row. Save writes `daeboard.binds` and reloads.
- `--follow`, `--daeboard-start`, `--daeboard-stop`, `--daeboard-reload`.
- Brightness and static color go through the daemon socket when it is up.

## 2026-09-23 — POWER view: staged edits + Apply/Revert

- h/arrows no longer write per keypress: edits land in `g_ui.pwv_*`
  staging fields and nothing reaches the hardware until `w` / the
  ` Apply ` button. One `ctrl_set_ppt` covers the SPL/SPPT/FPPT triple;
  NV boost/temp, panel OD, CPU boost and clock limit write only when
  changed. PPT-limit toggle rides along (off writes maxima, restore
  then values).
- `r` / ` Revert ` drops staged values back to live. Dirty rows render
  `live → staged ●`; the panel title shows `POWER ●` while pending.
- `pw_sync_from_hw()` re-stages from hw at TUI start, after mode or
  profile apply, and after Apply (read-back verify). Stale nb-wmi reads
  (≤5 W) keep the written values instead of collapsing to defaults.
- Mouse: second click on a row stages/toggles it; Apply/Revert are
  registered click targets. Help view gained a POWER section.
- Key landscape: 'l' remains the workspace LIGHT switch, so staging is
  h / ← → / Enter / Space; ESC remains the settings-overlay key, so
  revert is 'r'. CLI and the CONTROLS panel are unchanged.
- `make` warning-free, `make test` ok, `--status` live on FA608PP.
  Interactive TUI field-test on a real terminal still pending.

## 2026-09-23 — CPU clock limit row in the POWER view

- The TUI POWER view gains a "CPU clock limit" row: h/l (or Enter)
  steps the cpufreq ceiling ±100 MHz through `ctrl_set_cpu_max_mhz`,
  which walks cpu0..cpuN (the per-CPU path from 09-21). Display reads
  the live `scaling_max_freq`; `--` when unknown. CLI `--freq` already
  existed; this is the missing TUI counterpart.

## 2026-09-22 — battery watts on FA507NVR

- `--status`, `--watch`, and the LIVE strip show battery power.
  `power_now` when present, otherwise `current_now` × `voltage_now`.
  Discharging is `dis 31.6W`, charging is `chg 12.0W`.
- Rebuilt and installed to `~/.local/bin/ctron`. On this machine,
  discharging at about 33 W. `--epp` now lands on all 16 CPUs.

## 2026-09-22 — fragile-idiom cleanup (no behaviour change)

- `ut_path_join()` (bounds-checked) replaces the five
  `strcat(strcpy(...))` chains in `hw.c` — sysfs path building can no
  longer overflow.
- New `ui_btn_row()` flow helper replaces hand-counted button offsets
  (`x+9`, `x+17`, ...) in the profiles panel, the fan editor's two rows
  and the mode editor's save row; labels can now change freely and
  buttons that do not fit are skipped instead of overlapping.
- `dash_if()` in `--status`/`--watch`/`--doctor` no longer returns slots
  from a static 4-entry ring; callers pass their own buffers.

## 2026-09-22 — PPT limits toggle (POWER row & --ppt off|on)

- POWER view gains a "PPT limits" row and the CLI gains `--ppt off|on`:
  off remembers the current SPL/SPPT/FPPT and writes the platform maxima
  (the clean equivalent of the profile-flip trick that reset the limits);
  on writes the remembered values back. Session-only state — PPT returns
  to firmware defaults on reboot.
- Makefile: object files now depend on the headers (a stale settings.o
  with the old struct layout corrupted the persist tests).

## 2026-09-22 — settings window (btop-style) + live layout settings

- The settings overlay is now a centred floating window (double frame,
  drop shadow) over the still-rendered main screen — ghost text fixed by
  construction: panels and window each fill their area every frame.
  Click outside the window closes it; mouse hits search newest-first so
  background buttons cannot be clicked through the window.

- Settings opens as a floating window with ESC or 's' (and the
  SET tab / controls row). ESC/s close it; 'q' always quits. The mode
  editor runs inside the window. Input-field ESC cancels keep priority.
- New LAYOUT section in the overlay, applied instantly and persisted:
  swap left panels, telemetry at top, left-column %, left split %,
  telemetry height (`settings.ini`: swap_left/telem_top/left_pct/
  split_pct/telem_h).
- Fixed a v1-era input filter that swallowed lone ESC (`0x1b` == NCKEY_ESC)
  as "stray CSI" — ESC could never reach the UI.
- Mouse: settings rows now dispatch through TGT_PANEL_SETTINGS (previously
  registered as workspace targets that the workspace ignored).

## 2026-09-21 — per-CPU cpufreq writes

- `--freq` / `--epp` wrote only cpu0's cpufreq node ("governor mirrors
  cpu0"). On amd-pstate every CPU has its own policy (`related_cpus` is
  single-member), so `sudo ctron --freq 5386` unlocked one core and left
  the other 31 clamped at base (2.4 GHz). The helper now walks cpu0..cpuN
  and writes each node.

## 2026-09-21 — Refresh row starts on the live rate

- CONTROLS → Refresh row opened on the first mode (60) regardless of the
  live rate. It now starts on the mode closest to `hz_cur` (e.g. 165).

## 2026-09-21 — tr_TR locale fix (Kerempkl, FA608PP)

- TUI pinned `LC_NUMERIC` to "C" after `setlocale(LC_ALL, "")`. Under
  tr_TR (decimal comma) `strtod("59.87")` returned 59, so the refresh list
  became [59,60,164,165] and `--hz`/TUI refresh applied "59 Hz" → FAILED.
  Verified in a tr_TR pty: TUI Enter switches 165→60 and back.

## 2026-09-20 — FA507NVR / Hyprland 0.55

- Hyprland backend parses `hyprctl monitors -j` **array** JSON (`name`, `refreshRate`, `availableModes`; no `currentMode` wrapper).
- `set_hz` uses `hyprctl eval 'hl.monitor({...})'` (0.55 Lua). `keyword monitor` is a no-op and was reported as success. Position/scale copied from JSON. Legacy keyword kept as fallback.
- `--hz 60` / `--hz 144` verified on eDP-1, restored 144.
- PPT stale `5` prints `--` (was `0`).
- `make test` / `--doctor` / `--status` on NixOS 26.05 FA507NVR.

## 2026-09-19 — ctron v2 (Kerempkl)

Modular TUI + CLI. See `README.md` / `PLAN.md`. Target FA608PP / KDE.

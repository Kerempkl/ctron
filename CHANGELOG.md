# Changelog

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

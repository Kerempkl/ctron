# Ctron Handoff

## Signed 2026-09-23 — Grok, FA507NVR, NixOS

Status: **installed** at `~/.local/bin/ctron`. Not left running.

Last ship: LIGHT view, **b**, opens the daeboard editor. Down and Up are side by side. Presets append. Add key and Change key wait for a real keypress; Change key keeps the steps. Del key removes the row. Del on a step removes the step. Start, Stop, Install, Save. `--follow`, `--daeboard-start`, `--daeboard-stop`, `--daeboard-reload`. While the daemon answers ping, brightness and static color use the socket. Other aura effects are refused.

Known issues:

- Quit any ctron that was already open before using this binary. An old process will save a chopped binds line.
- `key6` in `~/.config/ctron/daeboard.binds` is not a real key. Del key it, or Change key it.
- A step cannot be moved from Down to Up.
- Install on this NixOS machine starts the daemon. It does not run `install.sh`.
- Battery watts (2026-09-22, previously uncommitted) are in this same tree: `--status`, `--watch`, and the LIVE strip.

Resume: open ctron, LIGHT, **b**, Save after edits. Plan text is `PLANS.md`.

Latest earlier session: **2026-09-22** (FA608PP / KDE / CachyOS, with the ZCode
"GLM" agent). Pulled to `68de7e6` on FA507NVR, then rebuilt and installed
to `~/.local/bin/ctron` (still reports `2.0.0-deno`). Previous sessions below.

## FA507NVR — 2026-09-22, installed build

- Battery power is in `--status`, `--watch`, and the LIVE strip.
  This pack has no `power_now`; watts are `current_now` × `voltage_now`.
  Sample while unplugged: `BAT 41% Discharging dis 33.6W`.
- Retest on that binary, then restored to the state found at the start
  of the run (not Quiet): profile **Performance**, EPP `performance` on
  all 16 CPUs, keyboard off. `--epp balance_performance` reached all 16
  CPUs and restored. `--kbd low` → brightness `1` → off.
  `--profile balanced` swapped in the Balanced fan curve and
  `balance_power` on all 16; `--profile performance` put both back.
- PPT was already `60/75/75` before this run and was not written.
  Readback works. Do not run `--ppt off` here: no `asus-armoury`, so the
  generic ceiling is 90/120/120 W.

## FA507NVR — 2026-09-22, old binary

Checked on the 2026-09-20 binary, then restored:

- `--kbd low` → brightness `1`, restored `off`.
- `--profile balanced` loads that profile's fan curve and sets EPP
  `balance_power` on all 16 CPUs. `--profile quiet` put both back.
  Quiet curve: CPU `42,44,49,63,65,67,80,86` / `0,31,63,127,159,191,223,255`,
  GPU `40,42,43,60,65,69,74,78` / `5,20,38,43,255,255,255,255`, both on.
- `--epp` on that binary wrote cpu0 only. `68de7e6` walks every CPU;
  not rebuilt here yet.
- Do not run `--ppt off` on this machine until the ceiling is checked.
  No `asus-armoury`, so the new toggle's maxima are the generic
  90/120/120 W, above the old FA507 80 W cap. Live PPT still reads `--`.

## Session 2026-09-22 — settings overlay + live layout

- ESC (or 's') opens a centred floating **settings window** (double frame, drop shadow, main screen still visible behind it); ESC/s close, 'q' always quits. Click outside closes it too. Mode editor runs inside it. Lone-ESC swallowing filter
  removed (0x1b == NCKEY_ESC).
- New LAYOUT rows in the overlay, applied instantly and persisted:
  swap_left, telem_top, left_pct, split_pct, telem_h. Placement is fully
  data-driven from g_prefs in `ui_layout()`.
- Also: per-CPU cpufreq writes (`--freq`/`--epp` walk cpu0..cpuN — the
  cpu0-only write left 31 cores clamped at 2.4 GHz), 2.4 GHz clamp
  diagnosis (see CHANGELOG 09-21), Refresh row starts on the live rate.
- `~/.local/bin/ctron` is built from this tree (v1 backed up as
  `ctron.v1.bak`).
- Uncommitted in the worktree at handoff time: the 09-22 overlay/layout
  changes plus these doc updates — commit as one unit.

## Session 2026-09-21 — v2 bugfixes on FA608PP

**Repo state:** `main = 52ea4ce`, synced with GitHub. This is the single
source of truth — the scratch copies under `~/Projeler/glm deneme`
(`ctron deneme`, `ctron 2.1`) are now redundant snapshots; do all further
work here in the repo.

### What landed

- **PPT limits toggle** (POWER row + `--ppt off|on`): off remembers the
  current SPL/SPPT/FPPT and writes the platform maxima — the clean
  equivalent of the profile-flip trick that reset the EC limits. on
  restores the remembered values. Session-only (PPT is firmware-default
  after reboot). Makefile object files now depend on headers.
- **tr_TR locale fix** (`src/ui/ui.c`): the TUI calls `setlocale(LC_ALL, "")`
  and on the Turkish locale `strtod("59.87")` stopped at 59 (decimal comma),
  corrupting the refresh list to `[59,60,164,165]`. Applied modes then tried
  "59 Hz" and logged FAILED. Fix: `setlocale(LC_NUMERIC, "C")` right after.
  Symptom to remember: ghost 59/164 Hz values, only inside the TUI.
- **Refresh row init** (`src/ui/ui.c`): the CONTROLS row opened on the first
  mode (60) regardless of the live rate; it now starts on the mode closest
  to `hz_cur` (e.g. 165).
- **kscreen-doctor writer** (`src/display/display_kde.c`): float refresh
  (`@60.00`) is rejected as "Unable to parse arguments" while still exiting
  0. We now send integer refresh (`@60`) and verify by reading the mode
  back instead of trusting the exit code.
- `~/.local/bin/ctron` on FA608PP is built from this tree; the old v1
  binary is kept at `~/.local/bin/ctron.v1.bak`.

### Verified this session

`make` warning-free, `make test` ok, `--doctor`/`--status` live on
FA608PP (asusctl 6.5, asus-armoury, k10temp, nvidia-smi, fan rpm, kde
backend with 60/165 modes), `--hz 60` ↔ `--hz 165` end-to-end, TUI smoke
under a pty with a terminal-query responder (clean `q` exit 0). TUI
Refresh row verified under `LC_ALL=tr_TR.UTF-8`.

### Still open

1. Version string still says `2.0.0-deno` — bump to `2.1`, tag `v2.1`.
2. From the FA507NVR session: HDMI-A-1 Hz (multi-monitor `hl.monitor`),
   TUI mouse under Hyprland.
3. Parked: Waybar sync, MUX/dGPU, throttle_thermal_policy.

---

## Session 2026-09-20 — FA507NVR / Hyprland 0.55

v2 (`888dc12`) plus FA507NVR Hyprland fixes.

**FA507NVR / NixOS / Hyprland 0.55.4:** `make test` ok. `--doctor` sees ASUS TUF A15, k10temp, nvidia-smi, asusctl, fan hwmon, **display hyprland**. `--status` live. `--hz 60` / `--hz 144` applied and restored.

Kerempkl verified FA608PP / KDE. This box is the Hyprland sibling.

Paths: project `~/Documents/ctron`, GitHub `github.com/Kerempkl/ctron`,
binary `~/.local/bin/ctron`, config `~/.config/ctron/`, build via
`nix-shell -p gcc pkg-config notcurses gnumake --run make`.
No-args: fullscreen TUI (v2). Flags: headless.

Machine differences: A15 FA507NVR, 7435HS + RTX 4060, kernel 6.18,
Hyprland Lua config. No `asus-armoury`. PPT sysfs is `5` → `--`. GPU fan
RPM often 0 at idle → `--`. Hyprctl JSON is a raw array; `keyword
monitor` is a no-op (Lua parser). v2 uses `hl.monitor({...})` via
`hyprctl eval`.

Not field-tested there: TUI click-through, FAN Write, PPT presets,
`--mode`, Aura. `--hz` was tested (60 then 144).

Todo from that session: TUI on that terminal (mouse / FAN Write, restore
Quiet curve if EC is touched); HDMI-A-1 Hz; after both laptops are on v2:
daetron, Waybar sync. Parked: Metatron, Gentoo ESP, 140 W, kernel bump.

Signed:

- **Arcioth** — FA507NVR / Hyprland
- **Kerempkl** — v2 rewrite, FA608PP / KDE
- **Grok 4.6 (xAI)** — Hyprland 0.55 JSON + `hl.monitor` eval

*Date: 2026-09-20*

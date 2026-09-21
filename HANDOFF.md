# Ctron Handoff

Latest session: **2026-09-22** (FA608PP / KDE / CachyOS, with the ZCode
"GLM" agent). Previous sessions below.

## Session 2026-09-22 — settings overlay + live layout

- ESC (or 's') opens a fullscreen **settings overlay**; ESC/s close, 'q'
  always quits. Mode editor runs inside it. Lone-ESC swallowing filter
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

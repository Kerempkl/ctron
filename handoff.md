# Ctron Handoff

Created 2026-09-03 by Arcioth & Kerempkl. Session close **2026-09-04**.

## Status

**Software gate: passed.** Live on ASUS TUF Gaming A15 **FA507NVR** (Ryzen 7 7435HS + RTX 4060), kernel **6.18**, NixOS.

512² Kitty applet (`~/.local/bin/ctron`). Tabs `PRF PWR FAN AUR ACV SLT`. Fan editor is an **X=°C, Y=pwm** plane (1–8 points, graph readout, click to place, **Write** to EC). Waybar Sync OFF is a real no-op (`/tmp/acv-waybar-sync.off`). Edge tab-switch is **chronic / parked**.

Do not bloat. Next work is the arclinkdae bind, not another TUI pass, unless Arcioth names persist-fans first.

## Paths

| | |
|---|---|
| Project | `~/Documents/ctron` |
| Binary | `~/.local/bin/ctron` (`vhelper`, `tufhelper`) |
| Settings | `~/.config/vhelper/settings.ini` |
| Profiles | `~/.config/vhelper/profiles/*.acv` |
| Waybar sync script | `~/.config/hypr/scripts/kb-waybar-sync.sh` (NixOS copy: `~/Documents/nixos/files/hypr/scripts/kb-waybar-sync.sh`) |
| Build | `cd ~/Documents/ctron && nix-shell -p gcc pkg-config notcurses gnumake --run make` then `install -m 755 build/ctron ~/.local/bin/ctron` |

No-args spawn = floating 512². `--tui` = current terminal.

## Driver table (FA507NVR / 6.18)

| in TUI | later / never |
|---|---|
| Quiet / Balanced / Performance, EPP, freq cap, Tctl software cap | `asus-armoury` empty until kernel 6.19 |
| PPT Q45/B60/P80 — AC 15–80 / DC 15–65 W (ASUS Crate) | `nvidia-smi -pl` 140 W **not exposed** |
| NV boost 5–25 W, NV temp 75–87 °C | `throttle_thermal_policy` overlaps profiles |
| 60/144 Hz, battery 20–100 + oneshot, panel OD, CPU boost | |
| Aura, 2 tint modes, SLOT `.acv` | |
| Fan presets + 1–8 point X,Y editor + Write | persist curve in settings (todo) |

`asus-nb-wmi` PPT/nv_* are root-owned (`sudo tee`, same as freq). Sysfs PPT `5` is stale kernel cache — UI shows `--` until written.

## Todo

1. **Bind arclinkdae → ctron** (next). Spare USB plug, uevent wakes `arclinkdae`, spawn 512² applet, measure RSS/CPU vs the old 2s bash pollers. Rules live in `~/Documents/arclinkdae`. Do this after a short field check that FAN Write still holds.
2. **Persist fan X,Y** into `settings.ini` and `.acv`. Today only ON/OFF is saved; the curve lives in the EC until reboot/profile change unless Write was used.
3. **`asus-armoury` on 6.19** — dual-path is already in the binary; do not bump kernel for it (NVIDIA 595 already forced a 7.2 revert).
4. Optional, low: extra EPP/governor rows. Do not add unless asked. Do not expose 140 W.

Parked until named: Metatron / Uriel / `~/Documents/thermal-p2` (Pi at school). Gentoo ESP / GAMES partition. Edge tab-switch.

## Known limitations

- **Edge tab-switch is chronic.** Leave/drag on the 512² chrome still flips tabs sometimes. Press-only + inset hitboxes + pixel mis-map fix were tried. Parked.
- **Fan curve is not in settings.** Restart of the applet re-reads hwmon (8 kernel points). Custom 1–7 point edits are in-memory until **Write**; kernel always stores 8 hwmon slots (we pad with the last point).
- **Same-temp points get +1 °C.** Sort-by-X plus unique temps so dots do not stack and vanish. Hardware also wants one pwm per temp.
- **Graph is ~40×7 cells.** Not Armoury Crate CAD. Kitty 144 Hz ≠ 144 fps TUI.
- **Waybar CPU/VOC/IMEX still tick every 2s.** That is module `interval`. Color sync is CSS + `USR2`. OFF means no CSS write and no `USR2`.
- **Tctl cap is software** (`scaling_max_freq` steps). No `ryzenadj` on PATH.
- **PPT live read is unreliable** until a write. Do not treat sysfs `5` as 5 W.
- **Root for WMI writes.** Same sudo path as before. Do not chmod the sysfs.
- **Kernel 6.18** — no live `asus-armoury` attributes.

## Controls (short)

`1`–`6` tabs. `H`/`L` prev/next. `j`/`k` focus. `h`/`l` nudge (on FAN: X if the X field is focused, else Y). FAN: type X, Tab/comma, type Y, Set; `+`/`-`; click graph to place; Write. `q` saves settings and exits.

## Sign-off — 2026-09-04

Software gate closed. Next session starts at todo item 1 (arclinkdae bind) unless Arcioth says persist-fans first.

Signed:

- **Arcioth** — System Architect & User
- **Kerempkl** — Original Architecture & Blueprint
- **Grok 4.6 (xAI)** — this session (FAN X,Y, Waybar sync-off, handoff)
- **Antigravity (Google DeepMind)** — earlier pair work (mouse, PPT, SLOT, tint)

*Date: 2026-09-04*

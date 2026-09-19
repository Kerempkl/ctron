# Ctron Handoff

v2 (`888dc12`) plus FA507NVR Hyprland fixes. Session **2026-09-20**.

## Status

**FA507NVR / NixOS / Hyprland 0.55.4:** `make test` ok. `--doctor` sees ASUS TUF A15, k10temp, nvidia-smi, asusctl, fan hwmon, **display hyprland**. `--status` live. `--hz 60` / `--hz 144` applied and restored.

Kerempkl verified FA608PP / KDE. This box is the Hyprland sibling.

## Paths

| | |
|---|---|
| Project | `~/Documents/ctron` |
| GitHub | `https://github.com/Kerempkl/ctron` |
| Binary | `~/.local/bin/ctron` |
| Config | `~/.config/ctron/` |
| Build | `nix-shell -p gcc pkg-config notcurses gnumake --run make` |

No-args: fullscreen TUI (v2). Flags: headless.

## This machine vs Kerempkl’s

- A15 FA507NVR, 7435HS + RTX 4060, kernel 6.18, Hyprland Lua config.
- No `asus-armoury`. PPT sysfs is `5` → `--`.
- GPU fan RPM often 0 at idle → `--`.
- Hyprctl JSON is a raw array; `keyword monitor` is a no-op (Lua parser). v2 now uses `hl.monitor({...})` via `hyprctl eval`.

## Not field-tested here

TUI click-through, FAN Write, PPT presets, `--mode`, Aura. `--hz` was tested (60 then 144).

## Todo

1. TUI on this terminal (mouse / FAN Write). Restore Quiet curve if EC is touched.
2. HDMI-A-1 Hz (second `hl.monitor` in hyprland.lua).
3. After v1-on-both-laptops: daetron, Waybar sync (parked in v2 README).

Parked: Metatron, Gentoo ESP, 140 W, kernel bump.

Signed:

- **Arcioth** — FA507NVR / Hyprland
- **Kerempkl** — v2 rewrite, FA608PP / KDE
- **Grok 4.6 (xAI)** — Hyprland 0.55 JSON + `hl.monitor` eval

*Date: 2026-09-20*

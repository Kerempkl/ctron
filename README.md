# Ctron (Arcioth & C & Kerempkl Helper)

Custom ASUS TUF Gaming Control Center written in **pure C** with the **Notcurses** toolkit.
Designed specifically as a **512×512px floating desktop applet** for Hyprland / Wayland.

## Features

- **Platform Profiles**: Quiet, Balanced, Performance via `asusctl` & ACPI `platform_profile`.
- **CPU & EPP**: Power, Balanced Power, Balanced Performance, Performance + frequency capping + optional Tctl cap (software throttle).
- **Display Refresh**: One-click switching between 60 Hz power save and 144 Hz high refresh rate via `hyprctl`.
- **Battery Health**: Limit battery charging to 60%, 80%, or 100%.
- **Aura RGB Backlight**: 12 animation effects, 8 color presets, and 4 brightness levels.
- **Hardware Telemetry**: Real-time 250ms polling of CPU temperature, frequency, and battery status.
- **Driver Support**: Dual-path driver support for `asus-armoury` and `asus_wmi` / `asusctl`.

## Quick Start

```bash
# Launch the 512x512 floating applet:
./run.sh

# Or run from anywhere:
ctron
# (or vhelper / tufhelper)
```

## CLI Usage

```bash
ctron --status
ctron --profile Performance
ctron --hz 144
ctron --battery 80
ctron --epp balance_performance
ctron --tctl 85
```

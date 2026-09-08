# ctron — Agent Guidelines

ASUS TUF control program (AMD Ryzen + NVIDIA), verified on TUF A15 FA507NVR. **Ctron** (Arcioth & C & Kerempkl): C + Notcurses. CLI is the program; `--tui` is optional.

## Key Rules

1. **CLI first**:
   - `ctron` is a headless ASUS TUF tool. No-args does not spawn a GUI.
   - `--tui` is optional, current terminal, always linked.
   - Config: `~/.config/ctron` (`CTRON_CONFIG` / `--config-dir`). Profiles `*.ctr`.
2. **Quit TUI**:
   - `q` / `Q` only (not ESC). Saves `settings.ini`.
3. **Driver Hierarchy**:
   - **`asus-armoury`**: Checked first at `/sys/class/firmware-attributes/asus-armoury` and via `asusctl armoury`. When present on kernel 6.19+, exposed for direct firmware control.
   - **`asus_wmi` + `asus_nb_wmi`**: Active default on the machine's running Linux 6.18 LTS kernel. Backed by `asusctl` daemon and `/sys/firmware/acpi/platform_profile`.
   - **Fallback Sysfs**: Always support direct kernel sysfs (`/sys/class/power_supply/BAT*/charge_control_end_threshold`, `/sys/class/leds/asus::kbd_backlight`, `/sys/devices/system/cpu/cpufreq`).
4. **Display Control**:
   - Detects compositor: Hyprland (`hyprctl keyword monitor`), Sway/wlroots (`wlr-randr`), KDE (`kscreen-doctor`), X11 (`xrandr`).
5. **Portability**:
   - Link only notcurses. Detect asusctl, compositor tools, ryzenadj, sysfs nodes at runtime. Grey out missing rows.
   - `Makefile` is the build. flake.nix wraps it. Do not add cmake, extra UI kits, or a `.ko`.
   - daetron is shelved for v1. Do not `apply.sh` for SMU. Do not drive SMU PPT; ASUS WMI already has watts.

## Build & Run

```bash
cd ~/Documents/ctron
make
./build/ctron --status
./build/ctron --tui
```

Or via Nix flake:
```bash
nix run .#ctron
```

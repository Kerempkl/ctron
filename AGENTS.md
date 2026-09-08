# ctron — Agent Guidelines

Desktop 1:1 floating TUI applet for **ASUS TUF Gaming** laptops (specifically verified on TUF A15 FA507NVR with AMD Ryzen 7 7435HS and RTX 4060).
Dubbed **Ctron** (Arcioth & C & Kerempkl) — built in pure C with Notcurses, based on Kerempkl's original C++ blueprint and modernized for Wayland / Hyprland.

## Key Rules

1. **Applet Architecture**:
   - Strictly sized as a 512×512px floating window applet.
   - Terminal runner: `kitty --class ctron_widget --title "Ctron" -o initial_window_width=512 -o initial_window_height=512`.
   - Hyprland window rule: `class = ".*(ctron|vhelper).*"` floats at 512×512, centered with popin animation.
2. **Zero-RAM Idle**:
   - Quits instantly on `q` or `ESC` or mouse close; saves active state to `~/.config/vhelper/settings.ini`.
3. **Driver Hierarchy**:
   - **`asus-armoury`**: Checked first at `/sys/class/firmware-attributes/asus-armoury` and via `asusctl armoury`. When present on kernel 6.19+, exposed for direct firmware control.
   - **`asus_wmi` + `asus_nb_wmi`**: Active default on the machine's running Linux 6.18 LTS kernel. Backed by `asusctl` daemon and `/sys/firmware/acpi/platform_profile`.
   - **Fallback Sysfs**: Always support direct kernel sysfs (`/sys/class/power_supply/BAT*/charge_control_end_threshold`, `/sys/class/leds/asus::kbd_backlight`, `/sys/devices/system/cpu/cpufreq`).
4. **Display Control**:
   - Detects compositor: Hyprland (`hyprctl keyword monitor`), Sway/wlroots (`wlr-randr`), KDE (`kscreen-doctor`), X11 (`xrandr`).

## Build & Run

```bash
cd ~/Documents/ctron
make
./run.sh
```

Or via Nix flake:
```bash
nix run .#ctron
```

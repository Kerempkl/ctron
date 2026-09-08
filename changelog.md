# Changelog

## 2026-09-04 — Session close (signed)

- Software gate passed. Handoff signed. Next: arclinkdae → ctron USB wake, then optional fan-curve persist.
- FAN is X=°C / Y=pwm (click graph or type both). Waybar Sync OFF is a no-op. Edge tab-switch parked as chronic.
- Known limits and the todo list live in `HANDOFF.md`.

## 2026-09-04 — Fan X,Y plane

- Each point is **X=°C, Y=pwm**. Two fields, not one jammed value. Click empty graph to drop a point at that cell; click a ● to select.
- `+` uses the current X,Y. Points are sorted by X and given unique temps so they cannot stack on the last cell and vanish.
- `-` can clear down to 0. Neighbor clamp that froze temperature is gone.

## 2026-09-04 — Fan coordinates, Waybar sync-off, edge mouse

- FAN: no drag. 1–8 points via `+`/`-`. Type `temp,pwm` (also `60c:80`) and **Set**. Graph is a readout; click a ● or a `n:temp,pwm` chip to select.
- Waybar Sync OFF writes `/tmp/acv-waybar-sync.off`. `kb-waybar-sync.sh` idles (no CSS, no USR2). `hw_sync_waybar_color` is a no-op while the flag exists. NixOS copy no longer `pkill waybar`.
- Edge tab-switch: first BUTTON1 press only; ignore drag/repeat/release; ignore frame cells; do not treat 1-based last-cell as pixels.

## 2026-09-03 — 3-letter tabs, FAN graph, de-bloat

- Tabs: `PRF PWR FAN AUR ACV SLT`. SYS merged into footer (last log line).
- POWER no longer holds fans. New FAN tab: 9-row pwm-vs-°C plot, drag ●, presets, Write.
- Drag poll 16 ms; commit curve on mouse release (not every motion).
- Not a CAD plot: ~40×9 cells. Feasible; 144 Hz panel ≠ 144 fps TUI.

## 2026-09-03 — Fan 8-point editor; mouse verified

- POWER: node editor CPU/GPU, point 1–8, T± / P±, Write. Temps stay monotonic 20–105 °C, pwm 0–255.
- Tint h/l clamped to Clear Glass / Solid Opaque only.
- Mouse confirmed on live Kitty.

## 2026-09-03 — Mouse fix + ASUS-safe PPT/NV knobs

- **Mouse:** button clicks never fired (`continue` skipped action exec). Also map pixel mouse coords to cells.
- **PPT** (FA507 Armoury Crate): AC SPL/SPPT/FPPT 15–80 / 35–80 / 35–80 W; DC max 65. Presets Q45 / B60 / P80 (DC 35/45/65). Sysfs `5` is stale kernel cache — UI shows `--` until written.
- **NV dynamic boost** 5–25 W, **NV temp** 75–87 °C (ASUS table). No nvidia-smi 140 W.
- Panel OD, CPU cpufreq boost, battery oneshot. Shift-Tab via `ni.shift`.

## 2026-09-03 — Fan curves + arbitrary battery %

- POWER tab: CPU/GPU fan curve from `asus_custom_fan_curve` (8-point). Presets Silent / Stock / Cool / Full, ON/OFF. Writes sysfs + `asusctl fan-curve` for the active platform profile.
- Battery: 60/80/100 plus `[-5]`/`[+5]` (asusctl already allowed 20–100). `h`/`l` on POWER steps 5%.
- CLI: `ctron --fan stock|silent|cool|full|on|off`. `--status` shows fan hwmon.

## 2026-09-03 — Tctl temperature cap

- PERF tab: `Tctl Cap: NN°C` with `ON/OFF`, `[-5]` / `[+5]` (70–105°C). Live line shows `THROTTLE` when applied max is below the user freq cap.
- No `ryzenadj` on this machine. Cap drops `scaling_max_freq` by 200 MHz steps while k10temp ≥ cap, restores when 4°C under. If `ryzenadj` is later on PATH, `--tctl-temp` is used as well.
- CLI: `ctron --tctl 85` / `--tctl 0` to disable. Saved in `settings.ini` and `.acv` `[perf]`.
- `h`/`l` on the Tctl row adjust the cap; Space on `ON/OFF` toggles.

## 2026-09-03 — Custom .acv Profiles, Keyboard Hex Entry, Inside Window Tint & Backlight Sync Fix

- **Fix Mouse Out Tab Switch**: Permanently eliminated the border tab-switch bug by removing the `'['` / `']'` keybindings (which intercepted Kitty's `\033[O` FocusOut escape sequence on mouse leave), discarding stray CSI bytes, and restricting mouse actions strictly to inner-coordinate `NCKEY_BUTTON1` clicks.
- **Custom Keyboard Hex Entry**:
  - Replaced the spectrum wheel with the classic color presets/prefixes: `[Ice] [Red] [Grn] [Blu] [Gold] [Pnk] [Org] [Wht]`.
  - Added direct keyboard hex input field `[ #______ ]`: type hex chars `0-9, a-f`, Backspace, and Enter to apply immediately to keyboard backlight and Waybar.
- **Backlight Match & Sync Fix**:
  - Resolved the black screen issue by reading real backlight RGB directly from `/tmp/waybar-kb-hex` and `/etc/asusd/aura_tuf.ron`.
  - Added luminance scaling to ensure text is always high-contrast white (`0xFFFFFF`) with tinted borders and backgrounds (never black-on-black).
- **Inside Window Transparency & Tint**:
  - Implemented interior window transparency via Notcurses `NCALPHA_TRANSPARENT`.
  - Added 5-step controllable Tint Level (`Clear Glass`, `Soft`, `Med`, `Dark`, `Solid`) allowing wallpaper blur to shine through the window interior.
- **Custom `.acv` Profiles (Import & Export)**:
  - Unlimited profile slots stored in `~/.config/vhelper/profiles/*.acv`.
  - Automatic 3-letter random tag generation (e.g. `GAM`, `TUF`, `ECO`, `ARC`, `VGZ`) with direct in-TUI 3-letter keyboard editing.
  - Section checkboxes to selectively export or import (`[x]Perf`, `[x]Power`, `[x]Aura`, `[x]Theme`) or skip filters to export/import all.
- **Waybar & Backlight Sync Controls**:
  - Added direct toggle buttons for **Waybar Sync: [ON/OFF]** and **Backlight Sync: [ON/OFF]** to Tab 5 (`5:ACV`) under `INTEGRATIONS & SYNC`.
  - Added immediate **Waybar Sync: [ON/OFF]** toggle under `Match ACV Theme to Backlight` on Tab 3 (`3:AURA`), updating `~/.config/waybar/colors.css` and triggering immediate reload upon activation.

- **Fix Tab Change on Edge Hover**: Completely resolved border cursor triggering by strictly filtering for `NCKEY_BUTTON1` (left-click) and enforcing strict interior-coordinate boundary checks (ignoring all edge/scroll noise).
- **Comprehensive Vim Controls**:
  - `j` / `k` / `Down` / `Up`: Navigate vertically between interactive control rows.
  - `h` / `l` / `Left` / `Right`: Decrement/increment active values (Hue, CPU limit, sliders) or move horizontally.
  - `H` / `L` (Shift+H / Shift+L) or `[` / `]`: Quick-switch tabs left/right.
  - `1` .. `5`: Direct jump to any of the 5 tabs.
  - `Space` / `Enter`: Activate/toggle focused buttons.
  - `Tab` / `Shift-Tab`: Cycle focus forward/backward.
- **5th Tab Added: `5:ACV`**:
  - **Theme Presets**: Switch applet color scheme between *TUF Ice*, *TUF Tactical Amber*, *Cyber Emerald*, *Reze Crimson*, *Stealth Mono*, and *Backlight Live Match*.
  - **Transparency Protocols**: Real-time opacity switching (100% Solid, 90% Subtle, 80% Glass, 65% Translucent) applied via terminal OSC 11 escape sequences and Hyprland `hyprctl setprop active opacity`.
  - **Keyboard Backlight Sync**: Automatically reflects the active keyboard color onto Ctron's UI accent.
  - **Waybar Sync**: Directly pushes the current Aura hex code into `~/.config/waybar/colors.css` and `/tmp/waybar-kb-hex`, triggering Waybar's instant color re-theme without 8-second polling delay.
- **Visual RGB Color Spectrum Ribbon / Wheel**:
  - Engineered a 24-step Unicode truecolor chromatic spectrum bar across 360° hues with live pointer `▲`.
  - Full HSV steppers (`Hue ±15°`, `Sat`, `Val`) with real-time swatch preview `[ ████ ]` and hex display `#RRGGBB`.
  - Direct 1-click application to ASUS Aura keyboard and ACV theme.
- **Clean Terminal Output**:
  - Configured Notcurses with `NCLOGLEVEL_SILENT` to completely eliminate terminfo and CSI parse diagnostic warnings.
  - Added `2>/dev/null` in `run.sh` to cleanly suppress Kitty's internal GLFW notification service lookup.

## 2026-09-03 — Edge Cursor & Mouse Event Fix

- **Fix Cursor Boundary Exit**: Fixed issue where moving mouse cursor to the edge of the Kitty window sent unrecognized boundary escape codes that triggered an accidental `NCKEY_ESC` quit.
- Switched mouse event mask from `NCMICE_ALL_EVENTS` (which streamed motion sequences on every hover) to `NCMICE_BUTTON_EVENT` (captures clicks cleanly without cursor hover overhead).
- Removed raw `NCKEY_ESC` from the quit trigger; applet now exits on explicit `'q'` / `'Q'` or close.
- Fixed `notcurses_get()` deadline calculation to use `clock_gettime(CLOCK_MONOTONIC)` absolute deadlines.

## 2026-09-03 — Initial Release of ctron

- **Genesis**: Re-engineered Kerempkl's original C++/FTXUI blueprint into pure C with Notcurses for Arcioth's ASUS TUF Gaming A15 (FA507NVR).
- **Format**: Converted from full-screen CLI to a dedicated 512×512px floating desktop TUI applet.
- **Drivers & Hardware Layer**:
  - Integrated `asus-armoury` driver discovery and firmware-attributes getter/setter (`--armoury-get`, `--armoury-set`).
  - Seamless fallback to `asus_wmi` + `asusctl` on the current Linux 6.18 kernel.
  - ASUS platform profiles (Quiet, Balanced, Performance) via `asusctl` and ACPI `platform_profile`.
  - AMD EPP (Energy Performance Preference) and CPU frequency capping via sysfs.
  - Display refresh rate switching: native Hyprland (`hyprctl keyword monitor`) with fallbacks for Sway, KDE, and X11.
  - Battery charge health limit (60%, 80%, 100%) via `asusctl battery` and `charge_control_end_threshold`.
  - ASUS Aura RGB lighting engine with 12 effects and 8 color presets + keyboard backlight brightness control.
- **Interaction**:
  - Full keyboard navigation (`1-4` for tabs, `Tab/Shift-Tab`, Arrows, `Enter`, `q` to quit).
  - Native Notcurses mouse click support across all buttons and tabs.
  - Live 250ms telemetry polling for CPU temperature, active clock speed, battery capacity, and charging state.
- **Ecosystem Integration**:
  - Hyprland window rule added for class `.*(ctron|vhelper).*` to float at 512×512 centered.
  - Desktop entries installed to `~/.local/share/applications/{ctron,vhelper}.desktop`.
  - Binaries installed to `~/.local/bin/{ctron,vhelper,tufhelper}`.
  - Standalone `flake.nix` declared with default package, app, and devShell.

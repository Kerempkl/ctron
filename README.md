# ctron v2

Lightweight ASUS laptop control center for Linux. One C binary, one
dependency ([notcurses](https://github.com/dankamongmen/notcurses)).

- **No arguments** → fullscreen TUI (in your current terminal).
- **Any argument** → headless CLI, does the job, exits. Script/ssh friendly;
  with redirected output it never opens the TUI.

Verified on an **ASUS TUF Gaming A16 FA608PP** (Ryzen 9 8940HX + RTX 5070
Max-Q, CachyOS, KDE Plasma Wayland, kernel 7.2): CPU/GPU temps, fan RPM,
custom fan curves, platform profiles, EPP, battery charge limit, keyboard
backlight, PPT, panel overdrive and display refresh (via kscreen-doctor)
all read live on this machine.

---

## Build

```bash
# Arch          sudo pacman -S base-devel notcurses
# Fedora        sudo dnf install gcc make pkgconf notcurses-devel
# Debian/Ubuntu sudo apt install build-essential pkg-config libnotcurses-dev
# Nix           nix-shell -p gcc pkg-config notcurses gnumake
make
./build/ctron --status
```

`make install` copies the binary to `~/.local/bin/ctron`.

Runtime tools are probed, never linked: `asusctl`, `nvidia-smi`,
`kscreen-doctor` / `hyprctl`. Missing ones simply grey out their rows.

## Quick start (Türkçe)

```bash
ctron                    # tam ekran arayüz (TUI)
ctron --status           # tüm donanım durumunu yaz, çık
ctron --watch            # canlı tek satır telemetri (Ctrl-C durdurur)
ctron --doctor           # neler destekleniyor raporu
ctron --mode turbo       # hazır ayar paketi uygula
ctron mode list          # paketleri listele (modes.ini)
ctron --profile quiet    # tekil komutlar da çalışır
ctron --battery 80
```

TUI: `1-4` panel odaklanır · `j/k` gez · `h/l` değer değiştir · `Enter`
uygular · `ESC` (veya `s`) tam ekran ayarlar · `?` yardım · `q` kaydet ve çık.
Ayarlar > LAYOUT bölümünden panellerin yerleri/oranları anında değişir. Sol kolon profiller
ve hızlı kontroller, sağ üstte fan eğrisi editörü, sağ altta sürekli canlı
telemetri.

## CLI reference

```
--status | --watch | --doctor | --setup | --version | --help
--tui                                  force the TUI

--mode <name>                          apply a shortcut bundle
mode list|show <name>|add <name> <steps>|delete <name>
profile list
profile apply|export|delete <name>

--profile quiet|balanced|performance
--epp power|balance_power|balance_performance|performance
--freq <mhz>                CPU max frequency
--hz <rate|max>             display refresh
--battery <20..100>         charge limit
--battery-oneshot           charge to full once
--ppt Q45|B60|P80|off|on|<spl>,<sppt>,<fppt>   off = remove limits (platform max), on = restore
--nv-boost <5..25>          NVIDIA dynamic boost (W)
--nv-temp <75..87>          NVIDIA temp target (°C)
--panel-od on|off           panel overdrive
--cpu-boost on|off          cpufreq boost
--kbd off|low|med|high      keyboard backlight
--aura <effect> [color|hex]
--fan stock|silent|cool|full|on|off
--fan-curve cpu|gpu <temps> <pwms>    e.g. --fan-curve cpu 40,60,80 80,140,220
--fan-write                 write the in-memory curve to the EC

--config-dir DIR            config override (also $CTRON_CONFIG)
```

Modes ("CLI shortcuts") live in `~/.config/ctron/modes.ini`:

```ini
[turbo]
steps = profile performance, ppt P80, fan cool, hz max
```

Edit them from the TUI (**s** → CLI SHORTCUTS) or with `ctron mode add`.

## Writing to hardware — privilege model

Every write is a user action (nothing is written from the poll loop) and
goes through one chain:

1. **asusctl** when installed (no root needed),
2. else a **direct sysfs write** (works where the node is writable),
3. else `sudo -n tee` — passwordless sudo only; without it you get a clean
   error in the log, never a password prompt or a hang.

PPT limits come from the `asus-armoury` firmware-attribute `min`/`max`
files when the firmware exposes them; otherwise a generous physical
ceiling is used and kernel rejections are surfaced. Values reported as
`0`/`5` by the kernel cache are printed as `--` (unknown), never faked.

## Layout of the code

```
src/
  util.c        sysfs/exec/log helpers (single place for privilege writes)
  hw.c          read layer: fast poll (temp/RPM/battery) + full snapshot
  control.c     write layer: profile/EPP/PPT/battery/fan/kbd/Hz
  cmds.c        key/value command table shared by CLI, modes and profiles
  fan.c         8-point fan curve model (pure, unit-tested)
  modes.c       user shortcut bundles (modes.ini)
  profile.c     .ctr profiles (free-form names)
  settings.c    prefs + hardware persistence
  ui/           fullscreen TUI: layout engine + independent panels
  display/      compositor backends (see below)
tests/          unit tests (make test)
```

## Display backends (compositor refresh-rate control)

```c
typedef struct display_ops {
    const char *name;
    bool (*detect)(void);
    int  (*current_hz)(void);
    int  (*modes_hz)(int *out, int max);
    int  (*set_hz)(int hz);
} display_ops_t;
```

- `display_kde.c` — **complete**, verified on KDE Plasma (kscreen-doctor).
- `display_hypr.c` — **Hyprland handoff** (see below).
- Adding one = a new file implementing the struct + one line in
  `display.c`'s registry. `DISPLAY_BACKEND=<name>` forces a backend.

### Hyprland handoff notes (for the next maintainer)

The Hyprland backend carries the working hyprctl code from ctron v1 and
is the agreed handoff point. Current state and open items:

- [x] detect via `HYPRLAND_INSTANCE_SIGNATURE` + `hyprctl monitors -j`
- [x] JSON is a **raw array** (no `"monitors"` wrapper, no `currentMode`);
      parse top-level `name` / `width` / `height` / `refreshRate` on the
      focused output. Verified Hyprland **0.55.4** on FA507NVR (NixOS).
- [x] `availableModes` (`60` / `144` on the TUF panel)
- [x] `set_hz` via `hyprctl eval 'hl.monitor({...})'` (0.55 Lua parser;
      `keyword monitor` is a no-op). Position and scale copied from JSON.
      Fallback: legacy `hyprctl keyword monitor`.
- [ ] HDMI-A-1 / multi-monitor: only the focused output is changed.
- [ ] TUI mouse under Hyprland (v1 edge-tab history).

## Roadmap (parked)

- Waybar color sync (v1 feature, intentionally out of v2)
- MUX / dGPU switch (`gpu_mux_mode`, `dgpu_disable`) — needs reboot flow
- `throttle_thermal_policy` — overlaps platform profiles, skipped to
  avoid two masters fighting over fan behaviour
- wlr-randr / xrandr display backends

## Files

- `PLAN.md` — the approved v2 plan this tree implements
- Config: `~/.config/ctron/` (`settings.ini`, `modes.ini`, `profiles/*.ctr`)
- Log (TUI footer): ring buffer, last 24 actions

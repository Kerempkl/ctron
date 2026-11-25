<div align="center">

# ctron

**Lightweight ASUS laptop control center for Linux.**

One C binary. One dependency ([notcurses](https://github.com/dankamongmen/notcurses)). No daemon.

![version](https://img.shields.io/badge/version-v0.13.0-0072c6)
![license](https://img.shields.io/badge/license-MIT-0072c6)
![language](https://img.shields.io/badge/language-C-659ad2)
![deps](https://img.shields.io/badge/dependency-notcurses%20only-2ea043)
![platform](https://img.shields.io/badge/platform-Linux%20%C2%B7%20ASUS%20TUF-555)

</div>

**No arguments** → fullscreen TUI in your current terminal. **Any
argument** → headless CLI: does the job, exits. Script/ssh friendly —
piped output never opens the TUI.

![ctron TUI — fan curve editor and live telemetry](docs/tui-fan.png)

## What it does

| Area | What you get |
| --- | --- |
| **Fans** | Dual 8-point CPU/GPU curves with a live `%`/PWM graph editor, monotonic curve normalization, uncorrupted OEM Quiet / Balanced / Performance presets, reusable **saved fan curves** (`~/.config/ctron/fan_curves/*.fc`), per-fan on/off, EC write with full hwmon read-back verification |
| **Power** | Platform profiles, EPP, PPT watt limits (asus-armoury or ryzenadj), per-core / per-CCD / all-thread frequency ceilings, GPU core-clock lock, NVIDIA dynamic boost & temp target, asusd AC/battery profile takeover |
| **Battery** | Charge limit (20–100 %), one-shot charge-to-full |
| **Display** | Refresh-rate control via compositor backends (KDE, Hyprland), panel overdrive |
| **Lighting** | Keyboard backlight, keyboard colour and the effects the keyboard actually offers (read from asusd — the FA608PP has `static` only), optional daeboard LED daemon control + binds editor |
| **Automation** | Shortcut modes (`modes.ini`), hardware snapshots (`profiles/*.ctr`) and saved fan curves (`fan_curves/*.fc`) — save / rename / note / apply / diff / export, with saved fan curves selectable directly inside Snapshots, `2 Fans` presets, and CLI |
| **Telemetry** | Temps, fan RPM, frequencies, watts, per-core load, memory/IO pressure (PSI), network rx/tx + wifi signal, battery — live in the TUI, one line under `--watch`, CSV log under `--csv` |

Working principles, enforced in code:

- Every write is a **user action** — nothing is ever written from the poll loop.
- Writes are **verified by reading back** (kscreen-doctor and Hyprland both exit 0 without applying).
- Unknown reads print `--`, never fabricated fallback data.

Verified on an **ASUS TUF Gaming A16 FA608PP** (Ryzen 9 8940HX + RTX 5070
Max-Q, CachyOS, KDE Plasma Wayland, kernels 7.2–7.3) and a **TUF A15
FA507NVR** (Ryzen 7 7435HS + RTX 4060, NixOS, Hyprland 0.55) — same
picture with two kernel-related exceptions. Support beyond these
machines depends on the ASUS WMI stack, CPU vendor, NVIDIA driver and
compositor; the row-by-row breakdown lives in
[HARDWARE.md](HARDWARE.md) (English + Türkçe).

## Build & install

```bash
# Arch          sudo pacman -S base-devel notcurses
# Fedora        sudo dnf install gcc make pkgconf notcurses-devel
# Debian/Ubuntu sudo apt install build-essential pkg-config libnotcurses-dev
# Nix           nix-shell -p gcc pkg-config notcurses gnumake
make
./build/ctron --status
```

`make install` copies the binary to `~/.local/bin/ctron` (plus the
`ctron-toggle` helper — see [the M4 key section](#kde-armoury-crate-m4-key)).

Runtime tools are probed, never linked: `asusctl`, `nvidia-smi`,
`ryzenadj`, `kscreen-doctor` / `hyprctl`. Missing ones simply grey out
their rows.

## Quick start (Türkçe)

```bash
ctron                    # tam ekran arayüz (TUI)
ctron --status           # tüm donanım durumunu yaz, çık
ctron --watch            # canlı tek satır telemetri (Ctrl-C durdurur)
ctron --csv log.csv      # telemetriyi CSV'e kaydet (dışarıda grafikle)
ctron --doctor           # neler destekleniyor raporu
ctron --mode turbo       # hazır ayar paketi uygula
ctron mode list          # paketleri listele (modes.ini)
ctron --profile quiet    # tekil komutlar da çalışır
ctron --battery 80
```

## The TUI

| Key | Action |
| --- | --- |
| `1`–`5` / `Tab` | switch view — `1 Power` · `2 Fans` · `3 Display & Light` · `4 Snapshots` · `5 Telemetry` |
| `t` | jump directly to Telemetry (press again to return to previous view) |
| `j` / `k` | select row · `h` / `l` step / stage value |
| `e` | edit exact numeric or hex value (cursor-aware) |
| `w` / `r` | apply all staged edits · revert staged edits to live |
| `Enter` | apply / open detail (`c` Core Limits in Power, `b` daeboard in Display & Light) |
| `,` | Preferences & CLI shortcuts (`modes.ini`, startup view, optional **Boot snapshot**, terminal-native themes) |
| `L` / `?` | Event Log overlay (last 24 entries; or click bottom log) · Keymap Help overlay |
| `Esc` | strictly back / cancel / close modal (never opens a menu) |
| `q` | quit (asks twice when staged edits are pending) |

Layout uses a thin top status bar, a slim left navigation + live summary rail
(auto-collapsing to inline top tabs below 96 columns), a full-height workspace,
and a unified bottom status + contextual keybar. Default theme (`Native`)
preserves your terminal's own background/transparency (`Slate`, `Gruvbox`,
`Mono`, and `Light` are selectable under `,` Preferences, along with your
preferred **startup view**). Rows are clickable (mouse is a known gap under
Hyprland — see the [handoff notes](#display-backends-compositor-refresh-rate-control)).

View `4` (`Snapshots`) splits its left pane into **Snapshots** (`profiles/*.ctr`)
on top and **Saved Fan Curves** (`fan_curves/*.fc`) on the bottom (`f` or click
switches between the two lists; both support `group/name` subdirectories and
live diff badges `≡ live` / `Δ N`), paired with a context-aware right-hand
**Inspector & Interactive Editor**: `Enter` applies, `s`/`n` saves from live
(or press `s` directly inside `2 Fans`), `r` renames, `c` edits `# note:`, `uu`
overwrites from live while preserving notes, `dd` deletes, and `e` focuses the
right-hand editor (`h`/`l` step, `e` exact value, `x` toggle `— (skip)` for
selective/partial files, `f` capture live, `w`/`Enter` save; `o` on a saved fan
curve opens it directly in the `2 Fans` graph). On the Snapshot Editor's
`CPU fan curve` and `GPU fan curve` rows, `h`/`l` cycles through OEM presets and
all saved fan curves (`a` copies the chosen curve to both CPU and GPU; `e`
accepts a saved curve name or raw CSV). On the `PPT` row, `h`/`l` step
the three watt limits in 5 W increments (the same windows and ordering as
the Power view) and `e` takes `SPL,SPPT,FPPT`, a single value for all
three, or `off` — never a preset snap. A snapshot carries platform
profile, EPP, CPU boost, the all-core and per-core CPU ceilings, PPT, GPU clock
lock, the asusd AC/battery profiles, refresh rate, panel overdrive, charge
limit, keyboard backlight and colour, and both fan curves.

![ctron TUI — Snapshots inspector & field editor](docs/tui-snap.png)

On the `CPU ceiling` row, `c` opens the **per-core grid in snapshot mode**:
the same cells and keys as Power's `CORE LIMITS` (`h`/`l` ±100, `e` exact,
`a`/`A` a CCD / every core, `o` max, `r` undo), but it only edits what the
`.ctr` will replay — nothing is written to hardware until the snapshot is
applied. `e` on that row also takes rules joined by commas, validated as a
whole: `3500`, `core 8 3000` (the SMT sibling too), `ccd 1 2800`, `max`,
`clear`.

View `5` (`Telemetry`, or press `t` from anywhere) hosts the **8-section
read-only telemetry workspace** (CPU / GPU / iGPU / RAM / storage / network /
fans / power), each showing live numbers with zone colouring — CPU & GPU
temperatures, clocks, load, mean + busiest core utilisation,
CPU/memory/IO pressure (PSI stall %), APU package power, fan RPMs,
dGPU power/utilisation/VRAM, iGPU temp/clock/power/activity,
RAM use + swap + module temps, NVMe temps + disk I/O, per-mount
disk usage, per-interface network rx/tx, wifi signal quality,
battery draw/charge.
`h`/`j`/`k`/`l` walk the section grid and `Enter` opens a section's **detail
page** (inside a detail page `1`–`8` jump directly between sections and
`h`/`l` flip prev/next): a numeric now/min/avg/max table over the
~60 s window plus info lines from a fresh live snapshot (PPT/EPP,
per-core ceilings, GPU pstate & throttle reasons, VRAM/clock lock,
battery health, load 1/5/15 min …); the CPU page adds a per-core
`now` grid (util% + live clock per physical core, SMT merged) plus
`busiest`/`avg` series; `Esc` backs out to the grid, `r` (or `[r] reset stats`)
clears every window and the lifetime stats, and `s` flips the MIN/AVG/MAX
columns between the rolling 60 s window and everything since the last reset.
Unknown sensors stay `--`; Telemetry never writes hardware.

![ctron TUI — POWER view](docs/tui-power.png)
![ctron TUI — fullscreen telemetry](docs/tui-telem.png)

## CLI reference

The everyday surface:

```bash
ctron --hz 165                    # display refresh
ctron --fan-curve cpu 40,60,80 80,140,220
ctron --fan cpu on                # custom curve on that fan
ctron --ppt Q45                   # quick presets, or B60 / P80
ctron --ppt 45,35,54              # explicit SPL,SPPT,FPPT (W)
ctron --gpu-clock 1400            # NVIDIA core-clock lock
ctron --freq ccd 1 3500           # per-CCD ceiling (or: core 4 5386)
ctron --battery-oneshot           # charge to full once
```

<details>
<summary><b>Full flag list</b> (<code>ctron --help</code>)</summary>

```
Usage: ctron [flags] | ctron mode <sub> | ctron snapshot <sub> | ctron fancurve <sub>

No arguments opens the fullscreen TUI (needs a terminal).

Information:
  --status, -s [--json]   live hardware snapshot
  --watch, -w             rolling one-line telemetry (Ctrl-C stops)
  --csv <file>            append telemetry rows to a CSV (Ctrl-C stops)
  --doctor [--json]       capability report
  --help, -h              this help
  --version, -V           version

Modes, snapshots & fan curves:
  --mode <name> [--dry-run]   apply (or preview) a shortcut bundle
  --apply-startup [--dry-run] apply configured Boot snapshot (no-op if off)
  mode list|show <n>|add <name> <steps>|delete <n>
  snapshot list   (alias: profile)
  snapshot apply [--dry-run]|diff|export|delete <name>
  fancurve list|apply [--dry-run]|diff|export|delete <name>

Hardware (key = value pairs, same as mode steps):
  --profile quiet|balanced|performance
  --ac-profile off|quiet|balanced|performance
  --battery-profile off|quiet|balanced|performance
  --epp power|balance_power|balance_performance|performance
  --freq <mhz>                 every thread
  --freq core <cpu-id> <mhz>   one thread (kernel id)
  --freq ccd <n> <mhz>         every thread in that CCD
  --hz <rate|max>         display refresh (compositor backend)
  --battery <20..100>     charge limit
  --battery-oneshot       charge to full once
  --ppt Q45|B60|P80|<spl>,<sppt>,<fppt>|off|on
  --nv-boost <5..25 W>    NVIDIA dynamic boost
  --nv-temp <75..87 C>    NVIDIA temp target
  --gpu-clock <mhz>|reset NVIDIA core-clock lock
  --panel-od on|off       panel overdrive
  --cpu-boost on|off      cpufreq boost
  --kbd off|low|med|high  keyboard backlight
  --follow            run ctron actions from daeboard binds
  --daeboard-start    start the daemon (sudo -n systemd-run)
  --daeboard-stop     ask the daemon to quit
  --daeboard-reload   recompile ~/.config/ctron/daeboard.binds
  --aura <effect> [color|hex]
  --fan stock|silent|cool|full|on|off|cpu on|cpu off|gpu on|gpu off|<curve>
  --fan-curve cpu|gpu <temps> <pwms>
  --fan-write             write in-memory curve to the EC
  --dry-run               validate flags/mode/snapshot without writing

Config: ctron [--config-dir DIR]
Exit codes: 0 ok · 1 runtime/verify error · 2 usage error · 3 permission/sudo denied
```

</details>

Modes ("CLI shortcuts") live in `~/.config/ctron/modes.ini` and can be
edited from the TUI (**`,` → CLI SHORTCUTS**) or with `ctron mode add`:

```ini
[turbo]
steps = profile performance, ppt P80, fan cool, hz max
```

PPT notes: on locked-PPT laptops writing needs `ryzenadj` (the sudoers
rule must name the literal path, e.g. `/usr/bin/ryzenadj`), and SMU
limits are **volatile** — re-apply after reboot / profile changes.
Within the same boot (`boot_id`), `ctron` remembers the last applied PPT
and GPU clock lock and displays them as `(set)` without auto-writing on
TUI open; to apply a snapshot automatically at login or resume, select a
**Boot snapshot** in `,` Preferences (off by default) and enable the
optional oneshot unit `contrib/ctron-startup.service` (or
`contrib/ctron-resume@.service`).
`--gpu-clock` similarly needs sudoers for `nvidia-smi`. See
[SECURITY.md](SECURITY.md) for the privilege model and recommended
sudoers rules.

## Writing to hardware — the privilege model

Every write is a user action (nothing is written from the poll loop)
and goes through one chain:

1. **asusctl** when installed (no root needed),
2. else a **direct sysfs write** (works where the node is writable),
3. else `sudo -n` — passwordless sudo only; without it you get a clean
   error in the log, never a password prompt or a hang.

PPT limits come from the `asus-armoury` firmware-attribute `min_value`/`max_value`
files when the firmware exposes them; otherwise a generous physical
ceiling is used and kernel rejections are surfaced. Values reported as
`0`/`5` by the stale kernel cache are printed as `--` (unknown), never
faked.

<a id="display-backends-compositor-refresh-rate-control"></a>
<details>
<summary><b>Display backends</b> (compositor refresh-rate control)</summary>

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

Hyprland handoff state:

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

</details>

<a id="kde-armoury-crate-m4-key"></a>
<details>
<summary><b>KDE: Armoury Crate (M4) key</b></summary>

The TUF's M4 key emits `KEY_PROG3` → XKB `XF86Launch3`; KDE's shortcut
picker shows it as **"Launch (3)"**. Bind ctron to it under
**System Settings → Keyboard → Shortcuts → Add New → Command or
Script**:

```
Name:    ctron
Command: ctron-toggle           # installed by `make install`
```

`ctron-toggle` gives the key open/close behaviour: a running ctron is
closed (the `konsole -e` window ends with it), none running means one
opens. Without it every key press would spawn another window — a
desktop shortcut only spawns, it has no toggle sense. The script
honours `$TERMINAL` and falls back to `konsole`; point a `.desktop`
`Exec` at it just the same. **A service shortcut launches the command
kglobalaccel cached at session start — changing the `.desktop` `Exec`
takes effect only after the next re-login.** Independently of the
launcher, the fullscreen TUI is single-instance: a second ctron bows
out with "already running" instead of stacking windows.

If the capture dialog refuses the key, read its real code with
`sudo evtest /dev/input/event11` ("Asus WMI hotkeys"; XKB keycodes are
the Linux input code + 8). The binding lives entirely in KDE — ctron
itself never listens for the key (it is a TUI/CLI, not a daemon).

</details>

<details>
<summary><b>Layout of the code</b></summary>

```
src/
  main.c        CLI parsing + VERSION; no args = TUI, any arg = headless
  util.c        sysfs/exec/log helpers (single place for privilege writes)
  hw.c          read layer: fast poll (temp/RPM/battery) + full snapshot
  control.c     write layer: profile/EPP/PPT/battery/fan/kbd/Hz
  cmds.c        key/value command table shared by CLI, modes and profiles
  fan.c         8-point fan curve model (pure, unit-tested)
  modes.c       user shortcut bundles (modes.ini)
  profile.c     snapshots (.ctr files, free-form names)
  settings.c    prefs + hardware persistence
  telem.c       sample rings + stats for the 't' screen (pure, unit-tested)
  tinput.c      cursor-aware single-line text input (pure, unit-tested)
  daeboard.c    optional LED daemon client (AF_UNIX seqpacket socket)
  ui/           fullscreen TUI: layout engine + independent panels
  display/      compositor backends (see above)
tests/          unit tests (make test)
```

</details>

## Roadmap (parked)

Open work is tracked in [NEXT.md](NEXT.md) and [REVIEW.md](REVIEW.md).
Intentionally parked: Waybar color sync · MUX / dGPU switch (needs a
reboot flow) · `throttle_thermal_policy` (overlaps platform profiles) ·
wlr-randr / xrandr backends · UI language table (EN/TR) · polkit helper
(SECURITY.md Phase 3) · per-key keyboard color (single-zone keyboard) ·
replaying non-static AURA effects in snapshots (asusd does not document
their mode numbers) · AURA boot/sleep LED power states ·
daeboard daemon-side work (lives in the daeboard repo).

## Documentation

| Doc | What's in it |
| --- | --- |
| [HARDWARE.md](HARDWARE.md) | row-by-row support matrix & sensor map (English + Türkçe) |
| [SECURITY.md](SECURITY.md) | privilege model, sudoers rules, hardening roadmap |
| [CHANGELOG.md](CHANGELOG.md) | release-level changelog (detailed log in `docs/history/`) |
| [NEXT.md](NEXT.md) | open field work, version policy, parked list |
| [REVIEW.md](REVIEW.md) | code-verified critique & `0.x` engineering roadmap |
| [AUDIT.md](AUDIT.md) | A–L error-class audit checklist |
| [HANDOFF.md](HANDOFF.md) / [AGENTS.md](AGENTS.md) | recent session handoffs & agent guidelines |
| `docs/history/` | archived plans (`PLAN.md`, `PLANS.md`) & older session/change logs |

Config lives in `~/.config/ctron/` (`settings.ini`, `modes.ini`,
`profiles/*.ctr` snapshots, `fan_curves/*.fc` saved fan curves); the TUI
keeps a ring buffer of the last 24 actions (press `L` or click the LIVE
log line to inspect).

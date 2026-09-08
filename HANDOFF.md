# Ctron Handoff

Created 2026-09-03 by Arcioth & Kerempkl. Invert session close **2026-09-09**.

## Status

**Paused.** CLI-first invert is in `main` (local + GitHub after this push). TUI still works in the current terminal (`ctron --tui`). `--watch` was checked here. **Field test of the rest of the CLI/TUI is not done** — resume there.

Machine: ASUS TUF Gaming A15 **FA507NVR**, Ryzen 7 7435HS + RTX 4060, kernel 6.18, NixOS.

Product: Linux **ASUS TUF** control program (AMD Ryzen + NVIDIA). Headless flags are the program. `--tui` is optional. Notcurses is always linked. No Kitty spawn. daetron shelved. No device-plugin framework.

## Paths

| | |
|---|---|
| Project | `~/Documents/ctron` |
| GitHub | `https://github.com/kerempkl/ctron` |
| Binary | `~/.local/bin/ctron` (`vhelper`, `tufhelper` symlinks) |
| Config | `~/.config/ctron/` (`config.ini`, `settings.ini`) |
| Profiles | `~/.config/ctron/profiles/*.ctr` |
| Old widget config | `~/.config/vhelper/` — not auto-migrated |
| Build | `make && make install` (or `nix-shell -p gcc pkg-config notcurses gnumake --run make`) |

No-args: welcome + exit 2. `--tui` = this terminal. `--watch` = 250 ms temp/freq/battery.

## What landed this session

- Invert: no Kitty; CLI flags for TUI hardware knobs (PPT, NV, fan-curve/write, aura, profile subcommands).
- `--setup`, `--doctor`, `--config-dir`, XDG `ctron/`, `*.ctr`.
- `--status` / `--doctor` use `hw_refresh_live` (sysfs), not settings.ini.
- Poll loop no longer `sudo tee`s `scaling_max_freq`.
- `make test` persist / `.ctr` round-trip passed.
- Duplicate lowercase markdown removed.

## Not field-tested (resume)

- TUI click-through of FAN Write, PPT Q45/B60/P80, Aura, SLOT import/export `.ctr`.
- CLI writes: `--ppt`, `--fan-write`, `--profile`, `--battery`, `--aura` on the live laptop.
- `--setup` custom dir + later default-path launch.
- Old `vhelper/*.acv` import.

`--watch` looked fine (temp/freq moving). `--status` matched k10temp / ACPI / hwmon fans. PPT sysfs `5` still prints `--`.

## Todo (next session)

1. Field test CLI writes + TUI FAN Write (asusctl path). Restore Quiet curve if you touch fans.
2. Honest Tctl label in TUI (software freq). 4 °C hysteresis still not in code; poll no longer writes freq anyway.
3. Optional: migrate `~/.config/vhelper` → `ctron`.
4. After v1: RyzenAdj only if `sudo ryzenadj -i` works (needs `ryzen_smu`). No apply.sh until then.
5. After v1: daetron USB bind (inert `0000/0000/` rule in `~/Documents/daetron`).
6. `asus-armoury` on 6.19 — dual-path already in the binary; do not bump kernel.

Parked: Metatron / Uriel / `thermal-p2`. Gentoo ESP / GAMES. Edge tab-switch. Extra EPP. 140 W. Kitty 512² applet.

## Known limitations

- Edge tab-switch (TUI) still chronic if you go back to a 512² float later.
- Fan persist is mode A: settings/`.ctr` hold X,Y; Write is asusctl+sysfs. Sysfs-only loses to asusd.
- PPT live read is 0 or 5 until a write.
- Tctl is software / unused on the poll loop. Not FlyGoat `--tctl-temp`.
- Aura on `--status` is last-set, not live EC.
- Root for WMI writes (`sudo tee`). Do not chmod sysfs.

## Controls (TUI)

`1`–`6` tabs. `H`/`L` prev/next. `j`/`k` focus. FAN: X,Y editor, Write. `q` saves and exits.

## Sign-off — 2026-09-09

Invert coded and pushed. Field test remains. Continue later from todo 1.

Signed:

- **Arcioth** — System Architect & User
- **Kerempkl** — Original Architecture & Blueprint
- **Grok 4.6 (xAI)** — invert, live `--status`/`--watch`, this handoff
- **Antigravity (Google DeepMind)** — earlier pair work (mouse, PPT, SLOT, tint)

*Date: 2026-09-09*

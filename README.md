# Ctron

Linux control program for **ASUS TUF** laptops (AMD Ryzen + NVIDIA). One C binary with Notcurses. Headless flags are the program. `--tui` is optional and runs in the current terminal.

Verified on a TUF A15 FA507NVR (kernel 6.18). Makefile is the build; flake.nix is one package of that Makefile.

## Status

Paused 2026-09-09. CLI-first invert is in. `--watch` / live `--status` checked on FA507NVR. **Writes and TUI click-through are not field-tested.** Fan Write that lasts uses `asusctl`. Temp cap is a cpufreq ceiling, not RyzenAdj. No Kitty spawn. daetron is not this release. See `HANDOFF.md`.

## Requirements

C11 compiler, make, pkg-config, [notcurses](https://github.com/dankamongmen/notcurses).

```
# Debian/Ubuntu    sudo apt install build-essential pkg-config libnotcurses-dev
# Fedora           sudo dnf install gcc make pkgconf notcurses-devel
# Arch             sudo pacman -S base-devel notcurses
# Nix              nix-shell -p gcc pkg-config notcurses gnumake   # optional
make
./build/ctron --status
```

Runtime (detected): `asus-nb-wmi`, fan hwmon, k10temp, optional `asusctl`, compositor tools for Hz. Writes use `sudo tee` on user actions. Config: `~/.config/ctron` (or `$CTRON_CONFIG` / `--config-dir`). Profiles: `profiles/*.ctr`.

## Run

```bash
./build/ctron --help
./build/ctron --status
./build/ctron --watch
./build/ctron --doctor
./build/ctron --setup          # optional wizard
./build/ctron --tui            # this terminal
```

No-args prints a short welcome and exits. It does not open a window.

```bash
ctron --profile Performance
ctron --hz 144
ctron --battery 80
ctron --ppt B60
ctron --fan-write
ctron --epp balance_performance
ctron --tctl 85
ctron profile list
ctron profile export TUF
```

## Docs

README this file · PLAN.md invert · AGENTS.md agents · HANDOFF.md session · CHANGELOG.md history

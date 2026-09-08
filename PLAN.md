# Ctron plan — invert (paused 2026-09-09)

Approved and implemented. **Field test of writes/TUI knobs is not done.** `--watch` / live `--status` looked fine. Resume from `HANDOFF.md` todo 1.

`ctron` is a Linux **ASUS TUF** control program (AMD Ryzen + NVIDIA dGPU). Headless flags are the program. `--tui` is optional. The TUI is **always compiled in** (notcurses is a real dependency). No Kitty spawn. No plugin/platform framework.

---

## Locked for this version

- Target hardware: ASUS TUF series, AMD Ryzen, NVIDIA. That is the product. `hardware.c` stays TUF-shaped. No `platform.h`, no second device, no “modular later” scaffolding.
- TUI: always linked. Running it is optional (`--tui` in the **current** terminal). No `make tui` split. No Kitty, no 512² applet this pass.
- Profiles: `*.ctr` under the config dir (`profiles/TAG.ctr`). Stop writing new `*.acv` / `vhelper` paths.
- Makefile is the build. flake.nix packages it. README’s happy path is `make`, not nix-shell.
- daetron, RyzenAdj `.ko`, polkit helper, edge tab-switch, 140 W, kernel bump: not this pass.

---

## Wizard: what I suggest

Do **not** make setup a gate. Defaults must work with zero questions:

- Config dir: `$XDG_CONFIG_HOME/ctron` (usually `~/.config/ctron`)
- Profiles: `$config_dir/profiles/*.ctr`
- Env `CTRON_CONFIG` / flag `--config-dir` override

`--setup` is the wizard: re-runnable, documented, the place for questions (custom config dir, import old `vhelper/*.acv` if you want that, sudo check). Print distro from `/etc/os-release` and DMI product. Write `config.ini`. Exit.

**First no-args:** if stdin is a tty and there is no config file yet, print a short welcome (distro, DMI, default paths, “`ctron --setup` to change this, `ctron --help` for flags, `ctron --tui` for the interface”) and **exit**. Do not block on prompts. `ctron --battery 80` and `ctron --status` never start a wizard.

Why not auto-run the full wizard on first `ctron`: scripts, waybar, ssh non-tty, and “I just wanted --help” all hang or get a surprise interview. Why not skip `--setup` entirely: you asked for a welcome and a place to choose the directory; a named command is that place.

Build-time setup is still out. Packagers do not sit at the laptop.

TUI-during-setup: with TUI always in the binary, do not ask “install TUI?”. At most “show `--tui` in the welcome blurb.” That is a help sentence, not a compile switch.

---

## Defaults after invert

| Today | After |
|-------|--------|
| no-args forks kitty | no-args: usage if config exists; short welcome if not (tty). Never a window. |
| `run.sh` nix-shell + kitty | `make && ./build/ctron "$@"` or delete |
| CLI missing PPT/NV/fan-write | flags for every TUF hardware setter the TUI already has |
| `~/.config/vhelper` + `.acv` | `~/.config/ctron` + `.ctr` |

CLI flags to add (same `hw_*` as the TUI): `--doctor`, `--freq`, `--ppt`, `--nv-boost`, `--nv-temp`, `--panel-od`, `--cpu-boost`, `--battery-oneshot`, `--kbd`, `--aura`, `--fan-write`, `--fan-curve`, `profile list|export|import|delete`. Theme/tint/hex picker stay TUI-only.

`--status` / `--doctor` do not lie (PPT 0 or 5 → `--`; no asusctl; software cap ≠ SMU).

Privilege: no `sudo tee` on the 250 ms poll. Writes on user action / flags only.

---

## Order after approval

1. Paths: XDG `ctron/`, `.ctr`, `CTRON_CONFIG`. — done
2. `main.c`: no kitty. no-args welcome. `--setup`. — done
3. `--doctor` + welcome (`os-release` + DMI). — done
4. Missing CLI flags. — done
5. `--tui` in current terminal (fails cleanly without a tty). — done
6. README. — done

Keep `hardware.c` field behavior (asusctl+sysfs fan Write, PPT stale-5, 8-point pad). Old `~/.config/vhelper` is not auto-migrated; `profile list` still sees leftover `*.acv` if you point config there.

---

## Approve / reject

- TUI always in the binary; `--tui` optional; no Kitty this pass.
- No modularity. TUF + Ryzen + NVIDIA only.
- Wizard = `ctron --setup` only. First no-args is a short welcome, not an interview.
- Config `~/.config/ctron`, profiles `*.ctr`.

Say yes (or mark those four) and we start at order item 1.

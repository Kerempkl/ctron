# Changelog

All notable changes to **ctron** are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
Semantic Versioning.

*For the verbatim per-session engineering log from 2026-09 through 2026-11,
see [docs/history/changelog-2026-09-11.md](docs/history/changelog-2026-09-11.md).*

---

## [Unreleased]

### Added
- **Full-tree audit checklist (`AUDIT.md`)** covering categories A–L (~50 items)
  with live measurements (8-minute TUI RSS drift = 0 KB, extreme terminal resize
  `20×6`/`30×40`/`300×80`, hostile config battery, CLI bad-input table).
- **Professionalization critique & roadmap (`REVIEW.md`)** documenting 11
  code-verified P0 bugs/honesty cracks and a `v0.10.0` → `v1.0.0` plan.

### Fixed
- **Power honesty & CORE LIMITS unification** (`1611f5e`): single `PW_W_*`
  write-gate mask shared by `●`, Apply button, quit guard, diff preview and
  apply writes; unreadable `asusd.ron` (`-2`) guard on writes; CORE LIMITS
  staging survives close/open and rides the single POWER apply worker (global
  first, per-core captured pre-apply); fixed `cf_staged` zero-init and
  stale-baseline echo bugs.
- **Whole-line config parsing & note-rewrite contract** (`4d32a8e`): added
  `ut_fgets_full()` so truncated lines in `settings.ini`, `modes.ini`, and
  `.ctr` files are skipped and logged instead of parsed half-cut;
  `profile_set_note()` now refuses rewrites that cannot round-trip (file left
  byte-identical); `db_reload()` clears `err` on entry.
- **Undefined-behavior & upper-bound guards** (`3ac694c`): bounded double→int
  casts in `hw_parse_gpu_csv()` (C11 6.3.1.4 UB at `1e300`); upper bound
  (`≤10000`) on `settings.ini` `cpu_mhz_limit`.
- **Worker-vs-render data race** (`db349e1`): render loop now draws from a
  frozen copy (`s_hw_frozen`) while the background apply worker writes real
  `hw` (`apply_hw`), restored on finalize and signal-exit after `pthread_join`.
- **Process timeout escalation** (`2aaaea8`): added `-k1` KILL escalation to
  all four `timeout` call sites (TERM at 3 s, KILL at 4 s).
- **Atomic config writes** (`274f630`): added `ut_atomic_begin`/`commit`
  (`O_EXCL|O_NOFOLLOW`, `0600`, `fflush`+`fsync`+`rename`) across all six
  config writers; logged persistence failures and guarded `ut_log`
  `localtime_r` (`f06aeb6`).

### Changed / Docs
- **README & HARDWARE refresh** (`8e3d4b2`): re-verified on kernel `7.3.1-cachyos`
  (kernels `7.2–7.3` & `6.18-lts`), re-shot TUI screenshots (`docs/tui-*.png`).
- **2026-11-23 documentation audit & streamlining**: verified `REVIEW.md`
  claim-by-claim against source and live `--status` (added `PREF_NO_SUDO`
  4-path bypass, `profile_meta_parse` trap, `udev`+`tmpfiles.d` split,
  `editor_daeboard.c` dead keys, `tui_smoke.py` env leak); fixed broken GitHub
  anchors in `README.md`, stale `gpu_metrics`/`sparkline` notes in `AGENTS.md`,
  stale `ctron-ppt`/`ctrl_ppt_*_cmd`/pidfile paths in `SECURITY.md` &
  `HARDWARE.md`, and duplicate `F4` + reopened `[~]` items in `AUDIT.md`;
  archived `PLAN.md`, `PLANS.md`, old `HANDOFF.md` and detailed `CHANGELOG.md`
  under `docs/history/`.

---

## [0.9.0] — 2026-10-30

### Added
- **Numbers-only telemetry (`t`)**: replaced charts with right-aligned
  zone-coloured values, grid window min–max ranges, spaced detail reports
  (`NOW`/`MIN`/`AVG`/`MAX`), `'r'` stats reset, and `'s'` window vs since-reset
  lifetime stats.
- **Expanded telemetry (22 → 30 series)**: per-core NOW table (SMT merged) +
  `busiest`/`avg` series, CPU/memory/IO PSI stall `%` (`/proc/pressure/*`),
  APU PPT mirrored into CPU, RAM temp/swap, NVMe hottest temp, iGPU `util`,
  wifi signal quality (`/proc/net/wireless`), load 1/5/15 min, df-exact
  per-mount disk usage (`/proc/self/mounts`).
- **`--csv <file>` logger**: append-safe 22-column telemetry capture with
  column-count header guard and monotonic delta rates.
- **POWER panel readability & TUI navigation**: visual group headers, scroll
  viewport with `▲▼` markers, selected-row descriptions, pre-apply
  `"will write:"` preview; LIVE-strip `4` + `Enter`/`Space` opens fullscreen
  telemetry; `Shift+arrows` move focus geometrically between panels.

### Fixed
- **Security passes 2–3**: mouse input respects the `apply_busy` lock;
  initialized worker `err` buffers; validated `mode add` steps against newline
  injection (`mode_steps_ok`); hardened `ut_read_int`/`ut_parse_ints` clamps;
  mode-name validation at both creation sites.
- **APU split honesty correction**: live tight-pair proof showed `gpu_metrics`
  `u32@28` is gfx activity (centi-percent), not watts — retired the parser and
  kept amdgpu `power1_input` as the sole honest APU PPT reading.
- Fixed CPU grid box labelling threads as cores (now `8c/16t` / `16c`),
  per-core ceilings block bar replaced with numeric summary, ASCII-only
  uppercasing for `tr_TR` locale safety.

---

## [0.8.1] — 2026-10-28

### Security / Fixed
- Fixed stack buffer overflow in `profile_set_note()` on `>4 KB` `.ctr` files
  (`off += snprintf` wrap, ASan-proven) with hard-clamped `note_append()` +
  regression test.
- Hardened `scripts/ctron-toggle.sh` KWin `.js` creation with `set -C`
  noclobber; added `make HARDEN=1` (`-fstack-protector-strong`,
  `_FORTIFY_SOURCE=2`) and explicit `daeboard` sudoers warning in `SECURITY.md`.

---

## [0.8.0] — 2026-10-28

### Added
- **FAN telemetry section** (8th section, full 4×2 grid) + `HARDWARE.md`
  telemetry sensor map (EN + TR).
- `scripts/ctron-toggle.sh` polite `SIGTERM` → `SIGKILL` escalation (~2 s).

---

## [0.7.0] — 2026-10-27

### Added
- Telemetry sensors: `k10temp` `Tccd1`/`Tccd2` (label-verified), `spd5118` RAM
  thresholds & alarm flags, NVMe Sensor 1/2 hottest temps, iGPU
  `gpu_busy_percent` & `pp_dpm_sclk`.
- Automated CCD column verification on FA608PP (`CCD1`/`CCD2` matched sysfs L3
  groups).

---

## [0.6.0] — 2026-10-27

### Added
- **NET telemetry section** (physical/wifi interfaces via `/sys/class/net`,
  container/VPN plumbing excluded) and NVIDIA `pstate` + named
  `clocks_event_reasons` throttle flags.
- Added MIT `LICENSE`.

---

## [0.5.0] — 2026-10-26

### Added
- Fullscreen telemetry **detail pages** (`Enter` / `1`–`8`, `h`/`l` section
  flip, spatial 2D grid navigation), iGPU `vddgfx`/`vddnb` voltages, battery
  health (`charge_full`/`design`), and USB-C PD input watts.

### Security & Performance
- Eliminated `system()` and completed shell-free argv exec for batched cpufreq,
  PPT (`ctrl_ppt_*_argv`), and daeboard calls.
- Landed performance pass (input batching + 120 ms key-refresh rate limit cut
  key-burst CPU by 62%; split `cpu_limits_sweep` static/dynamic passes; strip
  on install + opt-in `make LTO=1`).

---

## [0.4.0] — 2026-10-26

### Added
- **Fullscreen read-only telemetry (`t`/`T`)** powered by pure sample rings
  (`src/telem.c`) and a `SECTIONS[]` registry (`src/ui/panel_telem_full.c`).
- Extended single `nvidia-smi` query, amdgpu hwmon, RAM/NVMe hwmon,
  `/proc/meminfo`, `/proc/loadavg`, `/proc/diskstats`, and `statvfs("/")`.
- Added `flake.nix` (`packages.ctron` + `devShell`) and multi-monitor parsing
  for KDE (`eDP*` block) and Hyprland (focused monitor object).

---

## [0.3.1] — 2026-10-26

### Security / Fixed
- Replaced `/bin/sh` in `ut_priv_write` with fork+execve `ut_write_via_argv()`;
  migrated `nvidia-smi`, `kscreen-doctor`, and `hyprctl` to `ut_exec_argv*`.
- Hardened singleton pidfile (`O_CREAT|O_EXCL|O_NOFOLLOW`, `0600`) and
  `ctron-toggle` claims; added `/usr/local/bin/ctron-ppt` probe and
  `SECURITY.md §4` sudoers guide.

---

## [0.3.0] — 2026-10-25

### Added
- Fan presets modal (`m`) with provenance labels, Armoury Crate M4 toggle
  (`scripts/ctron-toggle.sh`) + single-instance guard + clean `SIGTERM` exit.
- `ryzenadj` PPT write path, NVIDIA GPU core-clock lock (`--gpu-clock`),
  optional `daeboard` LED daemon client + binds editor, background apply worker,
  per-core / per-CCD frequency limits (`CORE LIMITS` overlay), and redesigned
  `README.md` with TUI screenshots.

---

## [0.2.1] — 2026-09-27

### Added
- `HARDWARE.md` bilingual support matrix, in-tree PTY smoke suite
  (`make tuitest` / `scripts/tui_smoke.py`), fan write read-back verification,
  and staged POWER view (`Apply`/`Revert`, toast, exact typing, presets).

---

## [0.2.0 / v2] — 2026-09-19 to 2026-09-22

### Added
- Initial **ctron v2** single-binary C11 + notcurses TUI/CLI rewrite:
  snapshots (`.ctr`), modes (`modes.ini`), fan curve editor, KDE & Hyprland
  0.55 display backends, `LC_NUMERIC="C"` `tr_TR` fix, per-CPU cpufreq writes,
  floating Settings overlay with live layout controls.

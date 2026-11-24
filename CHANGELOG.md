# Changelog

All notable changes to **ctron** are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
Semantic Versioning.

*For the verbatim per-session engineering log from 2026-09 through 2026-11,
see [docs/history/changelog-2026-09-11.md](docs/history/changelog-2026-09-11.md).*

---

## [Unreleased]

### Added
- **Two-pane `4 Snapshots` Inspector & Interactive Field Editor (`panel_profiles.c`, `profile.{h,c}`):**
  - Replaced the single-column snapshot list with a Konsept C two-pane workspace:
    left pane lists snapshots with live diff count badges (`≡ live` / `Δ N`),
    right pane shows a 15-field live Inspector & Interactive Editor (`e` / `l`
    or click any field to edit).
  - Supports **selective / partial `.ctr` snapshots** via `profile_save_meta()`:
    pressing `x` / `Del` toggles any field to `— (skip)` so omitted hardware
    domains stay completely untouched on apply; `h`/`l` step values, `e` types
    exact values (`MHz`, `SPL,SPPT,FPPT` / `Q45`/`B60`/`P80`/`off`, `gpu-clock`,
    `Hz`, `battery %`), `f` captures live CPU/GPU fan curves or per-core caps,
    and `w`/`Enter` saves atomically.
  - **Double-`u` (`uu`) live overwrite:** overwrites the selected snapshot from
    fresh live hardware while preserving its `# note:` header.
- **Snapshot scope widened to everything the Power and Display & Light views
  can write (`profile.{h,c}`, `panel_profiles.c`, `editor_corefreq.c`):**
  - **Keyboard colour (`aura = static <hex>` in `[light]`)**: captured from the
    live keyboard, diffed against it, stepped (`h`/`l` walk the 8 preset
    colours), typed (`static 1a2b3c`, `breathe red`), or captured with `f`.
  - **asusd power-source profiles (`ac-profile` / `battery-profile`)**: the
    two POWER rows that snapshots could not carry; `off|Quiet|Balanced|
    Performance` each, omitted (`—`) unless the snapshot names them.
  - **Per-core ceilings can now be assigned from the Snapshot Editor**, not
    only captured from live hardware: `c` on the `CPU ceiling` row opens the
    CORE LIMITS grid in snapshot mode (`h`/`l`, `e`, `a`/`A`, `o`, `x` to drop
    a thread, `r` undo) over its own `prof_cf_staged[]` pool, so a snapshot
    edit is never a pending POWER write; `e` accepts joined rules —
    `5386`, `core <cpu-id> <mhz>` (SMT sibling included), `ccd <n> <mhz>`,
    `max`, `clear` — validated as a whole by the new pure
    `profile_core_rule()`.
- **Keyboard LED state is now read (`hw.c`, `hw.h`):** asusd's TUF aura
  interface (`LedModeData`, `SupportedBasicModes` over D-Bus, with
  `/etc/asusd/aura_tuf.ron` as the daemon-down fallback) fills
  `hw->aura_mode` / `aura_rgb` / `aura_modes`; `--status` prints an `Aura`
  line, `--status --json` a `keyboard.aura` object, `--doctor` the interface
  and its effect list. While daeboard is up nothing is read (it owns the
  keyboard). Pure parsers `hw_aura_dbus_parse()`, `hw_aura_modes_parse()`,
  `hw_aura_ron_parse()` are unit-tested against the exact asusctl 6.5.0
  output of the FA608PP.

### Changed
- **Snapshot Editor PPT row steps watts, not presets (`panel_profiles.c`):**
  `h`/`l` no longer jumps between the Q45/B60/P80/off presets — one step
  could replace hand-entered watts wholesale (a typed `50,60,70` snapped
  straight to B60's `60/75/75` or P80). The row now steps all three watt
  limits in 5 W increments inside `1 Power`'s own windows
  (`ctrl_ppt_limits`) with the `fppt >= sppt >= spl` ordering
  (`ctrl_ppt_order`), entering from `off`/omitted at the live values or
  `60/75/75`. `e` is the manual path — `SPL,SPPT,FPPT`, one value for all
  three, or `off`; `Q45`/`B60`/`P80` still parse as typing shortcuts, they
  just no longer own the stepping.
- **`3 Display & Light` AURA rows show the real keyboard (`panel_workspace.c`):**
  the colour rows start from what the keyboard shows (an untouched custom
  colour is displayed as `#rrggbb (live)`), and on a keyboard whose effect
  list is just `static` the `Aura effect` row says so instead of cycling
  through 11 effects the hardware will reject.
- **`aura` command semantics (`cmds.c`, `control.c`):** `aura = <effect> <hex>`
  now applies that effect with that hex (it used to force `static` for any
  hex value); `--dry-run` and the real write share one verdict, so an effect
  the keyboard does not offer fails both without touching asusctl.
- **README visuals refreshed for the v0.12.0 Konsept C TUI:** re-shot
  `docs/tui-{fan,power,telem}.png` (rail + workspace layout, inline
  `CORE LIMITS`, 8-section Telemetry grid), added a `docs/tui-snap.png`
  two-pane Snapshots inspector image to the README, and added an opt-in
  `CTRON_SHOT_CONFIG=<dir>` to `scripts/tui_screenshot.py` for shooting
  against a pre-seeded config.
- `docs/tui-snap.png` re-shot for the 15-field inspector.

### Fixed
- **Aura writes reported success when the keyboard did not change
  (`control.c`):** asusctl 6.5.0 prints
  `org.freedesktop.DBus.Error.NotSupported` for an effect the keyboard lacks
  and still **exits 0**, so `--aura breathe red` and the `Aura effect` row
  logged `ok` on the FA608PP (which only has `static`) while nothing changed.
  `ctrl_set_aura_ex()` now reads the LEDs back through asusd and fails unless
  the mode and colour match; proven live with an invisible 1-unit colour
  change (`00fffe` read back, then restored to `00ffff`).
- **`"ice"` never matched the `Ice` colour under a Turkish locale:** colour
  names were compared with `strcasecmp()`, which folds `I`/`i` differently
  under `tr_TR` (the TUI's locale). New locale-independent
  `ut_ascii_ieq()` is used for colour, mode and asusd-profile names.
- **Duplicated board name in the device model string (`hw.c`):** some ASUS
  firmware repeats the board name in DMI `product_name`
  (`ASUS TUF Gaming A16 FA608PP_FA608PP`), which leaked into the TUI header,
  `--status`/`--doctor`/`--json` model field, and `.ctr` `# device:` headers.
  New pure `hw_sanitize_model()` collapses the repeated trailing token
  (word-bounded only), unit-tested in `check_model_sanitize()`.
- **Snapshot backend & apply bugs (`profile.c`, `ui.c`, `main.c`, `panel_profiles.c`):**
  - **Fresh live reads before TUI save/inspect:** entering `4 Snapshots`,
    opening Save (`s`/`n`), saving, and `uu` overwriting now call
    `hw_refresh_live()` first instead of exporting stale startup values for
    `bat_limit`, `panel_od`, `cpu_boost`, `epp`, `hz_cur`, and `ppt_*`.
  - **No unconditional fan curve clobber on apply:** `profile_import()`,
    `APPLY_SNAPSHOT`, and `ctron snapshot apply` no longer force
    `fan_cpu_on = fan_gpu_on = true` or run `ctrl_fan_write()` when a `.ctr`
    omits fan curves; `ctrl_fan_write()` errors are now surfaced instead of
    silently ignored, and post-apply refreshes `hw_refresh_live()`,
    `cf_reset_staging()`, and cached snapshot metadata.
  - **Lossless per-core & `ppt = off` metadata + diff:** `profile_meta_parse()`
    and `profile_meta_diff()` now track `freq = core <id> <mhz>` (plus legacy
    `freq core`) and `ppt = off`, preserve `# note:` on `profile_export()`
    overwrite, skip redundant `gpu-clock = reset` when unlocked, and clean up
    empty 1-level group subdirectories on `profile_delete()` / `profile_rename()`.
- **Inline `CORE LIMITS` background thermal ceiling oscillation (`editor_corefreq.c`):**
  untouched cores now keep `cf_staged[id] == 0` and read live ceilings via
  `cf_eff(id)` instead of seeding `cf_staged[]` on draw, preventing firmware
  thermal ceiling shifts from staging phantom per-core writes on `w`.

---

## [0.12.0] — 2026-11-24

### Changed
- **Complete TUI redesign (Konsept C — Left Rail + Full-Height Workspace):**
  - Replaced the 4-box neon Bento layout, faux GUI button clutter, and
    duplicate `CONTROLS`/`LIVE` panels (deleted `panel_controls.c` and
    `panel_telemetry.c`) with a clean UNIX layout: thin top summary header,
    slim 22-col left rail (`VIEWS 1–5` + compact `LIVE` metrics, auto-collapsing
    to inline top tabs below 96 cols), full-height right workspace, and a
    unified 2-row status/progress + contextual keybar footer.
  - **Terminal-native themes:** new `Native` (default) and `Mono` themes
    preserve the terminal's default background/transparency (`default_bg`),
    joined by restrained `Slate`, `Gruvbox`, and `Light` palettes; clean
    single-line modals (`ui_window`) without double-line drop shadows, and
    full-width selection bars (`ui_row`).
  - **Unified conflict-free keymap:** flat `1`–`5` / `Tab` / `Shift+Tab` view
    switching (`1 Power`, `2 Fans`, `3 Display & Light`, `4 Snapshots`,
    `5 Telemetry`), `t` toggles Telemetry, `Esc` strictly backs out / cancels /
    closes modals (never opens a menu), `,` opens Preferences & CLI shortcuts,
    `?` opens a floating Help modal, `e` edits exact values consistently, and
    `w`/`r` apply/revert staged edits across Power & Display/Light.

### Added
- **5th View `Telemetry` + configurable `startup_view`:** the 8-section
  read-only telemetry screen now runs directly as View `5` (preserving detail
  pages, `1`–`8` jumps inside detail, `r` reset, and `s` window/since-reset),
  and `,` Preferences lets you choose which of the 5 views `ctron` opens on
  (`startup_view = 0..4` persisted in `settings.ini`, backward-compatible with
  older configs).
- **Unified `Display & Light` view (`3`):** brings Refresh rate, Panel
  overdrive, Battery charge limit (`e` exact), one-shot 100% charge
  (`ctrl_battery_oneshot()`), Keyboard backlight, Aura, Custom hex swatch, and
  daeboard controls under one clean staged view backed by the `APPLY_SYSTEM`
  background worker.
- **Inline `CORE LIMITS` pane inside `1 Power`:** per-core / per-CCD frequency
  ceilings now sit directly beside (wide terminals `>= 89` workspace cols) or
  below (`>= 28` rows) the Power settings list with live CCD1/CCD2 ceilings,
  focused via `c` or clicking any core cell (`Esc`/`c` returns focus to Power;
  no popup modal needed).
- **GPU live temp marker (`▲`)** on the Fan Curve x-axis when editing GPU curve.

### Fixed
- **`editor_fan_act()` stale `fe_input` state:** clicking non-field buttons
  (`CPU`/`GPU`/`-`/`Write`/`Toggle`) now clears inline typing mode so `q` and
  shortcuts work immediately afterward.

---

## [0.11.0] — 2026-11-24

### Added
- **Machine-readable JSON output (`--status --json`, `--doctor --json`)** with
  zero external libraries — unknown/stale sensor reads serialize honestly as
  JSON `null` (ready for `jq`, Waybar custom modules, and scripts).
- **`ctron snapshot diff <name>`** exposing `profile_meta_diff()` on the CLI
  (and live-refreshing before `snapshot export` so exported values match).
- **`--dry-run` validation & preview** for `--mode <name>`, `snapshot apply <name>`,
  and hardware CLI flags without touching hardware.
- **Documented exit-code contract:** `0` ok · `1` runtime/verify error · `2`
  usage error · `3` permission/sudo denied (`ut_priv_denied` sticky indicator).
- **Full `.ctr` snapshot scope:** export, capture preview, parse, and diff now
  cover `panel-od`, `cpu-boost`, `gpu-clock` (only when locked `> 0`), and
  `ppt` (only when all 3 limits read `> 0`; stale `-1` never written).
- **TUI Event Log viewer (`L`):** floating window displaying the 24-entry
  `ut_log` ring newest-first with error highlighting, `j`/`k`/`g`/`G` scrolling,
  `Esc`/`L`/outside-click close; also accessible via `l` in LIVE or clicking the
  LIVE log row.
- **Cursor-aware `tinput_t` (`src/tinput.{h,c}`):** full `Left`/`Right`,
  `Home`/`End`, `Delete`, `Ctrl+A`/`E`/`K`/`U`/`W`, and mid-string `|` / end `_`
  cursor rendering across every TUI input widget (unit-tested + PTY smoke tested).
- **Double-`d` delete confirmation** for snapshots (`panel_profiles.c`) and
  modes (`panel_settings.c`): first `d` arms with an inline prompt, second `d`
  deletes, any other key or focus/overlay exit cancels.

### Security
- **Zero `/bin/sh` and zero `popen()` in the entire binary (`AUDIT.md C6`):**
  replaced `ut_have_cmd()` and `nvidia_smi_bin()` with pure-C `ut_find_in_path()`
  (`stat S_ISREG + X_OK`, zero fork/shell), migrated all 17 remaining `asusctl`/
  `busctl` calls to `ut_exec_argv*`, and deleted `ut_exec` / `ut_exec_raw`
  (verified via `nm -u build/ctron`).

### Fixed
- **Honest unsupported POWER rows:** when `asusd_ac == -2` / `asusd_bat == -2`,
  `pw_nudge_row()` no longer mutates hidden staging values and displays
  `-- (asusd absent)`.
- **`snapshot export` stale state:** `cmd_profile()` now calls `hw_refresh_live()`
  before exporting so battery limit and other live fields are fresh.

---

## [0.10.0] — 2026-11-23

### Added
- **Full-tree audit checklist (`AUDIT.md`)** covering categories A–L (~50 items)
  with live measurements (8-minute TUI RSS drift = 0 KB, extreme terminal resize
  `20×6`/`30×40`/`300×80`, hostile config battery, CLI bad-input table).
- **Professionalization critique & roadmap (`REVIEW.md`)** documenting 11
  code-verified P0 bugs/honesty cracks (all closed in this release) and the
  `v0.11.0` → `v1.0.0` roadmap.
- **Shared monotonic clock helper `ut_mono_ms()`** (`src/util.c`) and cached
  draw-path helpers (`profile_meta_summary()`, `db_up_cached()`).

### Fixed
- **Honest-reads pass (`REVIEW.md` 1.2, 1.3, 1.4, 1.10):**
  - Wi-Fi `wifi_level_dbm` unknown sentinel changed from `-1` to `0` (valid dBm
    is always negative) and inverted `detail_net` condition fixed.
  - Multi-line sysfs/procfs reads (`pp_dpm_sclk`, `/proc/pressure/*`) switched
    from first-line `ut_read_file()` to `ut_read_file_all()`, with case-insensitive
    `MHz`/`Mhz` matching in `hw_parse_pp_dpm()` (live-verified `sclk DPM`).
  - Stopped fans (`0 RPM`) now report `0` honestly (`>= 0`) instead of `--`.
  - Unreadable `nv_boost`/`nv_temp` and missing `hw_init()` telemetry fields now
    use `-1` sentinels; raw `-1`/`0` outputs eliminated across `--status`,
    `--watch`, `panel_telem_full.c`, and `panel_telemetry.c`.
- **Snapshot, CLI & privilege accuracy (`REVIEW.md` 1.1, 1.6, 1.10, 1.11, 2.2):**
  - Per-core `.ctr` export now writes `freq = core <id> <mhz>` (import stays
    backward-compatible with legacy `freq core ...`), `freq_replay` raised to
    `HW_CPU_MAX + 1`, and `profile_meta_parse()` ignores `core`/`ccd` lines so
    the aggregate `freq` is never overwritten with `0`.
  - `PREF_NO_SUDO` (`ut_sudo_fallback_allowed()`) is now enforced across all
    privileged paths (`cpufreq_write_all`, `ppt_write`, `ctrl_gpu_clock_lock`/
    `reset`, `db_start`/`db_install`) with honest error logs; `cpufreq_write_all`
    now tries direct `ut_write_file()` before batched `sudo -n tee`.
  - `ctron --tui` / `-t` now shares the `ut_singleton_enter("ctron")` lock, and
    the fallback pidfile without `XDG_RUNTIME_DIR` is UID-scoped
    (`ctron-tui-<uid>.pid`).
  - Failed CLI runs no longer call `settings_save()`; snapshot save/rename
    matches against sanitized names; per-core freq verification uses `±2 MHz`
    tolerance matching all-cores.
- **Monotonic timers & draw-path I/O (`REVIEW.md` 1.7, 1.9):**
  - Replaced all 3 wall-clock `time(NULL)` cache timers and 7+ duplicate
    `CLOCK_MONOTONIC` ms helpers with `ut_mono_ms()`.
  - Removed per-frame disk/heap/blocking-socket I/O: snapshot summary uses
    cached `profile_meta_summary()`; fan presets modal caches its preview per
    selection (zero per-frame `fan_ron_parse` `malloc`/`free`); `db_up_cached()`
    caps ping timeout at 100 ms with a 2 s monotonic TTL; `is_nixos()` is cached.
- **TUI SETTINGS, `editor_daeboard.c` & smoke harness (`REVIEW.md` 1.5, 1.8):**
  - SETTINGS `CLI SHORTCUTS (modes.ini)` is now fully keyboard-navigable
    (`Tab` or unified `j`/`k` walk, `Enter` applies); MODE EDITOR arrow keys no
    longer close the editor; dead code removed.
  - Added `editor_daeboard_close()` closing evdev `cap_fd` on `keypick` finish,
    ESC, outside-click (`ui.c`), and TUI shutdown; replaced hardcoded keyboard
    name with `EV_KEY` + capability discovery; added `ut_mkdir_p()` in
    `save_file()`, `ut_fgets_full()` in `load_file()`, and fixed dead arrow/`x`
    branches and `Tab` cycling into `focus == 2` (`ctron` command row).
  - Fixed `CTRON_CONFIG` / `tmpcfg` cleanup leak in `tui_smoke.py`
    `flow_telem_full`.
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
- **Documentation audit & streamlining** (`9072975`): verified `REVIEW.md`
  claim-by-claim against source and live `--status`; fixed broken GitHub
  anchors in `README.md` and stale paths across `AGENTS.md`, `SECURITY.md`,
  `HARDWARE.md`, and `AUDIT.md` (all categories A–L except `I2` field closed
  `[x]`); archived historical logs under `docs/history/`.

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

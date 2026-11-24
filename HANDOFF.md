# Ctron Handoff

*Older sessions (2026-09-19 through 2026-10-30) are archived verbatim in
[docs/history/handoff-2026-09-10.md](docs/history/handoff-2026-09-10.md).*

---

## Latest session — 2026-11-24 (Snapshot backend audit & two-pane interactive Snapshot Editor)

**Antigravity / Kerempkl / FA608PP** — audited the entire `.ctr` snapshot
pipeline (`src/profile.{h,c}`, `src/main.c`, `src/ui/ui.c`,
`src/ui/panel_profiles.c`), fixed 6 backend bugs, and replaced the black-box
Snapshot list with a two-pane Konsept C **Snapshot List + Live Inspector &
12-Field Interactive Editor**:

- **Snapshot backend fixes (`src/profile.{h,c}`, `src/main.c`, `src/ui/ui.c`):**
  1. **Stale TUI exports fixed:** entering `4 Snapshots`, opening `s`/`n`,
     saving, and `uu` overwriting now run `hw_refresh_live()` first so fields
     outside `hw_refresh_fast()` (`bat_limit`, `panel_od`, `cpu_boost`, `epp`,
     `hz_cur`, `ppt_*`) are never exported stale.
  2. **Unconditional fan clobber fixed:** `profile_import()` now initializes
     `fan_cpu_on`/`fan_gpu_on` to `-1` (omitted) and `has_curves = false`;
     `APPLY_SNAPSHOT` and `ctron snapshot apply` only call `ctrl_fan_write()`
     when `hw->fan_staged` is set by curve lines, and check `ctrl_fan_write()`'s
     return value instead of ignoring EC write failures.
  3. **Post-apply sync fixed:** `APPLY_SNAPSHOT` now runs `hw_refresh_live(hw)`
     at worker completion, and `finalize_apply()` runs `cf_reset_staging()` +
     `panel_profiles_reload()` so live diffs and Power/System/Core views update
     immediately.
  4. **Per-core & `ppt = off` diff + round-trip:** `profile_meta_t` now tracks
     `core_ids[]`/`core_mhz[]` and `ppt_off`, `profile_meta_diff()` detects
     `"cores"` and `"ppt"` (`off`) diffs, `profile_export()` preserves `# note:`
     when overwriting an existing snapshot, `profile_import()` skips redundant
     `gpu-clock = reset` when `gpu_clock_lock == 0`, and `profile_delete()` /
     `profile_rename()` remove empty 1-level group subdirectories.
- **Two-pane Snapshot Inspector & Interactive Editor (`src/ui/panel_profiles.c`):**
  - Left pane caches `s_meta[MAX_PROFILES]` on reload (zero per-frame disk I/O)
    and displays `≡ live` / `Δ N` badges per snapshot; `uu` double-confirms
    overwriting the selected snapshot from fresh live hardware.
  - Right pane shows all 12 `.ctr` fields alongside `(live: ...)` diffs; `e`/`l`
    or clicking any field enters edit mode (`h`/`l` step, `e` exact input, `x`
    toggles `— (skip)` for selective/partial snapshots via `profile_save_meta()`,
    `f` captures live fan curves or per-core caps, `w`/`Enter` saves).
- **Bonus bug caught by PTY smoke (`src/ui/editor_corefreq.c`):** untouched
  cores in inline `CORE LIMITS` now keep `cf_staged[id] == 0` and read live
  ceilings via `cf_eff(id)` instead of seeding on draw, preventing firmware
  thermal ceiling oscillation from triggering phantom per-core writes on `w`.
- **Verification:** `make clean && make HARDEN=1 && make test` and standard
  `make && make test` 0 warnings (including new unit tests in `check_snapshots`
  for selective `profile_save_meta`, fanless import, note-preserving overwrite,
  per-core/`ppt=off` diff, and empty group dir cleanup); UBSan + LSan clean;
  `clang --analyze` 0 findings on `profile.c`, `main.c`, `ui.c`,
  `panel_profiles.c`, `editor_corefreq.c`; `make tuitest` **13/13 flows (48/48
  assertions) PASS** (exit 0).

Signed: **Antigravity** — Snapshot backend fixes & interactive Snapshot Editor.

---

## Previous session — 2026-11-24 (`v0.12.0` Konsept C TUI redesign & unified keymap)

**Antigravity / Kerempkl / FA608PP** — dropped the `v1.0` milestone (`dde0ceb`),
designed interactive TUI concepts with the user, and implemented, verified, and
released **`v0.12.0`** (`REVIEW.md §3.6`) with **Konsept C (Left Rail + Full
Workspace)**, `5 Telemetry` as a first-class view, and configurable
`startup_view`:

- **`7baf997` (Konsept C TUI redesign, native themes, `startup_view`, unified keymap):**
  - Deleted duplicate `src/ui/panel_controls.c` and `src/ui/panel_telemetry.c`
    (net `-155` LOC overall). Replaced the 4-box neon Bento screen with thin
    top live summary header, slim 22-col left rail (`VIEWS 1–5` + compact `LIVE`
    metrics, auto-collapsing to inline top tabs below 96 cols), full-height
    workspace (`1 Power`, `2 Fans`, `3 Display & Light`, `4 Snapshots`,
    `5 Telemetry`), and unified 2-row status/progress/log + contextual keybar.
  - Added 5 restrained terminal-native palettes (`Native` default-bg transparent,
    `Slate`, `Gruvbox`, `Mono` default-bg, `Light`), unified `ui_ch()` channel
    helper (respects `default_bg` outside modals and solid `pal->bg` inside
    single-line `ui_window` modals), and full-width `ui_row()` selection bars.
  - Added `startup_view` (`0..4`) to `settings.{h,c}` (persisted in
    `settings.ini`, unit-tested in `check_settings_roundtrip()`, legacy layout
    keys tolerated silently), integrated `Quick mode` into `1 Power`, unified
    Display/Battery/Lighting + `ctrl_battery_oneshot()` into `3 Display & Light`
    (`APPLY_SYSTEM` worker), and mounted `panel_telem_full.c` directly in
    `rc_ws` as View `5`.
  - Unified keymap: `1`–`5` / `Tab` / `Shift+Tab` switch views, `t` toggles
    Telemetry, `Esc` strictly backs out / closes modals (never opens Settings),
    `,` opens Preferences, `L` Event Log, `?` floating Help modal, `e` edits
    exact values everywhere, `w`/`r` apply/revert staged edits. Also fixed
    `editor_fan_act()` leaving `fe_input` active on non-field button clicks.
- **`76f01dd` (TUI smoke suite updated for Konsept C):**
  - Rewrote `scripts/tui_smoke.py` covering 13 interactive flows (43/43
    assertions passing inside sandbox, exit 0), including `Esc` silent on main
    screen, `,` Preferences + `startup_view=4` direct boot into Telemetry,
    `1..5`/`Tab`, Power/System/Fan staging & presets, Fan clicks, Telemetry
    workspace + `1..8` detail jumps + reset/since, Snapshots cursor rename +
    double-`d` confirm, `L` Log + `?` Help, and 80-col rail auto-collapse.
- **Inline `CORE LIMITS` in `1 Power` (follow-up):** replaced the popup modal
  (`cf_overlay`) with an inline pane right inside `1 Power` (side-by-side when
  `rc_ws.w >= 89`, stacked when tall, compact swap on tiny terminals); `c` or
  clicking any core cell focuses it and `Esc`/`c` returns focus to Power rows.
- **Verification:** `make clean && make HARDEN=1 && make test` and standard
  `make clean && make && make test` 0 warnings; UBSan + LSan clean;
  `clang --analyze` 0 findings on all 9 modified C files; `nm -u build/ctron`
  0 `popen`/`pclose`/`system` symbols; `make tuitest` **13/13 flows (43/43
  assertions) PASS** (exit 0).
- **Release:** bumped `VERSION` (`src/main.c`, `README.md`, `flake.nix`) to
  `0.12.0`, synced `README.md`, `AGENTS.md`, `CHANGELOG.md`, `NEXT.md`,
  `REVIEW.md`, tagged `v0.12.0` (local commits & tag ready for user to push).
- **Next:** **`v0.13.0`** — Packaging, `udev` privilege reduction & desktop/CLI
  artifacts (`Makefile` `PREFIX`/`DESTDIR` + single `VERSION` +
  `make check-version`, `contrib/` `99-ctron.rules` + `ctron.tmpfiles.conf` +
  `ctron-ppt` + `sudoers` + `.desktop` + `PKGBUILD`, `man/ctron.1`, shell
  completions, `CONTRIBUTING.md`).

Signed: **Antigravity** — `v0.12.0` Konsept C TUI redesign & release.

---

## Previous session — 2026-11-24 (`v0.11.0` UX, CLI & zero-shell release)

**Antigravity / Kerempkl / FA608PP** — implemented, verified, and released
**`v0.11.0`** covering `REVIEW.md §2.1` and `§3.1–3.5` across 3 focused
commits plus release bump:

- **`143d2f7` (Paket 2A — Zero `/bin/sh` & zero `popen()`, `AUDIT.md C6` → `[x]`):**
  added pure-C `ut_find_in_path()` (`stat S_ISREG + X_OK`, zero fork/shell),
  rewrote `ut_have_cmd()` and `nvidia_smi_bin()`, migrated all 17 remaining
  `asusctl`/`busctl` calls (`control.c` ×13, `hw.c` ×4) to `ut_exec_argv*`,
  deleted `ut_exec` / `ut_exec_raw`, synced `SECURITY.md §2.2`, and verified
  zero `popen`/`pclose`/`system` symbols via `nm -u build/ctron`.
- **`e77622f` (Paket 2B — JSON/dry-run/diff CLI, exit code 3, `.ctr` scope):**
  added zero-dep `--status --json` and `--doctor --json` (unknown values emit
  JSON `null`), `cmd_validate()` / `mode_dry_run()` / `profile_dry_run()` and
  `--dry-run` across `--mode`, `snapshot apply`, and hardware flags,
  `snapshot diff <name>` (fixing missing `hw_refresh_live()` in `snapshot export`),
  `ut_priv_denied` sticky flag with exit code `3` (`0/1/2/3` contract in `--help`),
  and extended `.ctr` export/parse/diff/capture-line with `panel-od`, `cpu-boost`,
  `gpu-clock` (only `> 0`), and `ppt` (only all 3 `> 0`).
- **`2241130` (Paket 2C — Cursor `tinput_t`, `L` event log overlay, double-`d`, honest POWER):**
  extracted pure `src/tinput.{h,c}` (`Left`/`Right`, `Home`/`End`, `Del`,
  `Ctrl+A/E/K/U/W`, `|` mid / `_` end cursor rendering across all 6 UI inputs,
  notcurses Ctrl chord normalization in `dispatch_key()`, unit-tested in
  `check_tinput()`); added floating `EVENT LOG` overlay (`L` globally, `l` in
  LIVE, or click LIVE log row, 24 entries newest-first, red error highlight,
  `j`/`k`/`g`/`G` scroll); added double-`d` delete confirmation on snapshots
  and modes; guarded unsupported `asusd_ac`/`asusd_bat == -2` rows in
  `pw_nudge_row()` and displayed `-- (asusd absent)`.
- **Verification:** `make` & `make HARDEN=1` 0 warnings; `make test` +
  UBSan/LSan clean; `clang --analyze` 0 findings on all modified units;
  `nm -u build/ctron` 0 `popen`/`pclose`/`system` symbols; `python3 -m json.tool`
  validated live `--status --json` and `--doctor --json`; `make tuitest`
  13/14 flows (50/51 assertions, including mid-string cursor rename, double-`d`
  delete, and `flow_log_overlay`) pass in sandbox (only `mode_drift` requires
  host D-Bus/`asusctl`).
- **Release:** bumped `VERSION` (`src/main.c`, `README.md`, `flake.nix`) to
  `0.11.0`, synced `README.md` CLI help & TUI table, updated `CHANGELOG.md`/
  `NEXT.md`/`REVIEW.md`, tagged `v0.11.0` (local commits & tag ready for user
  to push).
- **Next (updated 2026-11-24):** `v1.0` milestone dropped (`0.x` continuous
  releases). Next up is **`v0.12.0` — TUI redesign & keymap overhaul**
  (`REVIEW.md §3.6`: replace the 4-box neon/faux-button "AI slop" layout with
  a clean terminal-native tab/workspace/unified-keybar TUI, `ESC` = back/cancel
  only, flat `1..N`/`Tab` navigation, conflict-free keymap), followed by
  **`v0.13.0`** (Packaging + `udev`/`tmpfiles` + `man`/completions) and
  **`v0.14.0`** (`CTRON_SYSFS_ROOT` + `--offline` smoke + CI + module split).

Signed: **Antigravity** — `v0.11.0` UX, CLI & zero-shell release.

---

## Previous session — 2026-11-23 (`v0.10.0` P0 accuracy & honesty release)

**Antigravity / Kerempkl / FA608PP** — committed the 2026-11-23 documentation
streamlining (`9072975`), then implemented, verified, and released **`v0.10.0`**
closing all 11 P0 findings from `REVIEW.md §1` plus `cpufreq_write_all`
direct-first across 4 focused commits:

- **`8a70b77` (Paket 1A — Honest reads, `AUDIT.md D5` → `[x]`):**
  `wifi_level_dbm` sentinel `0` + `detail_net` inverted check; `pp_dpm_sclk`
  and `/proc/pressure/*` `ut_read_file_all()` + case-insensitive `MHz` parser
  (live-proven `--status`: `iGPU busy : 0 % · sclk DPM 2/3 @ 600 MHz`);
  stopped fan `0 RPM` (`>= 0`); `nv_boost`/`nv_temp` `-1` sentinel + missing
  `hw_init()` sentinels; raw `-1`/`0` eliminated across `--status`, `--watch`,
  `panel_telem_full.c`, `panel_telemetry.c`.
- **`9d5caf9` (Paket 1B — Snapshots, CLI & privilege, `AUDIT.md E3, H4` → `[x]`):**
  per-core `.ctr` export (`freq = core <id> <mhz>`) + legacy `freq core ...`
  import compatibility + `freq_replay[HW_CPU_MAX + 1]` + `profile_meta_parse()`
  guard (round-trip tested); `PREF_NO_SUDO` enforced on all 4 privileged paths
  (`cpufreq_write_all` direct-first, `ppt_write`, `ctrl_gpu_clock_lock`/`reset`,
  `db_start`/`db_install`, tested via `check_no_sudo`); `--tui` singleton gate
  via shared `run_tui()` + UID-scoped `/tmp/ctron-tui-<uid>.pid` (tested +
  `SECURITY.md §2.3` synced); failed CLI skips `settings_save()`; snapshot
  save/rename matches sanitized name; per-core freq verify `±2 MHz`.
- **`b349b6c` (Paket 1C — Monotonic timers & draw-path I/O, `AUDIT.md A1, K4` → `[x]`):**
  unified `ut_mono_ms()` replacing 3 `time(NULL)` caches and 7+ duplicate
  `CLOCK_MONOTONIC` helpers; `profile_meta_summary()` eliminates per-frame
  disk opens in `panel_profiles.c`; fan presets modal caches preview per
  selection (zero per-frame `fan_ron_parse` `malloc`/`free`); `db_up_cached()`
  (100 ms ping cap, 2 s monotonic TTL) + cached `is_nixos()`.
- **`b1eabe3` (Paket 1D — SETTINGS, `editor_daeboard.c` & smoke, `AUDIT.md C1` → `[x]`):**
  SETTINGS `CLI SHORTCUTS` keyboard navigation (`Tab` + unified `j`/`k` walk,
  `Enter` apply) + MODE EDITOR arrow fix & dead-code removal;
  `editor_daeboard_close()` closing evdev `cap_fd` on all exits (`keypick`,
  ESC, outside-click `ui.c`, TUI shutdown), `EV_KEY` capability keyboard
  discovery, `ut_mkdir_p()` in `save_file()`, `ut_fgets_full()` in `load_file()`,
  fixed arrow/`x` branches and `Tab` → `focus == 2` (`ctron` row);
  `tui_smoke.py` `flow_telem_full` `CTRON_CONFIG`/`tmpcfg` cleanup.
- **Verification:** `make` + `make HARDEN=1` 0 warnings, `make test` + UBSan/LSan
  clean, `clang --analyze` 0 findings on all modified units, `--doctor` &
  `--status` live-verified, `make tuitest` 12/13 flows pass in sandbox (only
  `mode_drift` needs host D-Bus/asusctl). All `AUDIT.md` A–L items except `I2`
  (Arcioth field) are `[x]`.
- **Release:** bumped `VERSION` (`src/main.c`, `README.md`, `flake.nix`) to
  `0.10.0`, updated `CHANGELOG.md`/`NEXT.md`/`REVIEW.md`, tagged `v0.10.0`
  (local commits & tag ready for user to push).
- **Next:** start **`v0.11.0`** (`--json`, `snapshot diff`, `--dry-run`, exit
  codes, TUI LOG viewer, cursor `tinput_t`, double-`d` delete confirmation,
  `.ctr` scope completion, zero-shell `ut_find_in_path` + `ut_exec_argv`).

Signed: **Antigravity** — `v0.10.0` P0 closure & release.

---

## 2026-11-23 — `REVIEW.md` verification + full documentation audit & streamlining

**Antigravity / Kerempkl / FA608PP** — user asked to verify the newly
created `REVIEW.md` against the real codebase, then update `REVIEW.md`,
check all docs for accuracy, and simplify/streamline them. Docs-only
session; zero source files modified.

- **`REVIEW.md` verified claim-by-claim against source & live `--status`:**
  all 10 original P0 findings confirmed (1.3 `pp_dpm_sclk` live-proven:
  `./build/ctron --status` drops `· sclk DPM 2/3 @ 600 MHz` because `*` is
  on line 2). Updated `REVIEW.md` with verified catches and technical
  corrections:
  - **1.1 regression trap added:** changing export to `freq = core %d %d`
    without updating `profile_meta_parse` (`src/profile.c:493-522`) causes
    `atoi("core ...")` to overwrite `m->freq` with `0`.
  - **New P0 (1.11):** `PREF_NO_SUDO` (`ut_sudo_fallback_allowed()`) is
    bypassed in 4 privileged paths (`cpufreq_write_all`, `ppt_write`,
    `ctrl_gpu_clock_lock`/`reset`, `db_start`/`db_install`).
  - **2.2 technical correction:** `cpufreq_write_all` currently invokes
    `sudo -n tee` *before* direct `ut_write_file()`, and
    `/sys/firmware/acpi/platform_profile` + `cpufreq/boost` need
    `systemd-tmpfiles.d` alongside `udev` (`99-ctron.rules` +
    `ctron.tmpfiles.conf`).
  - Additional verified catches folded in: `hw_parse_pp_dpm` `MHz` vs
    kernel `Mhz`, `nv_boost`/`nv_temp` `0` vs `dash_if < 0`,
    `gpu_vram_used_mb` / partial-PPT raw `%d`, `editor_daeboard.c` dead
    arrow/`x` branches + unreachable `Tab` `focus == 2` + `ui.c:509` close
    hook, `fan_ron_parse` per-frame `malloc`/`free`, exact `ut_exec` counts
    (13 in `control.c` + 1 `popen`, 4 in `hw.c`), 7+ `CLOCK_MONOTONIC` ms
    copies, unused `telem_spark()` & `ACT_SET_MODE_*`, `tui_smoke.py`
    `flow_telem_full` `CTRON_CONFIG`/`tmpcfg` cleanup leak, `Makefile`
    `PREFIX ?= $(HOME)/.local` recommendation.
- **All repository documentation audited & streamlined (lossless archive):**
  - Moved historical `PLAN.md` & `PLANS.md` to `docs/history/`; archived
    2026-09/10 `HANDOFF.md` (2408 lines) to
    `docs/history/handoff-2026-09-10.md` and full detailed `CHANGELOG.md`
    (2862 lines, restoring 3 missing 2026-11-17 headings) to
    `docs/history/changelog-2026-09-11.md`.
  - Root `CHANGELOG.md` rewritten in release-level Keep a Changelog format;
    `NEXT.md` stripped of 15 already-closed `[BİTTİ]`/`[KAPANDI]` items so
    it holds only open work, version policy, parked list, and lessons.
  - Fixed real drift across active docs: **2 broken GitHub anchors** in
    `README.md` (`<a id="...">` added before `<details>`), `min_value`/
    `max_value`, synced parked list & doc table; **`AGENTS.md`** fixed
    stale `gpu_metrics`, `sparkline`, `kernel 7.2`, workspace view list,
    missing `settings.c`; **`SECURITY.md`** fixed stale `ctrl_ppt_*_cmd`
    names, contradictory Phase 1.2 note, honest current pidfile path;
    **`HARDWARE.md`** fixed stale `ctron-ppt` future-opt-in note (EN+TR),
    stale `v0.7.0` tag, typo `boştayla`; **`AUDIT.md`** removed duplicate
    `F4` entry, fixed typos, and honestly reopened `A1`/`C1`/`D5`/`E3`/
    `H4`/`K4` to `[~]` with `REVIEW.md` pointers.
- Next: commit doc cleanup (if user wants) and start **`v0.10.0`** (P0
  bug/honesty pass from `REVIEW.md` §1).

Signed: **Antigravity** — `REVIEW.md` verification & full doc audit/streamlining.

---

## 2026-11-20 — full codebase read + professionalization critique (`REVIEW.md`)

**Antigravity / Kerempkl / FA608PP** — full codebase read and initial
creation of `REVIEW.md` covering 10 code-verified P0 bugs, P1/P2
architecture/UX/packaging roadmap (`v0.10.0` → `v1.0.0`). Docs-only.

Signed: **Antigravity** — full codebase read, initial `REVIEW.md`.

---

## 2026-11-19 — file audit + README renewal

**GLM / Kerempkl / FA608PP** — verified zero empty files; verified every
`README.md` claim against code/binary; updated kernel support claim to
`7.2–7.3` (live re-verified on `7.3.1-cachyos`: 0 warnings, `make test`,
`make tuitest`, `--status`, `--doctor`; synced `HARDWARE.md` EN+TR);
re-shot `docs/tui-{fan,power,telem}.png` and verified pixel stats.

Signed: **GLM 5.3** — file audit, README/HARDWARE renewal, screenshots.

---

## 2026-11-17 — audit rounds A1–L3 (`AUDIT.md` born & processed)

**GLM / Kerempkl / FA608PP** — created `AUDIT.md` (categories A–L, ~50
items) and audited every category except `I2` (Arcioth field items):

- **Fixes landed (7):** `ut_fgets_full()` whole-line config parsing (A5);
  `profile_set_note` refuses non-round-trip rewrites (A5); `db_reload`
  clears `err` on entry (A6); `hw_parse_gpu_csv` upper-bound UB guards +
  `cpu_mhz_limit` bound (A7); freeze-frame `s_hw_frozen` draw copy during
  background apply (B2/BULGU 5); `ut_atomic_begin`/`commit` across all 6
  config writers (J1); persistence failure logging + `ut_log` `localtime_r`
  guard (L2/K4).
- **Hardenings & evidence:** `-k1` KILL escalation on all 4 `timeout`
  sites (C4); hostile-config + RON drift test battery (D2/D7); extreme
  resize live proof `20×6`/`30×40`/`300×80` (F3); 8-minute TUI RSS flat
  at `45588 KB` with 0 KB drift (G4); live CLI bad-input table (H1).
- Verified: 0 warnings, `make test`, full `make tuitest`, `clang --analyze`
  + UBSan + LSan clean.

Signed: **GLM 5.3** — audit rounds A–L, fixes, regression tests.

# Ctron Handoff

*Older sessions (2026-09-19 through 2026-10-30) are archived verbatim in
[docs/history/handoff-2026-09-10.md](docs/history/handoff-2026-09-10.md).*

---

## Latest session — 2026-11-23 (`v0.10.0` P0 accuracy & honesty release)

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

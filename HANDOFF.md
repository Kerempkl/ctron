# Ctron Handoff

Latest session — **2026-10-03** **GLM / FA608PP** (full code audit +
P1–P3 fixes, see below). Before that **2026-10-02** **Kerempkl + GLM /
FA608PP** (two
rounds: cpufreq writes + per-core indexing on real kernel cpu ids,
then the CORE LIMITS physical-core/CCD view). Before that
**2026-10-01** (asusd power-source profile takeover managed from
ctron + its two follow-up fixes; then sysfs path cache + Makefile
hardening), **2026-09-29** (per-core CPU frequency limits), the
**2026-09-28** run (preset button row, 6.18-lts re-verification +
HARDWARE.md, list scrolling, pty harness, fan write verify/flash,
fan-button fix), the **2026-09-27** docs (support matrix),
**2026-09-26** (exact-value typing + range hints, amd-pstate
clock-window refresh), the **2026-09-24** POWER round, and
**2026-09-23** in parallel: **Grok / FA507NVR / NixOS** (daeboard
editor, signed below). **Read `NEXT.md` first** — it carries the
prioritized todo list and the distilled session lessons.

## Session 2026-10-03 — full code audit + P1–P3 fixes (GLM, FA608PP)

- Full audit first: ~10.5k lines read end to end (core + ui + display +
  tests + scripts), make/test/tuitest baselined. Findings handed over in
  chat as a P1–P4 report; the user picked the P1+P2+P3 scope and the
  `fan_staged` flag approach for the fan fix. Nothing else touched.
- **P1 asusd.ron fallback**: `ut_read_file` is first-line-only (fgets);
  the daemon-unreachable fallback parsed the 51-line asusd.ron with it —
  first line is "(", so takeovers always parsed as off. New
  `ut_read_file_all()` + pure `hw_asusd_ron_parse()` (unit-tested).
- **P1 kbd sysfs fallback**: wrote "low"/"med" strings into a brightness
  node that takes an integer — always FAILED without asusctl. Numeric
  now.
- **P1 fan staging**: editor edits live in `hw->fan_cpu/fan_gpu` and any
  `hw_refresh_live` (`fan_hwmon_read`) clobbered them — switching to
  POWER, applying a mode, an Apply pass silently dropped un-written
  curve edits. New `fan_staged` flag: editor/mode-steps/profile-import/
  settings-load raise it, `fan_hwmon_read` skips the refill while set,
  `ctrl_fan_write` clears it after a verified write (kept on VERIFY
  FAILED so a retry survives).
- **P2**: `mode add` OOB stack write on a full table → clean error (the
  table starts with 5 seeded modes, so the guard bites at the 28th add);
  `tinput_t.buf` 80 → MODE_STEPS_MAX (mode-editor steps were silently
  truncated to 79 chars on save), display snprintfs got precisions.
- **P3**: gcc 16.2.1 warnings (5× -Wformat-truncation in
  editor_daeboard.c — HANDOFF's earlier "warning-free" claims predate
  this gcc — + test_core `_GNU_SOURCE` redefine) fixed via precision +
  guard; `DBGHZ` stderr line + double `hw_refresh_live` at TUI start
  removed; kde_query dead /tmp dump and panel_profiles dead code
  removed.
- Tests: `check_asusd_ron_parse` + `check_read_file_all` in test_core;
  `flow_fan_staging` in tui_smoke (isolated CTRON_CONFIG — real
  settings.ini and EC untouched). Mutation-proven: with the guard
  removed the flow fails with "staged edit lost". Notcurses emits frame
  diffs — the flow keys on changed-digit runs and reads the full chip
  only after the full FAN repaint.
- **Arcioth note**: editor_daeboard.c got snprintf precision specifiers
  only — no behavior change; the NixOS gcc warnings should clear too.
- Verified: `make` zero warnings, `make test`, `make tuitest` (6 flows
  green), `--doctor`/`--status`, the 33rd `mode add` rejects cleanly,
  `make install`. All uncommitted — suggested commit split is in the
  handover message.

### Same-day follow-up (user-committed the fixes as 5bbaa46, then):

- User report: "f opens FAN but POWER/LIGHT/? unreachable from the
  keyboard". pty-measured the truth: letters only switch views from the
  workspace focus with shift (p/l are bound by the fan editor and the
  power/light rows), other panels ignore them, and an accidental 'p'
  makes the typing field swallow the next key ('?' included).
- Landed: **view hotkeys 5..8** (fan/power/light/help) in
  `dispatch_key` — global, any focus, no view binds digits; tab labels
  show them (" 6:POWER "); help view documents them; dead `case 'P'`
  removed from editor_fan (lowercase 'p' still types pwm). Letter
  aliases (f/p/l/shift) unchanged. `flow_view_hotkeys` (tui_smoke,
  7th flow) drives 6→5→7→8 from the default CONTROLS focus — green.
  make 0 warnings, make test, make tuitest (7 flows), installed.

### Same-day follow-up 2 — M4/Armoury Crate key → ctron (KDE)

- User asked to bind the M4 key to ctron on KDE. Identity verified with
  evtest (user capture on `/dev/input/event11`, "Asus WMI hotkeys"):
  the key emits scancode `0x26` → `KEY_PROG3` → XKB `XF86Launch3` (XKB
  keycode = Linux input code **+ 8**: 202 → `<I210>`) → Qt
  `Key_Launch3` = 0x010000a5. First guess PROG1/XF86Calculator was
  wrong on this machine.
- Plasma 6.7 quirks mapped: kglobalaccel runs **inside kwin** (the
  standalone `plasma-kglobalaccel.service` exits 0 right after start —
  normal); application shortcut components live per desktop-id (the
  `btop.desktop` pattern) and only become ACTIVE at session start —
  live registration via `busctl` doRegister+setShortcut works and
  persists but stays inactive until re-login (user test pending).
  Prepared: `~/.local/share/applications/ctron.desktop`
  (`konsole -e ~/.local/bin/ctron`) + `kglobalshortcutsrc`
  `[services][ctron.desktop] _launch=Launch (3)` (written by the
  daemon itself). qdbus6 cannot pass `ai` types — busctl is required.
- Docs landed (user-picked scope: docs only): HARDWARE.md EN/TR notes
  bullet + README "KDE: Armoury Crate (M4) key" section — GUI binding
  steps, evtest diagnosis, the +8 keycode trap, kwin-grab behaviour.
  No code changes.

### Same-day follow-up 3 — CONTROLS Mode row: applied mode + drift star

- User idea, landed with the resource-minimal (snapshot) design they
  picked: the Mode row shows `-` before any apply, `name` while clean,
  `name*` on drift. The arrow + candidate (`- → name`, `name* → other`)
  is latched behind actual picker interaction (h/l or a click) after a
  user tweak — startup and untouched rows stay bare. Pure helpers in
  modes.c — `mode_touch_mask` (steps → tracked-field bits),
  `mode_snapshot` (captures readable fields, drops unreadable ones
  from the mask), `mode_drift_count` (stale reads never drift) —
  unit-tested in test_core; capture in `ctl_capture_mode` from both
  apply sites (controls row + settings overlay); draw-time compare
  only. Session-only, no persistence.
- Bonus: asusd's takeover profile revert now surfaces as `name*` in the
  Mode row without any extra reads.
- `flow_mode_drift` (tui_smoke, 8th flow): isolated config with a
  one-step `[drift] profile quiet` mode, real profile saved/restored.
  Harness lesson: notcurses renders diffs, so a whole log line ("mode
  'drift' applied") never appears contiguous — wait for the
  `applying mode...` flash plus a short fragment (`drift'`) instead.
- make 0 warnings, make test, make tuitest (8 flows), installed.

## Session 2026-10-02 — topbar focus chips fix (Kerempkl + GLM, FA608PP)

- User report: the `1:profiles…4:telemetry` chips "only appear after
  pressing 1/2/3/4". Two causes, both fixed in `draw_topbar` (ui.c):
  the fixed `dimx-44`+12-stride layout pushed the last chip past the
  edge (clipped — pty-harness-proven), and unfocused chips used the
  muted colour, unreadably dim on real screens. Chips now right-align
  from real widths; unfocused use pal->text. Repro script captured
  first-frame bytes before/after (in /tmp, ad-hoc). Note: the harness
  Session class is importable — a tracked
  scripts/__pycache__/*.pyc got dirtied and was restored; consider
  gitignoring __pycache__ someday.
- User committed the asusd warning round themselves (c968e14).
  Uncommitted: this ui.c fix + CHANGELOG/HANDOFF edits.

## Session 2026-10-02 — asusd takeover conflict warning (Kerempkl + GLM, FA608PP)

- List item 4 landed: `hw_asusd_enforced()` (pure, unit-tested) + three
  surfaces — POWER profile-row `⚠asusd` marker, rising-edge telemetry
  log, apply-toast note, plus an `--status` `asusd note` line. Dormant
  here (takeovers deliberately off); FA507NVR line benefits most.
- Same day earlier: the user asked about the v0.2.1 tag — explained
  what a tag is (no code change); user then asked for the ryzenadj
  NEXT note and retracted it ("yanlış anlamışım") — reverted both
  docs, nothing landed.
- Verified: warning-free `make`, `make test`, `make tuitest`,
  installed. Uncommitted: this change + doc edits; commit message
  handed over.

## Session 2026-10-02 — all-cores freq write verify + amd-pstate clamp forensics (Kerempkl + GLM, FA608PP)

- User hit "CPU clock limit stays 2401 whatever I apply". Forensics:
  the write path works (sudoers got a path-scoped rule — plain
  `sudo -n tee /dev/null` fails but the sysfs tee succeeds), but the
  kernel clamps scaling_max_freq to nominal 2401 synchronously right
  now (raw tee: 2402→2401; 2000 sticks; cpuinfo_max still 5386,
  boost on, scaling_min also moved 421→1492 → amd-pstate re-baselined
  the policy window after the day's OCCT + profile churn). Not
  userspace-fixable; expect reboot/module reload to clear. If it
  reproduces after OCCT on kernel 7.2, compare the 6.18-lts boot and
  consider reporting upstream. OCCT numbering note: it enumerates
  adjacent logical pairs (Windows-style), so its rows 0-3/8-11 =
  CCD1's two thread blocks, 4-7/12-15 = CCD2's — not a ctron bug.
- ctron fix (rule 4): `ctrl_set_cpu_max_mhz` now reads back every
  present cpu (±2 MHz tolerance) and on a driver clamp reports
  "kernel kept N MHz on x/y cpus" + returns failure (CLI exit 1,
  toast shows it). The running TUI must be restarted to pick it up.
- User committed the earlier rounds themselves (fc5acf6, 2e7325d,
  83aca82). Uncommitted: this control.c fix + doc edits.

## Session 2026-10-02 — CORE LIMITS physical-core/CCD view (Kerempkl + GLM, FA608PP)

- User's idea, landed: the CORE LIMITS editor shows physical cores
  (`c0·16` SMT pairs) under CCD headers (L3 domains; AMD → `CCD1/2`,
  else `L3-n`). Cell edits stage both threads; header selection makes
  h/l/t/o/a operate on the whole CCD; `a` = value across its CCD,
  `A` = all cores. Topology from sysfs only (thread_siblings_list +
  level-3 shared_cpu_list), built once at init; `topo_odd` falls back
  to the old per-thread grid (SMT-off fine, no-L3 → single group,
  >2 threads per core → fallback). Pure `hw_topology_group` unit
  -tested; tuitest asserts CCD headers on the real machine (passed).
- Verified: warning-free `make`, `make test`, `make tuitest`
  (10 flows). Interactive field-test on a real terminal pending;
  real writes still need the sudoers fix (see below).

## Session 2026-10-02 — real cpu ids in the cpufreq write path (Kerempkl + GLM, FA608PP)

- The small list item landed: `cpufreq_write_all` (EPP/scaling to all
  policies) now iterates `hw->cpu_ids` instead of scanning cpu0..N and
  breaking at the first gap (sparse present lists like "0-15,32-47"
  would silently skip everything after the hole). Per-core indexing
  unified on real kernel ids end-to-end: `hw_state_t.cpu_ids[]`,
  `hw_cpu_present()`, CORE LIMITS editor (cf_sel/mouse = grid
  position, cells/staging/labels = real id), `freq core N M`
  validation, profile export. Behaviour identical on contiguous
  machines. The promised topology note is in place.
- Environment finding: **passwordless `sudo -n tee` is gone on
  FA608PP right now** (the 09-27 fan-write verify needed it, so
  sudoers changed since). Privileged writes fail cleanly ("command
  failed" / toast "· N failed") on old and new binary alike — not a
  regression. Restore the sudoers rule to re-enable writes.
- Verified: warning-free `make`, `make test`, `make tuitest` (incl.
  corefreq overlay flows); installed to `~/.local/bin/ctron`.
  Uncommitted at handoff: this change + doc edits; the user takes the
  commit (message provided in chat).

## Session 2026-10-01 — sysfs path cache + Makefile hardening (Kerempkl + GLM, FA608PP)

- Makefile: required flags appended after external CFLAGS/LDLIBS
  (packaging/Nix/debug envs no longer drop -Isrc/-D_GNU_SOURCE/pkg
  flags); pkg-config-less fallback to -lnotcurses -lnotcurses-core;
  dead `CC ?= gcc` removed (make's builtin cc always won). Verified:
  external-flags build, pkg-config-stripped fallback, default build +
  tests + harness green.

- NEXT.md item 1 closed: hw_state_t.paths caches the five device
  paths (fan curve/k10temp/fan RPM/battery/mains); failed reads drop
  the entry (one-tick re-probe after suspend/resume renumbering);
  the fan WRITE path resolves through the same cache. Honest result:
  no measurable CPU-time change on FA608PP (globs were cheap); the
  win is ~18 fewer opens/poll + read/write consistency. All tests +
  live stream green.

## Session 2026-10-01 — asusd takeover management (Kerempkl + GLM, FA608PP)

- Second follow-up same day: changing an auto-profile row also
  switched the ACTIVE mode at apply. Isolated live: `asusctl profile
  set -a X` applies X immediately when on that power source (flag
  irrelevant). ctrl_set_asusd_auto now snapshots the active profile
  first and restores it if the side-effect moved it; pw_apply's
  explicit profile-row write happens before, so user-chosen modes
  still win. Verified live (quiet held while AC-auto Performance
  applied) + harness green.
- Follow-up same day: user hit a display bug — apply from "off" worked
  but the row stayed "off". The read-back refresh raced asusd's
  asynchronous ron flush; the read layer now prefers daemon-live state
  (hw_asusd_auto_flag/profile, shared with control's verify), ron file
  is only a no-daemon fallback. --status shows the asusd auto line;
  verified immediately-correct after every apply. Takeover now left
  OFF on both sides per user preference (was restored to original
  earlier in the session).

- User bug: profile set from KDE flipped back to Performance within
  seconds (fan noise included). Root cause chain proven live: asusd
  enforces PlatformProfileOn{Ac,Battery} on power events; USB-C PD
  emits ~59 power-supply uevents / 90 s on this machine. Not ctron
  (verified absent + startup-write audit clean).
- POWER: "AC auto-profile" / "Battery auto-profile" staged rows (off/
  Quiet/Balanced/Performance, `--` without asusd), applied last.
  Writes: `asusctl profile set -a/-b <name>` + `busctl set-property`
  on xyz.ljones.Asusd for the change flags; no restart, no file
  editing. CLI `--ac-profile/--battery-profile`; modes/profiles
  inherit.
- Two live-caught implementation traps, both fixed: the ron file
  flushes asynchronously (verify reads the daemon via asusctl profile
  get / busctl get-property instead), and ut_exec's default capture
  truncates at the first newline (use ut_exec_raw for multi-line).
  Earlier trap: ron lines are indented + " Balanced," needs trim.
- Live-verified every form incl. restore of the original config;
  warning-free make, unit + TUI tests green. Uncommitted at handoff
  per protocol.
- FYI for the user's own machine: the takeover is currently left in
  its ORIGINAL state (AC Performance on, battery Quiet on) — disable
  with `ctron --ac-profile off` or the POWER row when desired.

## Session 2026-09-29 — per-core frequency limits (Kerempkl + GLM, FA608PP)

- New `src/ui/editor_corefreq.c` overlay (POWER **c** / ` Cores `
  button): grid of cNN cells, staged edits, `w` writes only changed
  cores via `ctrl_set_cpu_max_mhz_core` (per-core read-back verify +
  summary log). hw gained `cpu_n` + `cpu_mhz_core[]` (the existing
  sweep fills them — zero extra reads); `hw_cpu_list_parse()`
  handles comma cpu lists (unit-tested).
- `freq` command: all-cores form unchanged + `core <N> <MHz>` —
  modes/profiles inherit it; profile export appends per-core lines
  when non-uniform (chosen persistence: profiles, no settings.ini
  auto-restore). CLI: `ctron --freq core 4 3000`.
- Found live and fixed: the CLI flag path ran before any sweep, so
  `cpu_n` was 0 — `hw_init` now sweeps once.
- Verified: warning-free `make`, unit + TUI tests (new side-effect-
  free corefreq flow), live write/read-back/restore on core 4,
  invalid-id rejection. Uncommitted at handoff per protocol.

## Session 2026-09-28 — list scrolling (Kerempkl + GLM, FA608PP)

- NEXT.md item 4 closed: PROFILES and the settings-overlay CLI
  SHORTCUTS lists scroll to follow the selection (`prof_top`,
  `set_mode_top`, clamped during draw; ▲/▼ hint markers). The easy
  pick of the session — tctl gauge (item 3 leftover) was rejected
  first: k10temp exposes no `temp1_crit`, so a ceiling would have to
  be hardcoded (honest-reads violation).
- Same session: POWER preset button row and the 6.18-lts
  re-verification (sections below).
- Verified: warning-free `make`, `make test`, `make tuitest` green,
  installed. Uncommitted at handoff per protocol.

## Session 2026-09-28 — POWER preset button row (Kerempkl + GLM, FA608PP)

- POWER layout: presets are now one "Presets" row (EPP → presets →
  watts) with Q45/B60/P80 as side-by-side buttons; armed = staged
  triple match, custom shows "custom". h/l walks and stages, Enter
  applies, clicks stage directly (ACT_WS_PW_PRESET_BASE 17..19,
  label click selects the row). PW_ROWS 14 → 12; `t` no-op there.
- Same session: LTS-kernel re-verification (below) — HARDWARE.md now
  lists kernels 7.2 and 6.18-lts.
- Verified: warning-free `make`, `make test`, `make tuitest` green,
  installed. Uncommitted at handoff per protocol.

## Session 2026-09-28 — LTS-kernel re-verification (Kerempkl + GLM, FA608PP)

- Kernel switched to 6.18.52-1-cachyos-lts; full verification green:
  warning-free `make`, `make test`, `make tuitest` (fan write+verify
  click included), `--doctor` (asusctl/armoury/ppt sysfs all yes),
  live `--status` (amd-pstate ceiling 5353/5386, custom curve hwmon,
  BAT1 limit 70%, KDE 165 Hz). HARDWARE.md updated in both languages.
- `sudo -n` shows "unavailable" in this boot (environment, not the
  kernel): privileged writes still land via asusctl/direct sysfs —
  the fan-verify harness flow proves the write path.
- Everything from 09-27 (harness, fan verify/flash, button fix,
  version, matrix) still uncommitted in the worktree at handoff.

## Session 2026-09-27 — pty harness in-tree (Kerempkl + GLM, FA608PP)

- NEXT.md item 2 closed: `make tuitest` runs `scripts/tui_smoke.py`
  — pty + one-second query responder (CPR/DA1/kitty/sync/OSC) + four
  flows (open/quit, settings overlay, POWER net-zero apply, fan
  button regression + Write click). Three consecutive green runs.
- Two behavioural notes encoded in the harness: lowercase `p` is the
  fan editor's pwm key, so POWER is reached with `P`; the POWER flow
  stages `l`+`h` (net zero) so Apply logs without touching hardware.
- The fan Write-click flow performs a real no-op curve re-write —
  needs the TUI's usual privileges; read-only flows run without.
- Session protocol unchanged: no commits by the agent; message handed
  over. Uncommitted at handoff: harness + fan-verify/flash + button
  fix + version/docs from earlier today.

## Session 2026-09-27 — fan write verify + ui_flash (Kerempkl + GLM, FA608PP)

- NEXT.md item 5 closed: `ctrl_fan_write` reads back all 8+8 points
  from the custom-curve hwmon and logs `verified 8/8 + 8/8 pts` or
  `VERIFY FAILED: cpu n/8, gpu n/8`; the padding rule lives in the
  shared `fan_point()` so write and verify agree. Live-checked with a
  no-op `--fan-curve` re-apply on FA608PP (curves unchanged).
- `ui_flash()` (ui.c): "applying..." on the telemetry log row with a
  synchronous render before fan writes, fan presets, profile apply,
  mode bundles and POWER apply — kills the frozen-UI feel. CLI-safe
  (no-ops without the TUI).
- **Fan-editor button collision (user hit it live):** `ACT_FE_*` ids
  were registered under TGT_PANEL_WORKSPACE and collided with
  `ACT_WS_*` — clicking Write opened Help, CPU/GPU/+/- opened tabs.
  Fixed same session: ids rebased to 100..159, `panel_workspace_act`
  forwards them to `editor_fan_act`, which now has a full button
  dispatch (Write flashes "applying..." and writes). daeboard editor
  unaffected (own TGT panel).
- Verified: warning-free `make`, `make test`, installed; uncommitted
  at handoff per the commit protocol.

## Session 2026-09-27 — HARDWARE.md (Kerempkl + GLM, FA608PP)

- Docs only: new `HARDWARE.md` — bilingual (EN first, TR below)
  device-class support matrix with the four dependency layers, the
  two verified machines and their quirks, honest-degradation and sudo
  notes. README intro summarizes and links instead of carrying the
  table. No code touched. Uncommitted at handoff per the user's
  commit protocol — ready message handed over.

## Session 2026-09-26 — amd-pstate clock window refresh (Kerempkl + GLM, FA608PP)

## Session 2026-09-26 — amd-pstate clock window refresh (Kerempkl + GLM, FA608PP)

- User bug: on battery (EPP power / quiet) the CPU clock window stayed
  "300–2400" after changing EPP. Two causes: `cpu_mhz_min/max` were
  read once at startup from cpu0 only, while amd-pstate re-negotiates
  per-core ceilings dynamically (2401↔5386 MHz observed within
  seconds, cores diverging); and the quiet platform profile itself
  holds the ceiling at the base clock (2401) regardless of EPP, on AC
  or battery — Balanced/Performance widen to 5386 in ~3 s. Fix:
  `hw_refresh_fast` sweeps cpuinfo/scaling across all present CPUs and
  keeps the widest window; polls, Apply and view entries follow the
  kernel. KDE ppd re-asserts its EPP on profile changes — ctron's EPP
  row overrides on Apply.
- Commit protocol change (user's rule from today): I do NOT run
  `git commit` anymore — hand over a ready commit message, the user
  reviews and commits. This fix is uncommitted in the worktree with
  its CHANGELOG/HANDOFF entries.
- Verified: warning-free `make`, `make test` ok, installed to
  `~/.local/bin/ctron`. TUI field-test (window refresh after EPP/
  profile change) pending on the real terminal — the running TUI must
  be restarted to pick the fix up.

## Session 2026-09-26 — POWER t-typing + range hints (Kerempkl + GLM, FA608PP)

- The last two items of the POWER usability list: `t` on a numeric
  row types an exact value (seeded with the staged value, digits
  only, Enter stages clamped like a nudge, Esc cancels; a view
  switch or any click ends typing), and plain rows show their allowed
  window (`65 W (15–90)`), firmware windows from a 1 s-cached
  `ctrl_ppt_limits`. Dirty rows keep the arrow form. Help documents
  `t`.
- README had an uncommitted staged edit (v0.2.1 rebrand) from a
  parallel line — deliberately left out of this commit.
- Verified: `make` warning-free (ctron side), `make test` ok,
  installed to `~/.local/bin/ctron`, `--status` live. Interactive
  TUI field-test on a real terminal still pending (toast, EPP rows
  and t-typing all untested by hand).

## Session 2026-09-24 — POWER apply toast (Kerempkl + GLM, FA608PP)

- Apply now answers visually: a 5 s toast above the Apply/Revert
  buttons lists what changed (`✓ SPL 65→80 W · EPP performance`),
  red with `· N failed` when writes fail. Theme-independent green
  `0x33FF66` / red `0xFF4D5E`; a list row yields its place while the
  toast is up so small terminals don't overlap. `pw_msg`/`pw_msg_ms`/
  `pw_msg_fail` in ui_ctx_t; `pw_diff_summary()` captures the diff
  before apply collapses staging onto live values.
- Fixed on the way: `pw_touched` bitmask (PW_T_*) marks user-edited
  fields; stale nb-wmi reads (0 W) no longer turn staged defaults into
  writes (EPP-only staging used to rewrite PPT).
- Verified: warning-free `make` (ctron side), `make test` ok,
  installed to `~/.local/bin/ctron`, `--status` live. Interactive TUI
  field-test on a real terminal pending at handoff.

## Session 2026-09-24 — POWER usability round (Kerempkl + GLM, FA608PP)

- POWER gained two staged rows: Platform profile and EPP preference
  (first TUI editor for EPP). Apply order: profile, then EPP, so a
  staged EPP beats the profile-implied one.
- Enter applies the staged bundle (was: steps the value like
  right-arrow); h/l/←/→ are the only staging keys, `w` stays as an
  alias. Mouse second-click on a row still stages it; the Apply button
  remains the click target.
- q with staged edits warns once and needs a second press (any panel);
  the warned flag resets in `pw_sync_from_hw()`.
- Verified: warning-free `make` (ctron side), `make test` ok,
  installed to `~/.local/bin/ctron`, `--status` live. Interactive TUI
  field-test on a real terminal pending at handoff.

## Session 2026-09-24 — POWER critique fixes (Kerempkl + GLM, FA608PP)

- Follow-ups to the 09-23 staged-apply, from a self-critique pass:
  view keys yield to editor keys (fan 'p'/'l' and power/light 'l' now
  reach their handlers; uppercase F/P/L/E always switch), stage-time
  clamp+order through the shared `ctrl_ppt_order()` (unit-tested in
  test_core), PPT-off staging mirrors the `ctrl_ppt_off` maxima, a
  live refresh when entering POWER with nothing staged, `value (?)`
  for rows whose live read is stale, a one-line apply ok/FAILED log,
  and a cpu-clock no-op write guard.
- Note for the FA507NVR line: `src/ui/editor_daeboard.c` ships
  format-truncation warnings (came with b931cd0); left untouched here.
- Verified: `make` (ctron-side files clean), `make test` incl.
  daeboard, `--status` live. Interactive TUI field-test on a real
  terminal still pending.

## Signed 2026-09-23 — Grok, FA507NVR, NixOS

Status: **installed** at `~/.local/bin/ctron`. Not left running.

Last ship: LIGHT view, **b**, opens the daeboard editor. Down and Up are side by side. Presets append. Add key and Change key wait for a real keypress; Change key keeps the steps. Del key removes the row. Del on a step removes the step. Start, Stop, Install, Save. `--follow`, `--daeboard-start`, `--daeboard-stop`, `--daeboard-reload`. While the daemon answers ping, brightness and static color use the socket. Other aura effects are refused.

Known issues:

- Quit any ctron that was already open before using this binary. An old process will save a chopped binds line.
- `key6` in `~/.config/ctron/daeboard.binds` is not a real key. Del key it, or Change key it.
- A step cannot be moved from Down to Up.
- Install on this NixOS machine starts the daemon. It does not run `install.sh`.
- Battery watts (2026-09-22, previously uncommitted) are in this same tree: `--status`, `--watch`, and the LIVE strip.

Resume: open ctron, LIGHT, **b**, Save after edits. Plan text is `PLANS.md`.

Latest earlier session: **2026-09-22** (FA608PP / KDE / CachyOS, with the ZCode
"GLM" agent). Pulled to `68de7e6` on FA507NVR, then rebuilt and installed
to `~/.local/bin/ctron` (still reports `2.0.0-deno`). Previous sessions below.

## FA507NVR — 2026-09-22, installed build

- Battery power is in `--status`, `--watch`, and the LIVE strip.
  This pack has no `power_now`; watts are `current_now` × `voltage_now`.
  Sample while unplugged: `BAT 41% Discharging dis 33.6W`.
- Retest on that binary, then restored to the state found at the start
  of the run (not Quiet): profile **Performance**, EPP `performance` on
  all 16 CPUs, keyboard off. `--epp balance_performance` reached all 16
  CPUs and restored. `--kbd low` → brightness `1` → off.
  `--profile balanced` swapped in the Balanced fan curve and
  `balance_power` on all 16; `--profile performance` put both back.
- PPT was already `60/75/75` before this run and was not written.
  Readback works. Do not run `--ppt off` here: no `asus-armoury`, so the
  generic ceiling is 90/120/120 W.

## FA507NVR — 2026-09-22, old binary

Checked on the 2026-09-20 binary, then restored:

- `--kbd low` → brightness `1`, restored `off`.
- `--profile balanced` loads that profile's fan curve and sets EPP
  `balance_power` on all 16 CPUs. `--profile quiet` put both back.
  Quiet curve: CPU `42,44,49,63,65,67,80,86` / `0,31,63,127,159,191,223,255`,
  GPU `40,42,43,60,65,69,74,78` / `5,20,38,43,255,255,255,255`, both on.
- `--epp` on that binary wrote cpu0 only. `68de7e6` walks every CPU;
  not rebuilt here yet.
- Do not run `--ppt off` on this machine until the ceiling is checked.
  No `asus-armoury`, so the new toggle's maxima are the generic
  90/120/120 W, above the old FA507 80 W cap. Live PPT still reads `--`.

## Session 2026-09-23 — POWER view staged-apply (FA608PP, third session today)

- POWER view no longer writes on every keypress: edits stage in
  `g_ui.pwv_*` until `w` / ` Apply `; `r` / ` Revert ` reloads from hw.
  Dirty rows render `live → staged ●`, title shows `POWER ●`. Presets
  stage their triple (staged watts imply limits back on).
- `pw_sync_from_hw()` runs at TUI start, after mode apply (controls
  panel) and profile apply, and after power Apply (which ends with a
  `hw_refresh_live` read-back). Stale nb-wmi reads keep the written
  values, not defaults.
- 'l' stays the LIGHT view-switch (workspace level), so staging uses
  h/←/→/Enter/Space; ESC stays the settings-overlay key, revert is 'r'.
- Verified: warning-free `make`, `make test`, `--status` on FA608PP.
  Interactive TUI field-test on a real terminal pending at handoff
  (plain-pty smoke still blocked by the known harness issue, NEXT.md
  item 2). Committed and rebased over the daeboard line (b931cd0);
  doc conflicts with the Grok session resolved keeping both records.

## Session 2026-09-23 — fragile-idiom cleanup + NEXT.md

- No behaviour changes: `ut_path_join()` replaced the `strcat(strcpy())`
  chains in `hw.c`; `ui_btn_row()` replaced hand-counted button offsets;
  `dash_if()` no longer uses a static ring. All committed by the user as
  "Session 2026-09-22 4".
- Arcioth added `PLANS.md` (fullscreen dashboard telemetry, daeboard
  client/control) — later-stage add-ons, see NEXT.md ordering.
- Worktree clean at `4db3e96`; next work starts from NEXT.md items 1-2
  (hwmon path caching, repo pty harness).
- Same day, second ZCode session: POWER view gained a "CPU clock limit"
  row — NEXT.md item 3 — h/l/Enter steps ±100 MHz through the per-CPU
  `ctrl_set_cpu_max_mhz`; value is the live `scaling_max_freq` (`--`
  when unknown). `make` warning-free, `make test` ok, `--status` live
  on FA608PP (limit 2401 MHz). The "tctl-vari gösterge" half of item 3
  stays open. Uncommitted at handoff: this row + the NEXT/HANDOFF/
  CHANGELOG doc edits — commit as one unit.

## Session 2026-09-22 — settings overlay + live layout

- ESC (or 's') opens a centred floating **settings window** (double frame, drop shadow, main screen still visible behind it); ESC/s close, 'q' always quits. Click outside closes it too. Mode editor runs inside it. Lone-ESC swallowing filter
  removed (0x1b == NCKEY_ESC).
- New LAYOUT rows in the overlay, applied instantly and persisted:
  swap_left, telem_top, left_pct, split_pct, telem_h. Placement is fully
  data-driven from g_prefs in `ui_layout()`.
- Also: per-CPU cpufreq writes (`--freq`/`--epp` walk cpu0..cpuN — the
  cpu0-only write left 31 cores clamped at 2.4 GHz), 2.4 GHz clamp
  diagnosis (see CHANGELOG 09-21), Refresh row starts on the live rate.
- `~/.local/bin/ctron` is built from this tree (v1 backed up as
  `ctron.v1.bak`).
- Uncommitted in the worktree at handoff time: the 09-22 overlay/layout
  changes plus these doc updates — commit as one unit.

## Session 2026-09-21 — v2 bugfixes on FA608PP

**Repo state:** `main = 52ea4ce`, synced with GitHub. This is the single
source of truth — the scratch copies under `~/Projeler/glm deneme`
(`ctron deneme`, `ctron 2.1`) are now redundant snapshots; do all further
work here in the repo.

### What landed

- **PPT limits toggle** (POWER row + `--ppt off|on`): off remembers the
  current SPL/SPPT/FPPT and writes the platform maxima — the clean
  equivalent of the profile-flip trick that reset the EC limits. on
  restores the remembered values. Session-only (PPT is firmware-default
  after reboot). Makefile object files now depend on headers.
- **tr_TR locale fix** (`src/ui/ui.c`): the TUI calls `setlocale(LC_ALL, "")`
  and on the Turkish locale `strtod("59.87")` stopped at 59 (decimal comma),
  corrupting the refresh list to `[59,60,164,165]`. Applied modes then tried
  "59 Hz" and logged FAILED. Fix: `setlocale(LC_NUMERIC, "C")` right after.
  Symptom to remember: ghost 59/164 Hz values, only inside the TUI.
- **Refresh row init** (`src/ui/ui.c`): the CONTROLS row opened on the first
  mode (60) regardless of the live rate; it now starts on the mode closest
  to `hz_cur` (e.g. 165).
- **kscreen-doctor writer** (`src/display/display_kde.c`): float refresh
  (`@60.00`) is rejected as "Unable to parse arguments" while still exiting
  0. We now send integer refresh (`@60`) and verify by reading the mode
  back instead of trusting the exit code.
- `~/.local/bin/ctron` on FA608PP is built from this tree; the old v1
  binary is kept at `~/.local/bin/ctron.v1.bak`.

### Verified this session

`make` warning-free, `make test` ok, `--doctor`/`--status` live on
FA608PP (asusctl 6.5, asus-armoury, k10temp, nvidia-smi, fan rpm, kde
backend with 60/165 modes), `--hz 60` ↔ `--hz 165` end-to-end, TUI smoke
under a pty with a terminal-query responder (clean `q` exit 0). TUI
Refresh row verified under `LC_ALL=tr_TR.UTF-8`.

### Still open

1. ~~Version string~~ — done 2026-09-27: `VERSION` is `0.2.1`
   (README's scheme). Tag `v0.2.1` optional.
2. From the FA507NVR session: HDMI-A-1 Hz (multi-monitor `hl.monitor`),
   TUI mouse under Hyprland.
3. Parked: Waybar sync, MUX/dGPU, throttle_thermal_policy.

---

## Session 2026-09-20 — FA507NVR / Hyprland 0.55

v2 (`888dc12`) plus FA507NVR Hyprland fixes.

**FA507NVR / NixOS / Hyprland 0.55.4:** `make test` ok. `--doctor` sees ASUS TUF A15, k10temp, nvidia-smi, asusctl, fan hwmon, **display hyprland**. `--status` live. `--hz 60` / `--hz 144` applied and restored.

Kerempkl verified FA608PP / KDE. This box is the Hyprland sibling.

Paths: project `~/Documents/ctron`, GitHub `github.com/Kerempkl/ctron`,
binary `~/.local/bin/ctron`, config `~/.config/ctron/`, build via
`nix-shell -p gcc pkg-config notcurses gnumake --run make`.
No-args: fullscreen TUI (v2). Flags: headless.

Machine differences: A15 FA507NVR, 7435HS + RTX 4060, kernel 6.18,
Hyprland Lua config. No `asus-armoury`. PPT sysfs is `5` → `--`. GPU fan
RPM often 0 at idle → `--`. Hyprctl JSON is a raw array; `keyword
monitor` is a no-op (Lua parser). v2 uses `hl.monitor({...})` via
`hyprctl eval`.

Not field-tested there: TUI click-through, FAN Write, PPT presets,
`--mode`, Aura. `--hz` was tested (60 then 144).

Todo from that session: TUI on that terminal (mouse / FAN Write, restore
Quiet curve if EC is touched); HDMI-A-1 Hz; after both laptops are on v2:
daetron, Waybar sync. Parked: Metatron, Gentoo ESP, 140 W, kernel bump.

Signed:

- **Arcioth** — FA507NVR / Hyprland
- **Kerempkl** — v2 rewrite, FA608PP / KDE
- **Grok 4.6 (xAI)** — Hyprland 0.55 JSON + `hl.monitor` eval

*Date: 2026-09-20*

# Changelog

## 2026-10-02 — topbar focus chips: fit + legibility

- The `1:profiles … 4:telemetry` chips were placed at a fixed
  `dimx-44` with a 12-column stride, so the last chip ended past the
  final column and the terminal clipped it entirely (harness-proven:
  "4:telemetry" absent from the first frame). Chips are now
  right-aligned from their real widths; the model title truncates to
  the first chip's column.
- Unfocused chips rendered in the muted theme colour — too dim to
  read against the background on real screens (the "not visible until
  I press a key" report: only the inverted focused chip stood out).
  Unfocused chips now use the plain text colour; the focused one
  keeps the black-on-accent inversion.
- Repro: pty harness capture — first frame contains all four chips
  (before: three). `make`, `make test`, `make tuitest` green;
  installed.

## 2026-10-02 — asusd takeover conflict warning

- When asusd's power-source profile takeover is armed for the CURRENT
  source and would enforce a profile other than the live one, ctron
  now says so instead of letting the user wonder why a profile "does
  not stick" (the 10-01 saga). Surfaces: the POWER "Platform profile"
  row gets a `⚠asusd: <name>` marker, a one-time telemetry log line
  on the rising edge ("will return on the next power event — AC/
  Battery auto rows to manage"), the apply toast appends
  `· asusd will revert (auto on)` when a profile is applied against
  an armed takeover, and `--status` prints an `asusd note` line.
- New pure helper `hw_asusd_enforced()` (current-source takeover
  profile or -1), unit-tested: source selection, off/unknown, armed
  on the other source only, conflict rule. No new sysfs/D-Bus reads —
  the state was already refreshed per live pass.
- Dormant on FA608PP (both takeovers off since 10-01 by choice); live
  conflict could not be exercised without re-arming the takeover, so
  the path is covered by unit tests + review only.

## 2026-10-02 — all-cores freq write now verifies (amd-pstate clamp found)

- Symptom: the POWER "CPU clock limit" row stayed at 2401 no matter
  what was applied, and `--freq 3000` exited 0 while nothing landed.
- Root cause chain: the write itself succeeds (sudo path works), but
  the kernel clamps `scaling_max_freq` back to the nominal 2401 MHz
  synchronously on this machine right now — verified with raw
  `sudo tee`: 2402/2500/3000/5386 all read back 2401000 instantly,
  while 2000000 sticks. cpuinfo_max_freq still reports 5386 and boost
  is on, so this is an amd-pstate policy re-baseline (scaling_min also
  moved, 421→1492) after the day's OCCT load + profile churn — no
  userspace tool can lift it; reboot/module reload resets it.
- ctron bug this exposed (rule 4): `ctrl_set_cpu_max_mhz` reported
  success without reading back. It now verifies every present cpu
  (±2 MHz rounding tolerance) and on a clamp reports
  `kernel kept N MHz on x/y cpus` + returns failure, so the CLI exits
  non-zero and the TUI toast shows the real outcome.
- Verified: warning-free `make`, `make test`, `make tuitest`; the
  running TUI must be restarted to pick this up.

## 2026-10-02 — CORE LIMITS: physical-core + CCD view (topology-adaptive)

- The CORE LIMITS editor ('c' in POWER) now shows physical cores —
  SMT pairs side by side as `c0·16` cells — grouped under CCD headers
  on multi-CCD CPUs (L3 domains; `CCD1`/`CCD2` on AMD, `L3-n`
  otherwise). Editing a cell stages BOTH threads of the core; with a
  header selected, h/l/t/o/a operate on the whole CCD. `a` = the
  selected value across its CCD (single-CCD machines keep the old
  "all" meaning), `A` = everywhere. Writes go per thread id (policies
  are per thread; a kernel that propagates makes the sibling write an
  idempotent same-value write).
- Topology is derived from sysfs, nothing hardcoded:
  `thread_siblings_list` per present cpu for cores, the level-3
  `shared_cpu_list` for CCDs. SMT-off machines get single-thread
  cells (`c8`), missing L3 falls to one group, and a core with more
  than two present threads or unreadable topology falls back to the
  old per-thread grid (`topo_odd`). Built once at init
  (`hw_topology_build`); the pure grouping (`hw_topology_group`) is
  unit-tested: SMT pairs/two CCDs, SMT-off, no-L3, offline sibling,
  >2 threads.
- Harness: the corefreq flow now also asserts the CCD headers + core
  cells on the FA608PP (passes; the overlay opened in core mode).
- Follow-up round (user feedback): CCDs render as side-by-side
  COLUMNS (cores stacked under their header, like two chips), and a
  real truncation bug got its root fix — `ui_trunc` counted BYTES
  while callers pass display cells, so the multibyte `▸`/`●` glyphs
  ate the budget and a 5386 MHz cell showed "538". It now counts
  UTF-8 lead bytes (cells) and never splits a glyph; the core cells
  are sized for the worst `▸c255·255 5386●`.
- Verified: warning-free `make`, `make test`, `make tuitest`. Real
  writes still blocked by the missing passwordless sudo on this
  machine (pre-existing, see the cpu-id entry below).

## 2026-10-02 — cpufreq writes + per-core indexing use real cpu ids

- The kernel's present list is not contiguous on every machine (SMT
  off / offlined cores → "0-15,32-47"). The batch cpufreq writer
  (`cpufreq_write_all`: EPP + scaling_max to every policy) used to
  scan cpu0..N and stop at the first missing path — everything after
  the hole was never written. It now iterates `hw->cpu_ids`, the
  parsed present list, with the promised topology note in place.
- The per-core frequency array was indexed inconsistently: the sweep
  filled `cpu_mhz_core` by real cpu id while the CORE LIMITS editor,
  `freq core N M`, per-core writes and profile export indexed it
  densely (0..cpu_n). Unified on real kernel ids everywhere:
  `hw_state_t.cpu_ids[]` is published by the sweep (ids beyond
  HW_CPU_MAX dropped), `hw_cpu_present()` is the membership check,
  the editor's cf_sel/mouse targets stay grid positions while cells
  and staging use real ids (labels now show the kernel number).
- CLI `freq core N M` takes a kernel cpu number and validates against
  the present list; profile export writes `freq core <id> <MHz>`.
  Identical behaviour on contiguous machines (ids == 0..N-1).
- Verified: warning-free `make`, `make test`, `make tuitest` (incl.
  the corefreq overlay flows). Real privileged writes could not be
  exercised: passwordless `sudo -n tee` is currently unavailable on
  FA608PP (pre-existing — the old installed binary fails identically
  with a clean "command failed"); the TUI toast/log will show FAILED
  writes until sudoers allows it again.

## 2026-10-01 — Makefile hardened for external CFLAGS/LDLIBS

- The flags ctron cannot build without (`-std=c11 -Isrc -D_GNU_SOURCE`
  + pkg-config cflags; notcurses libs) are now appended AFTER any
  external CFLAGS/LDLIBS — a distro packager, Nix stdenv or a plain
  `-O0 -g` debugging env no longer silently drops the include path
  and the feature macros. Verified live: `CFLAGS="-O0 -g"
  LDLIBS="-lm" make` builds and links.
- pkg-config-less environments fall back to
  `-lnotcurses -lnotcurses-core` (tested with pkg-config stripped
  from PATH); `CC ?= gcc` removed — it never took effect over make's
  builtin `cc` and only misled. GNU-make requirement now documented
  at the top.

## 2026-10-01 — sysfs device paths cached (NEXT.md item 1)

- The 250 ms poll no longer runs four discovery globs per tick
  (k10temp, battery, Mains, asus RPM hwmon). Paths live in
  `hw_state_t.paths`, probed on first use; a failed read on a cached
  path drops the entry so the next poll re-probes — suspend/resume
  renumbering recovers within one tick.
- The fan-curve write path now resolves its hwmon through the same
  cache (`hw_path_fan_curve`), so reads and writes can never disagree
  after a renumber.
- Honest measurement: CPU time over 5 s of `--watch` is unchanged
  (sys 0.16→0.17 s, user 0.12→0.11 s) — the globs were cheap on this
  machine. The real gains are ~18 fewer file opens per poll and the
  read/write path consistency. Live stream, unit and TUI tests green.

## 2026-10-01 — asusd rows: setting a rule no longer switches the mode

- User-hit: changing an AC/battery auto-profile applied the rule AND
  flipped the active mode at the same moment. Isolated live:
  `asusctl profile set -a <X>` applies X immediately when running on
  that power source — even with the takeover flag off (quiet jumped
  to performance at the profile write, not at flag enable).
- `ctrl_set_asusd_auto` now snapshots the active profile before the
  writes and restores it afterwards if the side-effect moved it
  (`asusctl profile set <prev>`, logged). When pw_apply also staged
  the Platform-profile row, that row is written first, so the restore
  target is the user's explicit choice — explicit mode changes still
  win.
- Verified live: active stays Quiet while AC-auto Performance is
  applied (and battery side clean); TUI harness green. Machine left
  with both takeovers off, active Quiet.

## 2026-10-01 — asusd rows: stale-display fix after apply

- User-hit TUI bug: changing an auto-profile row from "off" and
  applying worked (hardware correct) but the row kept showing "off".
  Cause: pw_apply's read-back refresh read `/etc/asusd/asusd.ron`,
  which asusd flushes asynchronously — the stale file value clobbered
  the verified live one. The read layer now asks the daemon itself
  (`hw_asusd_auto_flag` / `hw_asusd_auto_profile`, shared with the
  control layer's verification) and only falls back to the ron file
  when no daemon answers.
- `--status` gained an `asusd auto: AC … · battery …` line, which
  doubles as the race regression check: value correct immediately
  after every apply (verified live across balanced/off transitions).
- TUI harness green; takeover left OFF on both sides per the user's
  preference.

## 2026-10-01 — asusd power-source profile takeover, managed from ctron

- Diagnosis behind the feature: asusd re-applies its per-power-source
  platform profile on every AC/battery event (`ChangePlatformProfileOnAc:
  true` + `PlatformProfileOnAc: Performance`), and this machine's USB-C
  PD fires power events ~every 1.5 s — so a Balanced picked from KDE
  flipped back to Performance within seconds, with asusd also swapping
  the fan curve (the audible "turbo"). Observed live twice.
- POWER gained two staged rows under Platform profile: **AC
  auto-profile** and **Battery auto-profile** (off / Quiet / Balanced /
  Performance; `--` when asusd is absent). Applied LAST in pw_apply so
  the daemon's re-assertions cannot race the other writes. Reads come
  from `/etc/asusd/asusd.ron` (hw layer, name-based — asusd's numeric
  enum deliberately never mapped).
- Writes use the daemon's native interfaces: `asusctl profile set
  -a/-b <name>` for the profile and `busctl set-property` on
  `xyz.ljones.Asusd` for the `ChangePlatformProfileOn{Ac,Battery}`
  flags (asusctl has no CLI for them) — no file editing, no daemon
  restart. Verification reads the daemon's live state: the ron file
  flushes asynchronously, and ut_exec's default capture truncates at
  the first newline — both found live and worked around
  (`ut_exec_raw`).
- CLI: `ctron --ac-profile off|quiet|balanced|performance` and
  `--battery-profile ...`; the shared command table means modes.ini
  steps and .ctr profiles accept `ac-profile off` too.
- Verified live on FA608PP: off (flag false, platform profile
  untouched), balanced on both sides, invalid value rejected, original
  config restored byte-identically. `make` warning-free, unit + TUI
  tests green.

## 2026-09-29 — per-core CPU frequency limits (grid editor + `freq core N M`)

- New `src/ui/editor_corefreq.c`: a fullscreen CORE LIMITS overlay
  opened from POWER with **c** or the ` Cores ` button (daeboard
  overlay pattern: swallows keys, click-outside closes). Grid of
  `cNN <MHz>` cells; staged in memory. Keys: j/k select, h/l ±100,
  t exact value, a value-to-all, o back-to-max, r revert, w/Enter
  writes only changed cores through the new
  `ctrl_set_cpu_max_mhz_core` (read-back verified per core, summary
  log). Status line shows `n/N capped · m staged` and warns that the
  POWER all-cores apply overwrites per-core limits.
- hw: `HW_CPU_MAX`, `cpu_n`, per-core `cpu_mhz_core[]` filled by the
  existing sweep (zero extra reads); `hw_cpu_list_parse()` handles
  "0-31" and "0-15,32-47" lists (unit-tested). `hw_init` now runs the
  sweep once so the CLI flag path knows `cpu_n` (found live:
  `--freq core` saw 0 cpus before).
- Shared command table: `freq <mhz>` (all) and `freq core <N> <mhz>`
  (single) — modes and .ctr profiles inherit the form; profile export
  appends `freq core N M` lines for every core capped below the
  aggregate, so applying a profile restores per-core setups. CLI
  accepts `ctron --freq core 4 3000` (multi-token join).
- Verified on FA608PP (kernel 6.18-lts): warning-free build, unit +
  TUI tests green (new corefreq flow: open/stage/revert/close without
  writing), live `--freq core 4 3000` → sysfs 3000000 → restored to
  5386, invalid core id rejected cleanly.

## 2026-09-28 — list scrolling (PROFILES + CLI SHORTCUTS)

- Both growing lists now scroll to keep the selection visible:
  `prof_top` / `set_mode_top` offsets are clamped in the draw pass
  (same pattern as the fan editor's fe_sel clamping). The hint line
  shows ▲/▼ when entries exist above/below the window.
- Closes NEXT.md item 4. Verified: warning-free build, `make test`,
  `make tuitest` green, installed.

## 2026-09-28 — POWER presets: one button row instead of three rows

- The three preset rows (Q45/B60/P80) collapsed into a single
  "Presets" row under EPP preference / above the watt limits, drawn
  as side-by-side buttons. The button whose watt triple equals the
  staged values is lit; a staged-but-custom triple shows "custom".
- Keyboard path follows the row grammar: j/k selects the row, h/l
  walks Q45 → B60 → P80 and stages the one it lands on (nothing
  writes); Enter still applies everything. Mouse: each button stages
  its preset directly; clicking the label area selects the row
  (second click stages the next preset). `t` is a no-op on this row
  (not numeric). Rows went 14 → 12.
- Verified: warning-free build, `make test`, `make tuitest` all
  green; installed.

## 2026-09-28 — FA608PP re-verified on 6.18-lts

- User switched kernels (7.2 → 6.18.52-1-cachyos-lts). Re-ran the
  full chain: clean warning-free build, `make test`, `make tuitest`
  (all flows incl. the fan write+verify click), `--doctor`, live
  `--status`. Every ctron interface is present on the LTS kernel:
  asus_custom_fan_curve + asus hwmons, armoury attrs, asusctl,
  platform_profile, amd-pstate active with per-CPU EPP (ceiling
  negotiates 5353/5386 MHz), k10temp, nvidia-smi, BAT1 threshold,
  KDE Hz backend. HARDWARE.md FA608PP row now lists both kernels.
  Environmental note (not kernel): `sudo -n` reported unavailable in
  this boot — writes still succeed via the asusctl/sysfs tiers.

## 2026-09-27 — pty harness in-tree: `make tuitest`

- `scripts/tui_smoke.py` drives the real TUI under a pty: a responder
  answers notcurses's init queries (CPR, DA1, secondary DA, XTVERSION,
  kitty `?u`, sync 2026, OSC 4/10/11) during the first second only,
  then stays silent while draining output — the documented workaround
  for `notcurses_init` hanging on a dumb pty.
- Flows: open/render/`q` exit 0 · ESC settings overlay (LAYOUT
  visible) · POWER net-zero stage+apply (`l`+`h` then `w` — asserts
  the "power apply" log line while writing nothing to hardware;
  lowercase `p` deliberately belongs to the fan editor, so the flow
  uses `P`) · fan control-row clicks must not switch views (the
  ACT-id collision regression) and the Write click must apply the
  curve (real no-op re-write; needs the TUI's usual privileges).
- `make tuitest` target; `make test` stays fast. Three consecutive
  runs green on FA608PP.

## 2026-09-27 — fan-editor buttons fixed (ACT id collision)

- User-hit bug from the morning entry below: clicking the fan
  editor's top row (CPU/GPU/+/-/Write/ON) opened workspace tabs
  instead — `ACT_FE_*` ids (1..30) were registered under
  TGT_PANEL_WORKSPACE and collided with `ACT_WS_*` (Write=5 was the
  Help tab). Ids rebased to 100..159 (`ACT_FE_BASE/END` in
  ui_internal.h) and `panel_workspace_act` now forwards them to
  `editor_fan_act`, which gained a button dispatch: CPU/GPU switch,
  add/del point, Write (with the "applying..." flash), ON/OFF toggle,
  T/P field entry, Set, Stk/Sil/Col/Ful presets, point chips select.
  Graph clicks (id 0) unchanged. daeboard editor already had its own
  TGT panel — unaffected.

## 2026-09-27 — fan write verification + "applying..." flash

- `ctrl_fan_write` now verifies itself (rule 4): all 8 temp/pwm
  points per fan are read back from the custom-curve hwmon and the
  log states `· verified 8/8 + 8/8 pts`, or `VERIFY FAILED: cpu 6/8,
  gpu 8/8` on a real mismatch (this hwmon is not stale-cache-prone,
  unlike nb-wmi PPT). The write loop's last-point padding moved into
  a shared `fan_point()`, so write and verify compare the same
  values. CLI path verified live with a no-op re-apply on FA608PP.
- New `ui_flash()`: before long operations (fan write/preset, profile
  apply, mode bundle, POWER apply) the telemetry log row immediately
  shows "applying..." with a synchronous render — no more frozen-UI
  feel during the blocking write. Naturally replaced by the next
  frame; no-op outside the TUI.
- Found on the way (NOT fixed, out of scope): fan-editor buttons
  register `ACT_FE_*` ids under TGT_PANEL_WORKSPACE, but
  `editor_fan_act` ignores ids and the values collide with
  workspace/power ids (`ACT_FE_WRITE=5` == `ACT_WS_TAB_HELP`) —
  clicking " Write " opens HELP. Keyboard `w` is fine. Needs an id
  rebase + a button dispatch branch.

## 2026-09-27 — version string: 0.2.1

- `VERSION` in `main.c` was still the v1-era `2.0.0-deno`; now `0.2.1`,
  matching the README title. `ctron --version` / `-V` report it; the
  installed binary rebuilt. HANDOFF "still open" item closed.

## 2026-09-27 — HARDWARE.md support matrix (EN + TR)

- New `HARDWARE.md`: per-device-class support table in English and
  Turkish, with the reasons behind each row — the four dependency
  layers (ASUS WMI stack, AMD CPU side, NVIDIA driver, desktop stack),
  the two verified machines and their quirks, plus the behavioural
  notes (honest `--`/FAILED degradation, sudo as the top breaker).
- README intro gains a two-machine verification summary and a link to
  the matrix instead of carrying the full bilingual table.

## 2026-09-26 — CPU clock window follows amd-pstate (battery bug)

- Bug: after changing EPP (or the platform profile) the POWER view's
  clock-limit window and the staging clamps kept the cpuinfo values
  captured at startup — and read cpu0 only. amd-pstate re-negotiates
  per-core ceilings with the firmware (observed 2401↔5386 MHz within
  seconds, cores diverging), so ctron showed a stale "300–2400" window
  and clamped staging to it long after the kernel moved on.
- `hw_refresh_fast` now sweeps `cpuinfo_min/max_freq` and
  `scaling_max_freq` across all present CPUs and keeps the widest
  window, so one clamped core (often cpu0) cannot cap the display or
  the clamps. Every poll follows the kernel; Apply / profile / mode
  refreshes inherit it.
- Findings on FA608PP: the **quiet platform profile** holds the
  ceiling at the base clock (2401 MHz) regardless of EPP — on battery
  or not. Balanced/Performance widen it to 5386 MHz within ~3 s.
  Nothing userspace can do raises scaling above cpuinfo; to exceed
  2.4 GHz in ctron, switch the Platform profile row, then the clock
  row accepts up to the live window. KDE's power-profiles-daemon also
  re-asserts its own EPP on profile changes — ctron's EPP row
  overrides it again on Apply.

## 2026-09-26 — POWER: exact-value typing + range hints

- `t` on a numeric row (SPL/SPPT/FPPT, NV boost/temp, CPU clock)
  starts exact-value typing, seeded with the staged value. Enter
  stages it — clamped exactly like a nudge, and digits only: anything
  else is dropped with a log line instead of staging the minimum.
  Esc cancels; switching views or any click ends typing; starting to
  type dismisses the apply toast.
- Plain value rows now show their allowed window, e.g. `65 W (15–90)`.
  The firmware windows come from `ctrl_ppt_limits` cached for one
  second in the draw pass, so frames do not re-read the armoury
  attrs six times each. Dirty rows keep the `live → staged ●` form
  (the arrow wins over the range); the typing line shows the window
  too: `set SPL (sustained) (15–90): 65_`.
- Help view (POWER section) documents `t`.
- `make` warning-free on the ctron side, `make test` ok, installed to
  `~/.local/bin/ctron`, `--status` live on FA608PP.

## 2026-09-24 — POWER apply toast (what changed, in colour)

- After Apply, the POWER panel shows a toast above the buttons for
  5 s: green `✓ SPL 65→80 W · EPP performance` listing every field
  that changed (old→new; `→80 W` when the live read was stale), red
  `⚠ … · N failed` when writes failed. New staging or Revert dismisses
  it; the plain `ut_log` line stays as history in the telemetry panel.
- Semantic, theme-independent colors: ok `0x33FF66`, fail `0xFF4D5E`.
  While the toast is up, one list row yields its place so small
  terminals do not overlap.
- **Stale-read fix found on the way:** Apply no longer writes watt/NV
  defaults just because the live nb-wmi read is 0/stale. A new
  `pw_touched` bitmask tracks the fields the user actually edited;
  writes happen only for touched fields or known-different values —
  staging only an EPP no longer rewrites PPT.
- `make` warning-free (ctron side), `make test` ok, installed to
  `~/.local/bin/ctron`.

## 2026-09-24 — POWER: profile/EPP rows, Enter applies, q guard

- Two new staged rows at the top: **Platform profile**
  (Quiet/Balanced/Performance) and **EPP preference** — EPP finally has
  a TUI editor (was CLI/mode-only). Both ride the same staging: h/l
  stage, one Apply writes everything; the profile write goes first so
  an explicitly staged EPP wins over the profile-implied one.
- **Enter now applies** the staged bundle (like every other panel);
  the old "Enter steps the value up like right-arrow" behaviour is
  gone. h/l and ←/→ remain the only staging keys, `w` stays as an
  apply alias. That also answers "the Apply button has no keyboard
  path": Enter is it.
- **q guard**: quitting with staged edits pending warns once
  ("staged edits pending — q again to quit") and quits on the second
  press; the warning state resets on apply/revert. Works from any
  panel, not just the POWER view.
- `make` warning-free (ctron side), `make test` ok, installed to
  `~/.local/bin/ctron`, `--status` live on FA608PP.

## 2026-09-24 — POWER view follow-ups (critique fixes)

- View-switch keys no longer shadow editor keys: lowercase f/p/l/e
  yield to the active view's bindings (fan editor 'p'/'l', POWER and
  LIGHT 'l'); uppercase always switches. POWER staging answers 'l'
  like 'h', and the fan editor's 'p' (type pwm) and 'l' (nudge) work
  again after being shadowed by the workspace shortcuts.
- Staged watt triples are clamped and ordered at stage time via the
  new shared `ctrl_ppt_order()` (extracted from `ctrl_set_ppt`), so
  the staged numbers are exactly what Apply writes — unit-tested in
  `test_core` (clamp, order, clamp-then-order, passthrough).
- Staging "PPT limits → removed" now stages the platform maxima,
  mirroring `ctrl_ppt_off`, so pending watts never show values that
  cannot apply while the limits are off.
- Entering the POWER view (with nothing staged) runs `hw_refresh_live`
  + restage, picking up external changes (CLI, asusctl) instead of a
  stale "live" column.
- Rows show `value (?)` when the live read is unknown/stale but a
  staged/written value exists (was a bare `--`). Apply logs a one-line
  ok/FAILED summary; the cpu-clock row skips the no-op privileged
  write when no limit is set and staging equals the cpuinfo max.
- `make` warning-free outside `editor_daeboard.c` (its warnings came
  with the daeboard ship, left for the FA507NVR line); `make test` ok;
  `--status` live on FA608PP.

## 2026-09-23 — daeboard editor

- LIGHT view, **b**, opens the macro editor. Down and Up are side by side.
  Presets append. Add key and Change key take the next keypress.
  Del key removes the row. Save writes `daeboard.binds` and reloads.
- `--follow`, `--daeboard-start`, `--daeboard-stop`, `--daeboard-reload`.
- Brightness and static color go through the daemon socket when it is up.

## 2026-09-23 — POWER view: staged edits + Apply/Revert

- h/arrows no longer write per keypress: edits land in `g_ui.pwv_*`
  staging fields and nothing reaches the hardware until `w` / the
  ` Apply ` button. One `ctrl_set_ppt` covers the SPL/SPPT/FPPT triple;
  NV boost/temp, panel OD, CPU boost and clock limit write only when
  changed. PPT-limit toggle rides along (off writes maxima, restore
  then values).
- `r` / ` Revert ` drops staged values back to live. Dirty rows render
  `live → staged ●`; the panel title shows `POWER ●` while pending.
- `pw_sync_from_hw()` re-stages from hw at TUI start, after mode or
  profile apply, and after Apply (read-back verify). Stale nb-wmi reads
  (≤5 W) keep the written values instead of collapsing to defaults.
- Mouse: second click on a row stages/toggles it; Apply/Revert are
  registered click targets. Help view gained a POWER section.
- Key landscape: 'l' remains the workspace LIGHT switch, so staging is
  h / ← → / Enter / Space; ESC remains the settings-overlay key, so
  revert is 'r'. CLI and the CONTROLS panel are unchanged.
- `make` warning-free, `make test` ok, `--status` live on FA608PP.
  Interactive TUI field-test on a real terminal still pending.

## 2026-09-23 — CPU clock limit row in the POWER view

- The TUI POWER view gains a "CPU clock limit" row: h/l (or Enter)
  steps the cpufreq ceiling ±100 MHz through `ctrl_set_cpu_max_mhz`,
  which walks cpu0..cpuN (the per-CPU path from 09-21). Display reads
  the live `scaling_max_freq`; `--` when unknown. CLI `--freq` already
  existed; this is the missing TUI counterpart.

## 2026-09-22 — battery watts on FA507NVR

- `--status`, `--watch`, and the LIVE strip show battery power.
  `power_now` when present, otherwise `current_now` × `voltage_now`.
  Discharging is `dis 31.6W`, charging is `chg 12.0W`.
- Rebuilt and installed to `~/.local/bin/ctron`. On this machine,
  discharging at about 33 W. `--epp` now lands on all 16 CPUs.

## 2026-09-22 — fragile-idiom cleanup (no behaviour change)

- `ut_path_join()` (bounds-checked) replaces the five
  `strcat(strcpy(...))` chains in `hw.c` — sysfs path building can no
  longer overflow.
- New `ui_btn_row()` flow helper replaces hand-counted button offsets
  (`x+9`, `x+17`, ...) in the profiles panel, the fan editor's two rows
  and the mode editor's save row; labels can now change freely and
  buttons that do not fit are skipped instead of overlapping.
- `dash_if()` in `--status`/`--watch`/`--doctor` no longer returns slots
  from a static 4-entry ring; callers pass their own buffers.

## 2026-09-22 — PPT limits toggle (POWER row & --ppt off|on)

- POWER view gains a "PPT limits" row and the CLI gains `--ppt off|on`:
  off remembers the current SPL/SPPT/FPPT and writes the platform maxima
  (the clean equivalent of the profile-flip trick that reset the limits);
  on writes the remembered values back. Session-only state — PPT returns
  to firmware defaults on reboot.
- Makefile: object files now depend on the headers (a stale settings.o
  with the old struct layout corrupted the persist tests).

## 2026-09-22 — settings window (btop-style) + live layout settings

- The settings overlay is now a centred floating window (double frame,
  drop shadow) over the still-rendered main screen — ghost text fixed by
  construction: panels and window each fill their area every frame.
  Click outside the window closes it; mouse hits search newest-first so
  background buttons cannot be clicked through the window.

- Settings opens as a floating window with ESC or 's' (and the
  SET tab / controls row). ESC/s close it; 'q' always quits. The mode
  editor runs inside the window. Input-field ESC cancels keep priority.
- New LAYOUT section in the overlay, applied instantly and persisted:
  swap left panels, telemetry at top, left-column %, left split %,
  telemetry height (`settings.ini`: swap_left/telem_top/left_pct/
  split_pct/telem_h).
- Fixed a v1-era input filter that swallowed lone ESC (`0x1b` == NCKEY_ESC)
  as "stray CSI" — ESC could never reach the UI.
- Mouse: settings rows now dispatch through TGT_PANEL_SETTINGS (previously
  registered as workspace targets that the workspace ignored).

## 2026-09-21 — per-CPU cpufreq writes

- `--freq` / `--epp` wrote only cpu0's cpufreq node ("governor mirrors
  cpu0"). On amd-pstate every CPU has its own policy (`related_cpus` is
  single-member), so `sudo ctron --freq 5386` unlocked one core and left
  the other 31 clamped at base (2.4 GHz). The helper now walks cpu0..cpuN
  and writes each node.

## 2026-09-21 — Refresh row starts on the live rate

- CONTROLS → Refresh row opened on the first mode (60) regardless of the
  live rate. It now starts on the mode closest to `hz_cur` (e.g. 165).

## 2026-09-21 — tr_TR locale fix (Kerempkl, FA608PP)

- TUI pinned `LC_NUMERIC` to "C" after `setlocale(LC_ALL, "")`. Under
  tr_TR (decimal comma) `strtod("59.87")` returned 59, so the refresh list
  became [59,60,164,165] and `--hz`/TUI refresh applied "59 Hz" → FAILED.
  Verified in a tr_TR pty: TUI Enter switches 165→60 and back.

## 2026-09-20 — FA507NVR / Hyprland 0.55

- Hyprland backend parses `hyprctl monitors -j` **array** JSON (`name`, `refreshRate`, `availableModes`; no `currentMode` wrapper).
- `set_hz` uses `hyprctl eval 'hl.monitor({...})'` (0.55 Lua). `keyword monitor` is a no-op and was reported as success. Position/scale copied from JSON. Legacy keyword kept as fallback.
- `--hz 60` / `--hz 144` verified on eDP-1, restored 144.
- PPT stale `5` prints `--` (was `0`).
- `make test` / `--doctor` / `--status` on NixOS 26.05 FA507NVR.

## 2026-09-19 — ctron v2 (Kerempkl)

Modular TUI + CLI. See `README.md` / `PLAN.md`. Target FA608PP / KDE.

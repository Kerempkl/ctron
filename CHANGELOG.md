# Changelog

## 2026-10-28 — security pass 3: mouse busy-gate, uninit err reads, mode-steps guard, parse clamps

- Independent full re-scan of current HEAD (three parallel review
  blocks + display/scripts verified by hand) after the v0.8.1 audit.
- **P1: `handle_mouse` skipped the apply-busy lock** that
  `dispatch_key` enforces — a mouse click during a background apply
  drove writes/refreshes on the UI thread concurrently with the apply
  worker (worst case a torn `fan_curve_t` written to the EC). Mouse
  events now return early while an apply runs.
- `apply_worker` printed **uninitialized `err` buffers** (`%s`) on
  success paths — `profile_import`/`mode_apply` only write them on
  failure; both are empty-initialized now.
- **`ctron mode add` steps validated** by the new `mode_steps_ok()`:
  a newline in steps injected bogus `[sections]` / a rewritten
  `steps` key into modes.ini on the next save (the steps twin of
  `mode_name_ok`; TUI typing cannot produce control characters).
  Unit-tested (4 cases) + live CLI check.
- The last two fragile `off += snprintf` sites hardened the
  `fan_curve_data_str` way (break on truncation):
  `gpu_clock_verify`'s sample join and `profile_meta_diff`'s
  `ADD_LABEL` macro — both proven safe today, now structurally unable
  to wrap `size_t`.
- `editor_daeboard`'s `last_int()` saturates at `INT_MAX` instead of
  signed overflow on hand-typed ms values; `ut_read_int` uses
  strtol+clamp and `ut_parse_ints` clamps long→int (atoi on hostile
  sysfs text is UB; atoi parity kept — no digits still means 0).
  New `check_int_parse_clamps` unit test.
- Re-verified clean: display backends (bounded parsers, argv exec),
  ctron-toggle hardened state, Python harnesses, SECURITY.md claims
  vs code; fanalyzer's 4 warnings are the known FP families.
- Verified: -O2 zero-warning build, `make test`, ASan+UBSan build +
  test, `HARDEN=1` build + test, full `make tuitest`.

## 2026-10-28 — review pass 2: modes.ini name guard + analyzer-clean shadows

- Second full look after the security audit, this time with
  `-Wshadow -Wstrict-prototypes -Wwrite-strings -Wformat=2` on top of
  the stock flags, plus the first read of the two Python harnesses
  (tui_smoke / tui_screenshot — both clean: isolated CTRON_CONFIG,
  bounded emulators, careful env restore).
- **Mode names now validated at both creation sites** (`mode add` CLI
  and the TUI MODE EDITOR) via `mode_name_ok()`: empty names, `[`/`]`
  (the ini section parser cuts at the first `]`) and newlines (would
  inject a bogus line) are rejected with a clear error — such names
  previously round-tripped broken and silently disappeared from the
  list. Unit-tested (6 cases).
- `editor_daeboard`: the four step-string helpers' `step` parameter
  shadowed the editor's global cursor `step` (-Wshadow) — renamed to
  `slot`; the 'a' key computed the destination pointer BEFORE the
  `*pn < DBE_STEPS` guard (benign — the address lands inside the
  struct and was never written out of range, but the guard now owns
  the pointer). `detail_ram`'s alarm-counter `n` shadowed the
  ncplane parameter — renamed to `cnt`. `hw.h` dropped a duplicate
  include pair.
- The extra-warnings pass is down to exactly one deliberate warning:
  `val_or_dash`'s format parameter is non-literal by design (every
  call site passes a literal).

## 2026-10-28 — security: full-tree audit, profile_set_note stack overflow fixed (v0.8.1)

- **Full read-only audit of every source file** (~19 k lines: src/,
  scripts/, Makefile) + `gcc -fanalyzer` pass + targeted greps. The
  SECURITY.md claims held against the code: privileged writes are pure
  argv execs everywhere, every remaining `popen`/`ut_exec` gets a
  code-constant command, every `sscanf %s` is width-bounded, every
  `ut_log` format is a literal, `system()` is gone, and
  `profile_sanitize_name` blocks path traversal. The analyzer's five
  warnings are all false positives or benign (child fds die at exec;
  `ctrl_ppt_limits` writes all six outputs unconditionally).
- **The one real vulnerability, found and fixed:** `profile_set_note()`
  accumulated `off += snprintf(...)` — snprintf returns the WANTED
  length, so a `>4 KB .ctr` file pushed `off` past `out[4096]`, the
  next `sizeof(out) - off` wrapped size_t, and the rewrite wrote past
  the stack buffer with file content. Proven live with an ASan PoC
  against the real function (WRITE of size 513, `out` overflow), then
  fixed with `note_append()` (offset hard-clamped on both sides;
  oversized snapshots are rewritten truncated, never out of bounds).
  A >12 KB regression test in `tests/test_core.c` covers the path and
  the whole suite passes under an ASan build.
- **ctron-toggle.sh:** the KWin helper `.js` is now created with
  `set -C` noclobber (the stamp's rule) — in the shared `/tmp`
  fallback a planted symlink at the pid-based name is refused
  (re-verified with a symlink→/etc/passwd probe), one retry on
  collision, KWin branch skipped if both refuse.
- **daeboard warning made explicit** (SECURITY.md §4.1): `db_install`
  execs `~/Documents/daeboard/install.sh` as root — never add a
  sudoers rule for it; the default ruleset deliberately excludes it.
- Small hardening: `make HARDEN=1` opts into
  `-fstack-protector-strong -D_FORTIFY_SOURCE=2` (distro CFLAGS add
  these anyway; plain make/Nix do not); `cut_step`'s shift is now
  `memmove` (regions were adjacent, not overlapping — cleanliness).
- v0.8.1 (PATCH): VERSION + README badge + flake version in one
  commit per policy.

## 2026-10-28 — perf: FAZ 4/16 closed with evidence (no background thread)

- The conditional "nvidia-smi background sampler" item is decided by
  field measurement (FA608PP, driver 615.71, the exact query ctron
  runs): baseline p50 20.6 / p95 30.3 ms; under REAL NVENC 4K60
  encoder load (75 % util · 77 W) p50 28.9 / p95 32.3 ms; under GL
  graphics stress (86 % util · 85 W) p50 21.2 / p95 24.4 ms —
  genuine GPU load does not lengthen the query. The only growth was
  artificial 8× parallel self-queries (p95 79 ms), which no real
  scenario produces and which still sits 37× under the 3 s timeout.
  The synchronous 25 ms / 2 s query on the UI thread is confirmed
  harmless; the background thread stays unbuilt — now as an
  evidence-backed decision instead of a "don't touch for now".

## 2026-10-28 — telem: professional monitoring pass (research-driven)

- User asked "telemetry kısmı profesyonelce nasıl yapabiliriz,
  muadillerini araştır" — researched btop/bottom/glances/htop/
  zenith-ttyplot conventions, user picked ALL four adoptable items.
- **Threshold zone colouring (glances OK→CAREFUL→WARNING→CRITICAL)**:
  temps 70/85/95 °C, util 70/90/98 %, charge inverted (low=alarm);
  the value text AND the curve+wash+dot take the zone colour, so a
  hot chart visually shouts. Fixed palette-independent warn colours
  (amber/orange/red) on top of the theme accent.
- **Value column alignment (btop per-core rows)**: grid rows are now
  label-left / sparkline-middle / value-RIGHT-ALIGNED, value in its
  zone colour; secondary pair series rows in accent2.
- **Dual-series charts (btop NET box)**: chartdef_t gained
  sidx2/label2 — STORAGE read/write, NET rx/tx and FAN cpu/gpu draw
  BOTH series in ONE chart over a shared honest scale (union range;
  secondary layer first, primary over it, two dots, two-colour label
  row "read 1.2 MB/s · write 0.3 MB/s", pair stats "rd a-b · wr c-d").
  Chart count on those details halves.
- **Window-average reference line (bottom)**: dotted muted line on
  detail charts where it floats above both curves — typical vs now.
- **Digit hints in box titles (btop)**: "CPU 1" … "POWER 8".
- Fixed an honest bug caught in review: the pair sentinel was 0,
  colliding with TS_CPU_TEMP=0 — non-pair minis rendered a bogus
  second series and "(null)" labels; sentinel is now -1.
- Verified: 0 warnings, make test, full tuitest, screenshots (digit
  titles, right-aligned values, clean captions, dual charts), raw
  stream checks, install in the same breath.

## 2026-10-28 — ui: FAN section + toggle escalation + sensor map (v0.8.0)

- **FAN telemetry section (8th)**: the `TS_FAN_CPU`/`TS_FAN_GPU`
  rings have been sampled every tick since v0.4 without ever being
  charted. New section (NET|FAN|POWER row; the grid is now a full
  4×2): cpu/gpu RPM wash charts with a new `U_RPM` unit, grid info
  "custom curve cpu on · gpu on · staged", detail adds a pointer to
  the FAN workspace view (the telemetry screen stays read-only).
  Footers/HELP say 1-8 (NET stays '6', '7' is FAN now).
- **ctron-toggle SIGTERM→SIGKILL escalation**: born from the 10-27
  incident — a wedged ctron ignoring TERM made every later key press
  take the close branch, so the app "would not open". `close_ctron()`
  gives the polite TERM ~2 s (settings save, worker join, pidfile
  removal all happen on that path) then escalates to `-9`; all three
  close sites route through it. Proven live with a fake comm=ctron
  process: TERM-trapping variant killed via escalation (log line
  present), polite variant closed cleanly.
- **HARDWARE.md telemetry sensor map (EN+TR)**: every 't'-screen
  source with its honest limits — Tccd label gating, the nvidia-smi
  field traps, whole-APU SoC PPT semantics, the gpu_metrics CPU
  0xFF no-go + root-only RAPL, spd5118 rarity, the net classifier
  exclusions, battery health caveats, PD-on-barrel-AC.
- tui-telem.png re-shot with the 8-box grid.
- Verified: 0 warnings, `make test`, full `make tuitest`, `make
  install` same breath (0.8.0 live); probe-child discipline held
  (zero stray ctron processes after every pty step).

## 2026-10-28 — ui: chart self-descriptions (desc per series)

- User: "grafiklerin yanına ve altına neyi temsil ettikleri ile
  ilgili yazıları yazarmısın". chartdef_t gained a desc field that
  travels with every series: detail pages draw it as a line under
  each chart's stats row (Tctl — CPU die temperature (hottest
  sensor); thread-0 live frequency; 1-min load average; whole-APU
  package power: CPU + iGPU; battery power flow (discharge watts);
  USB-C PD input power (-- when unplugged); ...), and the grid boxes'
  mini charts carry a "label — desc" caption line above them so the
  small graphs name themselves.
- Detail block overhead 2→3 rows (label/stats/desc); charts still
  get ~9-10 rows on a 50-line terminal. Grid mini needs ≥4 leftover
  rows now (caption + ≥3 chart rows).
- Verified: 0 warnings, make test, full tuitest, screenshots (desc
  lines under all three CPU charts, captions above the minis),
  install in the same breath.

## 2026-10-28 — ui: wash-fill charts — smooth area, crisp curve, live dot

- User on the two-tone block round: "sevmedim, daha detaylı hassas ve
  göze hoş gelebilecek ne yapabiliriz". The glyph-based fill was the
  last texture left; this round removes it entirely.
- The area under each curve is now a solid background COLOR WASH
  (accent halved into the theme bg, painted as cell backgrounds —
  zero glyphs, zero texture), and the curve is an eighth-block edge
  (▁▂▃▄▅▆▇█, bright accent) drawn over the same wash so it rises
  smoothly out of the area. Detail charts got taller (cap 8 → 12
  rows = up to 96 vertical levels), columns keep their window
  slice's PEAK again (spikes between columns no longer vanish), and
  the newest sample carries a bright ● marker so the current value
  pops. One put_cells() run per same-color span; ui_putln resets its
  own channels so the wash never leaks into other text.
- Grid boxes keep the value rows + info line and now ALSO draw the
  section's primary series as a small wash chart in the leftover
  space (≥3 rows) — every box carries one real graph without
  getting busy.
- Verified: 0 warnings, make test, full tuitest, screenshots (CPU
  detail: smooth wash + crisp bright curve + end dots at ~10 rows;
  grid: calm boxes with mini wash charts), install in the same
  breath.

## 2026-10-27 — ui: chart types matched to their context (block area + sparkline rows)

- User verdict after living with the braille rounds: the chart types
  weren't useful where they were used. Critical re-review (fresh
  screenshots) confirmed both contexts: 2-3-row braille minis in the
  grid boxes read as grey blobs (no shape separation, axis labels
  eating narrow boxes, boxes looking "busy"), and the dot texture in
  the large detail charts sat in front of the shape with a ragged
  top edge.
- Detail pages: charts are now TWO-TONE SOLID BLOCK AREA — each
  column fills with █ (dim accent2) and the top cell is an eighth-
  block (▁▂▃▄▅▆▇█, bright accent) so the series edge is a crisp
  bright line over a quiet fill (rows*8 vertical levels, no dot
  texture, no cell seams; one ui_putln per same-color run). Y-axis
  scale labels and stats rows unchanged.
- Grid boxes: mini charts dropped entirely — each series is one
  "label value sparkline" row (single-line block sparkline = the
  60 s shape next to the live value) plus the info line. Boxes are
  quiet again and read at a glance; series stay linked to the detail
  pages through the shared CD_* lists.
- Verified: 0 warnings, make test, full tuitest (incl. the spatial
  j+l→RAM proof and 1-7 digit jumps), screenshots of grid + CPU
  detail (two-tone separation confirmed, no tofu/seams), install in
  the same breath.

## 2026-10-27 — telem: sensor-fill round (v0.7.0)

- **Tccd1 / Tccd2** (k10temp temp3/temp4, label-verified once so a
  different chip layout can never mislabel): CPU grid+detail lines and
  `--status` gain " · Tccd 80/95 °C" — silent when absent, Tctl stays
  the headline.
- **RAM limits + alarm** (spd5118 temp1_max/crit + max_alarm): the
  grid shows "!>55" when the kernel flags a module over its limit
  (caught live during a stress run: modules 57/66 °C over max 55);
  the detail adds "limits max 55 · crit 85 °C · over max: module 1,2";
  `--status` appends "!over max".
- **NVMe Sensor 1/2** (temp2/temp3) → per-drive hottest: STORAGE
  detail "NVMe 51/49 °C · hottest 60/41 °C".
- **iGPU busy + sclk DPM** from the DRM device (dir cached once by
  glob): `gpu_busy_percent` + `pp_dpm_sclk` through the new pure
  `hw_parse_pp_dpm` (count / active `*` level / MHz, unit-tested);
  iGPU detail gains "busy 42% · sclk DPM 2/3 @ 2200 MHz", `--status`
  an iGPU busy line.
- **gpu_metrics verdict (live reverse-engineered)**: the 120-byte blob
  (signature `78 00 02 01`, kernel 7.2) has its CPU blocks
  permanently 0xFF on Dragon Range — proven by correlation: the only
  live power field (offset 28) tracks gfx busy 0–10 W while the
  package swings 7–27 W. **CPU power via gpu_metrics is a NO-GO on
  FA608PP** (RAPL stays root-only; sensor item ⑦ closed as
  not-honestly-readable). The one live slice is gfx power:
  `hw_metrics_gfx_mw` (signature-gated, range-checked, unit-tested)
  shows "gfx rail 10.0 W" beside the APU PPT explainer.
- AGENTS.md architecture map synced (telem.c, daeboard.c, main.c,
  ui/ SECTIONS note, tuitest pointer).
- **CCD columns field-verified by automated proof**: the CORE LIMITS
  overlay screen-dump matched sysfs L3 groups exactly (CCD1
  c0·16…c7·23 / CCD2 c8·24…c15·31) — the FAZ 2 "elle doğrulama" item
  closed without a human pass.
- Verified: 0 warnings, `make test`, full `make tuitest`, `make
  install` in the same breath (0.7.0 live).

## 2026-10-27 — telem: NET section + GPU pstate/throttle reads (v0.6.0)

- New **NET** section (7th; grid rows now STORAGE|NET, POWER last):
  `hw_net_if_whole` counts physical+wifi interfaces only (lo, veth*,
  docker*, br-*, virbr*, tap*, tun*, wg*, tailscale* excluded so
  traffic is never double-counted — the diskstats whole-disk spirit),
  `read_net_ifs` reads cumulative `statistics/rx_bytes`/`tx_bytes` +
  `operstate` each fast poll, and the `TS_NET_RX`/`TS_NET_TX` series
  ride the existing rings with the disk-style delta rule (an interface
  disappearing resets the counter: one unknown tick, not a spike).
  Grid box charts rx/tx with the interface list ("wlan0 up ·
  enp3s0 down"); the detail page lists per-interface cumulative totals
  plus the grand total; `--status` prints a NET line. New shared
  `hw_fmt_bytes` (GB/MB/`--`), unit-tested.
- nvidia-smi query grew `pstate` + the ten **named**
  `clocks_event_reasons` flags (underscore spelling — the dashed names
  in `--help` are NOT valid query fields; the `active` bitmask variant
  returns hex like `0x400` on driver 580, so the self-labeled
  "Active"/"Not Active" fields are parsed instead). A driver lacking a
  field rejects the WHOLE query, so one failure latches a permanent
  in-session fallback to the old 6-field form — nothing else degrades.
  GPU grid info shows pstate, the detail page adds "pstate P4 ·
  throttle: none/sw power cap, …" through `hw_gpu_throttle_str`, and
  `--status` appends pstate plus a throttle line when any reason is
  active. `hw_parse_gpu_csv` extended (pstate token + Active bits),
  unit-tested with real driver output.
- Footers/HELP say 1-7 now; the smoke flow jumps digit '6' into NET
  (probe word `total` — `rx` itself is fragment-prone: coming from the
  STORAGE page the 'r' cell does not change, the diff skips it).
- tui-telem.png re-shot with the 7-section grid under live load.
- **LICENSE added (MIT)** after the user's "nothing to change, low
  cost" pick, with a matching README badge.
- Verified: 0 warnings, `make test`, full `make tuitest`, `make
  install` in the same breath (0.6.0 live).

## 2026-10-26 — ui: spatial grid navigation in the telemetry overlay

- User request: arrows used to walk the sections linearly (1→2→3…);
  now they move in 2D matching the on-screen 2×3 grid — left/right
  step inside a row, up/down step between rows, edges clamp (no
  wraparound; "down" from the bottom row stays put). hjkl mirror the
  arrows; the column count follows the same wide/narrow rule as the
  draw pass (2 columns ≥78 cols, else 1 — where left/right no-op).
  Detail-page h/l keeps its linear section flip (a list, not a grid).
- Smoke flow updated to PROVE the spatial behavior: j (down) + l
  (right) from CPU must land on RAM (asserting the swap line; the
  old linear stepping would have opened the iGPU page).
- HELP lines extended (hjkl/arrows walk the grid, h/l flips details).

## 2026-10-26 — telem: self-describing charts + APU PPT honesty + PD/battery reads

- "Grafiklerin neyi temsil ettiğini anlayamıyorum": every chart now
  carries a y-axis of its actual scale — top-left max, bottom-left
  min, unit included (temp 100/0 °C, clock 2378/2212 MHz, ...) — so
  each graph states what its height means without cross-referencing
  anything; charts ≥2 rows get the axis, 1-row minis skip it.
- iGPU power honesty (user spotted it): amdgpu power1_input is the
  SoC PPT = whole-APU package power (CPU+iGPU), not the iGPU rail.
  Chart relabelled "APU PPT" (grid + detail) with an explicit
  "whole-package power (CPU+iGPU)" info line on the detail page;
  iGPU grid box now shows the vddgfx/vddnb voltages instead.
- POWER section gains the USB-C PD story: new TS_PD_MW series + usb-c
  chart (honest empty "--" while nothing is attached — this machine
  is on barrel AC, the two ucsi ports read 0), read_pd_ports()
  scanning type="USB" supplies each fast poll (µV×µA→mW), grid info
  appends "USB-C in W" when live, detail adds a PD-ports line.
- Battery electricals: voltage_now/current_now stored (mV/mA) and
  shown on the POWER detail ("battery 14.69 V · 2.76 A · health 89%
  of design").
- Verified: 0 warnings, make test, full tuitest, screenshots (axis
  labels present on every multi-row chart, no overlap; POWER shows
  3 charts + PD line), make install in the same breath.

## 2026-10-26 — ui: telemetry charts take 3 fixes (area fill, white info text, grid parity)

- User follow-up on the braille line charts: (1) "ne gösterildiği
  anlaşılmıyor" — a lone 1-dot line is too faint to read at real
  terminal sizes; (2) the muted-grey stats/info lines were nearly
  unreadable; (3) the new charts only lived in the detail pages, the
  grid boxes still had the old one-row sparklines.
- Charts are now braille AREA graphs: dots filled from the bottom up
  to each sample, so the series shape reads at a glance (btop look);
  scale fix from the previous entry still applies.
- info_line switched from pal->muted to pal->text across the
  telemetry panels (stats + info lines are real numbers; footers stay
  muted). Theme palettes untouched — this is local to the telemetry
  screens.
- Grid/detail parity: per-section series lists (CD_CPU..CD_POWER)
  are now shared by both views through one stack_charts() stacker
  (detail: stats row + min height 2 + tall blocks; grid: compact,
  min height 1, cap 4 rows; both shrink-then-drop on short boxes).
  metric()/sparkline rows deleted from the grid sections; telem_spark
  itself stays (LIVE strip elsewhere + unit tests).
- Verified: make 0 warnings, make test, full make tuitest,
  screenshots (grid shows the mini area charts in all six boxes,
  info lines white, no tofu), make install immediately after.

## 2026-10-26 — ui: detail charts redrawn as braille line graphs

- User report: the block area charts "çok bozuk" in the real terminal.
  Two causes, both fixed: (1) AUTO window scaling amplified a
  near-flat series (e.g. clock 2212–2378 MHz) into full-height noise
  and rescaled every 250 ms tick (constant jumping); (2) half-block
  ▄ cells leave seams between rows on many font/line-height combos.
- Charts are now btop-style braille line graphs: 2×4 dots per cell
  (rows×4 vertical levels, no cell seams), one thin line drawn
  left-to-right with vertical interpolation between points, newest
  right. Tofu-free in DejaVu-class monospace fonts.
- New chart_scale() for AUTO series only (fixed 0-100 ranges pass
  through untouched): minimum span = 25% of the larger magnitude so a
  flat series LOOKS flat, 5% headroom pad, and an exponential follow
  (k=0.2) that hides the rescale jump as the 60 s window slides.
  Chart height cap 10→8 rows (braille quadruples the resolution).
- No libm dependency introduced (dmax3 helper instead of fmax/fabs —
  the Makefile has no -lm and portability keeps it that way).
- Verified: make 0 warnings, make test, full make tuitest on both
  build/ and the INSTALLED binary, screenshots (thin lines, no tofu,
  flat clock renders flat with gentle wiggle). Rides the unpushed
  v0.5.0 (tag untouched).

## 2026-10-26 — ui: fullscreen telemetry section detail pages (v0.5.0)

- The 't' fullscreen grew a second mode: j/k (or arrows) walks the
  section grid with an accent-selected box, Enter/Space or 1-6 opens
  that section's detail page; h/l flips between detail sections
  without going back, esc backs out to the grid and closes from
  there. t/T close from either mode, q quits — rule 5 untouched.
- Detail pages stack full-width multi-row block charts over the same
  ~60 s rings (new pure accessor `telem_at`; every column keeps its
  window slice's max, so a spike between columns still shows; half
  blocks double the vertical resolution) plus min/max/last stats and
  info lines from a fresh `hw_refresh_live` run on every detail
  entry/switch: topology/EPP/PPT/fans and per-core ceiling blocks on
  CPU, VRAM/clock max/lock/NV boost on GPU, module temps on RAM,
  NVMe temps + / fill bar on STORAGE, status/AC/charge limit on POWER.
- Two cheap reads landed with it: amdgpu `in0/in1_input` (iGPU
  vddgfx/vddnb, mV) in the fast-poll extras, and battery health %
  (`charge_full`/`charge_full_design`, `energy_*` fallback) in the
  live snapshot — both honest ("--") when absent.
- Grid boxes are mouse targets (click selects, second click opens —
  presets-modal convention); a click outside the overlay closes it
  like every other overlay. HELP line and both footers updated in the
  same commit (new key = HELP line).
- Verified per commit: make 0 warnings, make test (telem_at unit
  tests added), full make tuitest — the telem flow now walks
  cursor/enter/h-l/digits/esc/stray-key — plus screenshots of the
  grid and the CPU/POWER detail pages, and make install.

## 2026-10-26 — security: privileged writes fully shell-free (follow-up pass)

- control: cpufreq batch tee + PPT go argv (12bd760) —
  `cpufreq_write_all` feeds the value through `ut_write_via_argv`
  (one `sudo -n tee`, every path an argv word; journal-proven single
  invocation for all cpus, and the 4 KB command-string ceiling that
  refused machines beyond a few dozen cpus is gone).
  `ctrl_ppt_*_cmd` string builders became `ctrl_ppt_*_argv` vector
  builders (unit tests assert the words; flag order + W→mW mapping
  unchanged). Live: EPP roundtrip through the real sudo rule.
- daeboard + doctor: `system()` gone tree-wide (53fd272) — start/
  install sudo calls and the `--doctor` probe exec argv vectors;
  env-derived paths are words, never parsed, so the manual
  quote-rejection checks left with the shell. `--doctor`'s
  "sudo -n: unavailable" verified honest here (`sudo -n true` is not
  in the NOPASSWD set under the minimal ruleset).
- SECURITY.md §2.2/Phase-2.1 honesty update: remaining shell users are
  the read-only code-constant asusctl/busctl calls and the two
  `command -v` probes — no config- or user-derived string is parsed
  by a shell anymore.

## 2026-10-26 — docs: machine-local setup notes genericized

- Wording pass over SECURITY.md / HANDOFF.md / CHANGELOG.md: per-host
  setup-state sentences replaced with generic guidance (§4.4 NixOS
  paragraph reworded; two session-record lines trimmed). Rule
  examples and generic risk notes untouched. No code change.

## 2026-10-26 — perf: FAZ 4 items 13/14/15 landed (key-burst 8.7% → 3.3% CPU)

- hw: cpu_limits_sweep split (a7d82b5) — the static column (present +
  cpuinfo_min/max, 3 files per cpu) is re-read only at init, on
  hw_refresh_live and every ≥5 s in the fast poll; the per-tick pass
  reads scaling_max_freq only. Fast path 48 → 17 file reads; raise-only
  cpuinfo_max rule untouched; --status output verified identical.
- ui: post-key refresh rate-limit + input batching (e2dd603) — keys
  that arrive while drawing are drained via notcurses_get_nblock and
  dispatched individually, then ONE rate-limited refresh covers the
  batch (120 ms floor); timeout/resize/apply-completion force. Writes
  unaffected (worker refreshes itself, POWER entry runs refresh_live,
  kbd/bat rows update hw in place). Measured with the same probe:
  key-held 8.73% → 3.33% (2.6 → 1.0 ms per keypress); idle 1.10 →
  1.00%. Remaining per-key cost is the draw phase (real feedback) —
  deliberately stopped there.
- build: stripped install + opt-in LTO=1 (347e881) — install -s
  strips only the installed binary (285 → 261 KB; the earlier
  150–180 KB estimate was wrong — no -g in the build, so strip only
  drops the symtab); LTO=1 adds -flto=auto to compile and link (0
  warnings, tests green); default build unchanged.
- Verified per commit: make 0 warnings, make test, make tuitest (all
  flows — the back-to-back key sequences stress the batching), live
  --status/--doctor, make install + installed --version. No release
  cut (internal performance; rides the next MINOR).

## 2026-10-26 — docs: performance audit → NEXT.md FAZ 4 (measured)

- Comprehensive optimization sweep of the hot paths (main loop, fast
  poll, telemetry sampling, draw helpers) with live measurements
  (pty harness + /proc/<pid>/stat sampling; throwaway probe, not
  committed): idle 1.10% CPU, telemetry fullscreen 1.13% (draw cost
  negligible — no malloc/exec anywhere in the draw path), key-held
  burst ~30 Hz 8.7% (2.6 ms per keypress — every key runs a full
  hw_refresh_fast + redraw, ~69 file reads/tick), nvidia-smi extended
  query 20–30 ms every 2 s on the UI thread, --status 0.21 s, binary
  285 KB unstripped.
- NEXT.md gains FAZ 4 (items 13–16, ranked by measured impact): key→
  refresh decoupling (rate-limit or input batching), cpu_limits_sweep
  split (static cpuinfo_* throttled, scaling_max every tick), install
  strip + optional LTO, conditional nvidia-smi worker thread; plus a
  weak-justification record (fd/pread cache, statvfs cache,
  erase_region, march=native — with reasons to leave them by default).
  README Roadmap untouched (its parked list mirrors the feature-parked
  list, not audit notes). No code changes.

## 2026-10-26 — ui: 'T' opens fullscreen telemetry everywhere (telemetry "would not open" report)

- User reported the telemetry screen would not open. Root cause was a
  stale install: ~/.local/bin/ctron still reported 0.2.1 — no release
  since had run `make install` (the running binary in the repo has had
  the feature since 6beee01). Reinstalled; verified 0.4.0 live.
- While diagnosing, one real trap was tightened: lowercase 't' kept
  its settled exact-value meaning when the workspace is focused in the
  FAN/POWER views, so 'T' (shift) now opens the telemetry from
  ANYWHERE (the POWER typing starter dropped its uppercase alias; the
  fan editor only ever bound lowercase). At startup — focus on the
  profiles panel — plain 't' already opened it.
- tuitest flow_telem_full now pins all three paths: 't' at startup
  (no editor owns it), lowercase 't' left to the fan editor while the
  workspace is focused, 'T' opening from exactly that state. make 0
  warnings, unit + tuitest green, installed fresh.

## 2026-10-26 — release: v0.4.0 (MINOR — fullscreen telemetry)

- MINOR per the agent-managed semver policy: the new user-facing
  feature is the fullscreen sectioned TELEMETRY screen on contextual
  't' (six sections, rolling sparklines, read-only) plus the telemetry
  read layer behind it (extended dGPU query, iGPU amdgpu hwmon, RAM
  module + NVMe temps, mem/load/diskstats/fs usage) and the new
  --status telemetry block.
- VERSION (src/main.c), README version badge and flake.nix version
  bumped together in this commit per policy; annotated tag on it, push
  stays user-side. Roadmap: the "mini-graph scope decision" item is
  closed — the 't' screen supersedes it (PLANS.md dash plan marked
  superseded; only its ASCII-art picker stays parked).

## 2026-10-26 — ui: fullscreen sectioned TELEMETRY on contextual 't' (FAZ: telem, step 3)

- New src/ui/panel_telem_full.c: a section REGISTRY drives the screen
  (SECTIONS[] — adding/removing/moving a section is a one-line edit),
  a responsive 2-column grid (single column under 76 cols, too-tiny
  sections skipped rather than crammed), and per-section metric rows
  (label + current value + block sparkline) plus read-only info lines.
  Six sections ship: CPU (temp/clock/load + fan·cores·EPP), GPU
  (temp/clock/power + util·VRAM·max), iGPU (temp/clock + power), RAM
  (used spark + module temps·swap), STORAGE (read/write B/s sparks +
  / fill bar + NVMe temps), POWER (draw/charge sparks + status·AC·
  limit). Nothing on this screen writes; sampling rides the poll loop
  (telem_poll after each hw_refresh_fast, never under apply_busy).
- Key wiring: 't'/'T' opens the fullscreen everywhere EXCEPT where 't'
  already starts exact-value typing (FAN/POWER views — settled keymap
  untouched); while open, t/T/ESC close, q/Q quit the app (rule 5),
  everything else is swallowed. Layout uses the db_overlay fullscreen
  geometry. HELP view + LIVE strip hint lines updated (new key = HELP
  line, same commit).
- tuitest flow_telem_full: open from a non-typing view, all six
  sections render, stray '5' swallowed, close + reopen round-trip, q
  exits rc=0 from the fullscreen. Visual check: docs/tui-telem.png
  captured live (160x50) and inspected — sections, real values, no
  clipping; README shows the new screen. make 0 warnings, unit +
  tuitest green.

## 2026-10-26 — hw: telemetry read sources — iGPU/RAM/NVMe temps, extended dGPU query, disk/mem/load (step 2)

- (Recorded with step 2 commit 59cdc97.) Extended single nvidia-smi
  query (temp + power.draw + graphics clock + utilisation + VRAM; 2 s
  cache; N/A fields honest -1), amdgpu hwmon iGPU temp/freq/power,
  spd5118 RAM module temps (×2) and nvme temps (×2) via the new
  hw_hwmon_path_nth/cached_hwmon_nth, /proc meminfo + loadavg +
  diskstats (whole-disk parser — partitions/loops excluded so bytes
  are not double-counted), statvfs / usage. Pure parsers
  unit-tested; --status prints the new block live (verified: GPU
  6.08 W draw, iGPU 57 °C/600 MHz, RAM modules, NVMe ×2).

## 2026-10-26 — telem: pure sample-ring + sparkline core (step 1)

- (Recorded with commit 5d0bf8a.) X-macro TELEM_SERIES table — enum
  and defs from one list, a new metric is one row + one extractor;
  240-slot rings with a 250 ms gate, -1-unknown skip, UTF-8 block
  sparkline with fixed-range clipping / AUTO window scaling / flat
  mid-level. Unit tests: gate, wrap, minmax, scales, clip, skip,
  empty.

## 2026-10-26 — build: flake.nix added (nix build + nix develop)

- v1 had a flake, v2 never did; Arcioth built via `nix-shell -p gcc
  pkg-config notcurses gnumake --run make`. The flake makes that
  permanent: `packages.<sys>.ctron` (+ `default`) as a plain-Makefile
  stdenv derivation installing build/ctron, and a devShell carrying
  exactly the documented toolchain for `make` inside `nix develop`.
- The in-flake `version` rides the release commit from now on
  (policy line added to NEXT.md: VERSION + README badge + flake
  version move together).
- Honest limit: this machine has no nix — the flake is NOT
  build-verified here (and the retired v1 copies no longer carry one
  to copy from). FA507NVR checklist (Arcioth): `nix build`,
  `nix develop` + `make`, generate + commit `flake.lock` on first
  run. NEXT.md FAZ 1 item 3 stays open until then.

## 2026-10-26 — display: multi-monitor parsing — the panel's own modes, not the first monitor's

- kde backend: output blocks are now picked (the eDP* laptop panel
  when present, else the first "Output:" block) and the Modes line is
  parsed strictly within that block. Previously the first "Output:"
  and the first "Modes:" in the whole buffer were taken, mixing or
  misattributing monitors when an external output is listed first.
- hyprland backend: name, geometry AND mode list now all come from
  ONE extracted monitor object (focused:true, else the first) via new
  hypr_obj_from/hypr_mon_obj — hypr_modes_hz and
  hypr_pick_mode_string previously took the first "availableModes"
  array anywhere in the JSON, which could list the wrong monitor's
  modes on a focused-panel + external-HDMI setup.
- Offline tests: a two-output kscreen-doctor sample (HDMI first → eDP
  picked, HDMI's 120 Hz absent, current hz from the panel block) and
  a two-monitor hyprctl JSON (focused wins; no-focused falls back to
  the first; empty array fails honestly). New disp_hypr_test_parse
  seam mirrors disp_kde_test_parse.
- Verified: make 0 warnings, unit green, live single-monitor
  regression identical (165 Hz, 60/165 modes on FA608PP). Live
  two-monitor proof (HDMI-A-1) moved to the FAZ 2 field-test items.

## 2026-10-26 — release: v0.3.1 (PATCH — security chain)

- PATCH per the agent-managed semver policy: everything since v0.3.0
  is hardening/fixes/docs with no new user-facing features — the
  singleton pidfile O_EXCL|O_NOFOLLOW claim, shell-free privileged
  writes (ut_write_via_argv), argv-vector exec for
  nvidia-smi/kscreen-doctor/hyprctl with the 2>&1 honesty fix, fan
  provenance wording, SECURITY.md §4 sudoers documentation, and the
  ctron-ppt wrapper probe.
- VERSION bump (src/main.c, the only definition) + README version
  badge in this commit; the annotated tag sits on this commit, push
  stays user-side (commits + tags: `git push && git push origin
  v0.3.1`).

## 2026-10-26 — power: ctron-ppt wrapper probed before ryzenadj (FAZ 3.12)

- When `/usr/local/bin/ctron-ppt` exists and is executable, PPT writes
  through ryzenadj-mode now invoke `sudo -n /usr/local/bin/ctron-ppt
  <mW> <mW> <mW>` FIRST — with the wrapper installed and allowed
  (SECURITY.md §4.3), ryzenadj is never invoked directly by ctron and
  the SMU sudoers surface shrinks to three integers. A missing wrapper
  or a denied sudo rule falls through to the existing candidates
  (/usr/bin → /usr/sbin → bare), so there is no configuration and no
  behaviour change on machines without the wrapper (inert on the
  FA608PP today — the wrapper is not installed there yet).
- `ctrl_ppt_wrapper_cmd` exported pure + unit-tested (bare-int mW
  mapping, default path); SECURITY.md §4.1/§4.3 updated to the probe
  reality. make 0 warnings, unit + tuitest green. **FAZ 3 is now
  fully closed.**

## 2026-10-26 — security docs: code-verified /etc/sudoers.d/ctron (FAZ 3.11)

- SECURITY.md gained section 4: exactly what ctron execs with sudo,
  derived from the code and verified against the live machine — the
  `sudo -n tee` path families (ut_priv_write), the ryzenadj candidates
  in try order (/usr/bin, /usr/sbin, bare), the absolute nvidia-smi
  path from `command -v`, daeboard's systemd-run/install, and the
  `sudo -n true` probes.
- The merged-usr footgun is spelled out: /usr/sbin is a symlink to
  /usr/bin on Arch/CachyOS but sudoers matches the LITERAL string
  ctron sends — bare `tee` resolves through sudo's secure_path (sbin
  first), ryzenadj arrives as /usr/bin/ryzenadj (first candidate),
  nvidia-smi as /usr/sbin/nvidia-smi. Mismatched spelling shows up as
  "password required" while `sudo -n true` works.
- §4.2 minimal ruleset keeps the residual risk honest (`ryzenadj *`
  still allows other SMU flags if typed manually — §2.1's concern);
  §4.3 is the stricter three-integers wrapper, which needs the small
  ctrl_ppt_ryzen_cmd probe follow-up now tracked as NEXT.md FAZ 3
  item 12 before ctron itself can use it; §4.4 documents NixOS
  store-path instability (FA507NVR unverified);
  §4.5 is the install verification checklist. Live-checked: both asus
  fan hwmons (`asus`, `asus_custom_fan_curve`) resolve under
  /sys/devices/platform/asus-nb-wmi/*, so the tee rule family covers
  curve points + pwm enable.

## 2026-10-26 — security: nvidia-smi / kscreen-doctor / hyprctl exec via argv, no shell (FAZ 3.9)

- New ut_exec_argv family in util.c: fork + execvp of a plain argv
  vector under `timeout 3`, stdout captured into out (first line /
  whole-with-ANSI-stripped / stderr-merged variants: ut_exec_argv,
  _raw, _both). Output beyond the buffer is fully drained — the same
  SIGPIPE lesson as ut_exec's drain fix, now applied to the capture
  path too: a chatty child can no longer fake rc=141 through a
  truncated read.
- Migrated every helper call the phase names: nvidia-smi temperature
  and max-clock reads (hw.c), the clock-lock verify sampler and
  `sudo -n <bin> -lgc/-rgc` (control.c), kscreen-doctor `-o` query +
  mode apply (display_kde.c), hyprctl `monitors -j` + `eval` +
  `keyword` fallback (display_hypr.c). Nothing builds shell strings
  for these anymore.
- Behaviour note: the old lock/reset commands carried `2>&1`, but
  exec_run appended `2>/dev/null` AFTER it — stderr was effectively
  dropped and "GPU clocks set to" always arrived via stdout. _both
  now genuinely merges stderr, so FAILED logs also show sudo's own
  complaint instead of "no output".
- Not migrated (documented): ut_have_cmd and nvidia_smi_bin must keep
  a shell because `command -v` is a builtin; asusctl and the
  ints-only ryzenadj commands stay on ut_exec for a later pass.
- Tests (check_argv_exec): echo capture + first-line trim, exit-status
  passthrough (false, missing binary = 127), ANSI strip, stderr merge,
  and a 100k-line child that must not SIGPIPE. Verified live:
  --status reads GPU temp/max-clock (nvidia-smi) and 165 Hz + modes
  (kscreen-doctor) through the new paths. make 0 warnings, unit +
  tuitest green.

## 2026-10-26 — security: ut_priv_write no longer spawns a shell (fork+execve argv)

- The sudo fallback used to build `printf '%s' '<val>' | sudo -n tee
  '<path>'` and run it through popen's /bin/sh, with a pre-exec
  single-quote rejection as the only injection guard. The fallback is
  now ut_write_via_argv(): pipe2(O_CLOEXEC), fork, the child dup2's
  the read end onto stdin and execvp's a pure argv vector —
  `timeout 3 sudo -n tee <path>` (the 3 s cap survives as an argv
  word, not a shell string). Quotes and metacharacters can no longer
  be parsed by anything, so the rejection guard is removed.
- Parent side survives a child that exits before reading (SIGPIPE
  ignored around the single feed write, EPIPE breaks the loop) and the
  child's exit status decides success — same lesson as the ut_exec
  SIGPIPE race: a dead child is a failed write, never our crash.
- ut_write_via_argv is exported as a generic stdin-feeding argv runner
  and unit-tested without sudo: a real `tee` child writes our bytes
  verbatim (quote round-trip pinned), an early-exiting child (`true`)
  must not kill the runner, and unwritable paths still fail cleanly
  through the real chain. Existing no-sudo and fd-leak coverage
  unchanged. SECURITY.md Phase 1.1 marked done (1.3 done earlier
  today). make 0 warnings, unit + tuitest green — the fan-write flow
  exercises the new path live.

## 2026-10-26 — security: singleton pidfile claim hardened (O_EXCL|O_NOFOLLOW, 0600, ownership)

- FAZ 3.10 audit result (SECURITY.md Phase 1.3): the toggle-script
  claim was already safe — noclobber create is O_EXCL and the stale
  steal is `rm -f`, which never follows symlinks — but the TUI
  singleton pidfile claim wrote through ut_write_file = fopen("w"):
  no exclusivity, no O_NOFOLLOW, 0644. In the shared /tmp fallback a
  planted /tmp/ctron-tui.pid symlink would have been followed on the
  stale-takeover write, clobbering the symlink's target.
- ut_singleton_enter now claims with open(O_WRONLY|O_CREAT|O_EXCL|
  O_NOFOLLOW, 0600) in a bounded retry. An existing entry is trusted
  as a holder only when lstat says regular file owned by the effective
  uid; anything else (symlink, foreign owner, garbage, dead pid) is
  unlinked — unlink never follows — and the exclusive create retried.
  A foreign entry that cannot be taken (sticky /tmp) fails open as
  before. The old pre-loop fast-path read that trusted the file
  before any ownership check is gone; every read now sits behind the
  lstat gate. ut_singleton_leave is unchanged (unlink never follows
  and already required a content match).
- ctron-toggle.sh: umask 077 (stamp/log/js are single-user artifacts)
  and log() refuses to append through a symlinked LOG in the /tmp
  fallback.
- Regression test (check_singleton): a planted pidfile symlink
  pointing at a victim file survives ctron's claim untouched while
  the slot is retaken by a regular 0600 file; the existing
  defer/claim/leave coverage is unchanged. make 0 warnings, unit +
  tuitest green.

## 2026-10-26 — fan provenance: S_SNAPSHOT upgraded to the 10-18 probe finding

- Wording-only change (no behaviour): the S_SNAPSHOT_* provenance
  comment (src/fan.c), the fan.h preset comment and FAN_PSRC_BUILTIN
  enum note, and the presets-modal source label now state the probe
  result — asusd's own defaults are GENERIC fallbacks, so this
  machine's ron (which the snapshot matches exactly) is the best
  available factory reference, most plausibly EC-seeded by asusd at
  first init. The residual caveat stays written down (asusd rewrites
  the ron on every curve change; pre-2026-10-17 edits on this machine
  may be baked in). Modal label: "built-in snapshot 2026-10-17" →
  "EC-seeded snapshot 10-17". Factual "factory" wording about
  pwmN_enable kernel semantics is untouched.
- HARDWARE.md EN+TR: new footgun bullet — `asusctl fan-curve
  --default` installs asusd's GENERIC curves (every profile disabled,
  255 °C sentinel), not the OEM factory curve.
- Roadmap: NEXT.md FAZ 1 item 2 closed. Verified: make 0 warnings,
  make test + make tuitest green.

## 2026-10-25 — docs: README redesigned as a GitHub landing page

- Real TUI screenshots (not mockups): `docs/tui-fan.png` (fan curve
  editor, hero) and `docs/tui-power.png` (POWER view), captured from
  the live binary by the new permanent tool `scripts/tui_screenshot.py`
  — pty Session from tui_smoke.py + a small ANSI terminal-emulator
  replay + PIL render (no pyte/tmux/pip needed on this machine).
  Isolated CTRON_CONFIG, so the real settings.ini is untouched.
- README restructured: centered hero with shields badges
  (version/language/dependency/platform), features table, working-
  principles list, TUI key table, collapsible details for display
  backends / KDE M4 key / code layout, documentation index.
- Truth fixes while rewriting: CLI reference regenerated from the real
  `--help` (`--setup`/`--tui` were stale → removed; `--freq core|ccd`,
  `--ac-profile`/`--battery-profile`, `--follow`/`--daeboard-*`,
  `--fan cpu|gpu on|off` added; the duplicated `--ppt` entry
  collapsed), snapshot directory corrected to `profiles/*.ctr`,
  parked list synced with NEXT.md's unified list.
- Version policy (NEXT.md): the README `version` badge is updated in
  the same release commit as the VERSION bump.
- No code changes; version stays v0.3.0 (docs-only).

## 2026-10-25 — release: v0.3.0 (first tag; numbering now agent-managed)

- Version numbering delegated to the agent by the user. Policy recorded
  in NEXT.md: semver (PATCH = fixes only, MINOR = features/UX, MAJOR =
  breaking config/CLI changes); the annotated `vX.Y.Z` tag sits on the
  same commit as the VERSION bump; pushing tags stays with the user,
  like every push.
- First cut v0.3.0 — the accumulation since 0.2.1 (09-27) is
  MINOR-grade: fan presets modal with source visibility, M4 toggle +
  single-instance guard + clean SIGTERM exit, PPT/ryzenadj write path,
  GPU clock lock, daeboard control + binds editor, background apply
  worker.
- VERSION bump in src/main.c (the only definition) + README title.
  make 0 warnings, make test green, `--version` prints 0.3.0.

## 2026-10-25 — docs: NEXT.md rewritten as the single-source roadmap

- NEXT.md is now roadmap-only: version policy, FAZ 1 maintenance &
  honesty (S_SNAPSHOT provenance reword from the 10-18 probe,
  HARDWARE.md `--default` footgun note, flake.nix), FAZ 2 field-test
  debt (FA507NVR live tests, FA608PP CCD/OCCT field tests,
  multi-monitor parsing, Hyprland TUI mouse), FAZ 3 security hardening
  (ut_priv_write fork+execve, ut_exec_argv migration, pidfile O_EXCL
  audit, sudoers doc), mini telemetry graphs parked with a scope
  decision pending (user choice), one unified parked list. The old
  mostly-BİTTİ archive stays in git history (this file at 5570b48).
- Truth fixes: PLANS.md's daeboard sections still said "Not built" —
  both shipped (client + control + binds editor; daeboard.c,
  editor_daeboard.c, LIGHT rows, CLI --daeboard-*/--follow); dashboard
  section marked unbuilt with a pointer to NEXT.md. README Roadmap
  (parked) unified with NEXT.md's list (+UI language table, +polkit
  helper) and now points at NEXT.md as the single source.

## 2026-10-18 — hardware finding: asusd's regenerated defaults are GENERIC, not factory

- One-off probe (script in ~/asusd-default-probe.sh, user-run with
  sudo; artifacts in ~/asusd-default-probe/): moved fan_curves.ron
  aside and restarted asusd — it regenerated the file in 1335 bytes
  with curves that are clearly generic fallbacks, NOT this model's
  factory data: every profile comes back `enabled: false`, quiet
  carries a 255 °C sentinel point, temps start at 20 °C, and the pwm
  ramps look formula-generated. Restored afterwards, byte-identical
  (md5), service active.
- Consequence: the CURRENT ron (which all three ctron S_SNAPSHOT_*
  tables now match exactly — Quiet and Perf always did, Balanced was
  synced on 10-17) is the best available "factory" reference: most
  plausibly seeded from the EC by asusd at first init, since asusd's
  own defaults demonstrably differ. The 10-17 provenance caveat
  ("may include user edits") still stands but is now bounded: the
  only alternative source is the EC, not some other machine's table.
- Footgun documented: `asusctl fan-curve --default` exists ("set the
  active profile's fan curve to default") but per the probe it
  installs these GENERIC curves, not the OEM factory curve — avoid it
  when factory behaviour is the goal.

## 2026-10-17 — fan presets: Balanced tables synced, sources made visible

- User suspicion confirmed: the built-in "ASUS factory" Balanced
  tables did not match the machine's real asusd curves — the GPU table
  was a completely different curve (a 53→255 step, likely from another
  TUF model) and the CPU pwm column diverged on 4 of 8 points. Quiet
  and Performance were byte-identical. The built-ins now carry this
  machine's asusd values (ron snapshot 2026-10-17), renamed
  S_FACTORY_* → S_SNAPSHOT_*.
- Provenance honesty: asusd rewrites fan_curves.ron on every curve
  change (ctron's own fan writes persist there too), so neither the
  ron nor the snapshot is VERIFIED factory data — "factory" is gone
  from the UI and comments; sources are labelled as "asusd ron" vs
  "built-in snapshot 2026-10-17".
- The FAN PRESETS modal now shows a source line: "Source: asusd ron ·
  CPU ✓ GPU ✓", per-fan asusd disabled flags ("· CPU+GPU off (factory
  runs)" — this machine's Quiet profile has both disabled, so loading
  its points would silently do nothing), and Active's fallback chain
  (live EC curve / init snapshot / generic default). Preset labels
  dropped the "(ASUS factory …)" suffixes; the apply button now reads
  "Load into editor (Enter)" — the modal never writes the EC, and now
  it says so at the point of action. HELP's 'm' line matches. Also
  fixed: preview rows printed outside the frame when the modal was
  height-clamped on tiny terminals (rows are now guarded).
- API: fan_preset_get gains an optional fan_preset_info_t (source +
  enabled), fan_ron_parse gains optional enabled out-params, and the
  built-in fill is exported as pure fan_preset_builtin() so tests can
  pin the tables without /etc present. Tests: full 2×8 Balanced pin
  (sync marker), Quiet/Perf spot pins, enabled:false parsing with
  per-fan attribution, source-path assertions (pure where possible;
  ACTIVE on real hardware documented as HWMON|STOCK).
- Live-verified in the real TUI via the harness: ron source line,
  "Load into editor" button, per-preset sources (ron/ron/ron/live EC),
  and the both-fans-off Quiet wording. make 0 warnings, unit + tuitest
  green, installed.

## 2026-10-16 — test environment: the guard no longer fights the harness

- Full-review pass found the pty harness red (9–10 flows) and 4 unit
  failures at HEAD — while the SAME commits had passed when landed.
  Two intertwined causes, both around the 654a80f single-instance
  guard:
  1. While a REAL ctron TUI runs on the desktop, every harness
     instance bowed out ("already running") and its flows starved.
     Fix: the guard fails open when `CTRON_TEST` is set (the harness
     Session now sets it for every spawn), and when `CTRON_CONFIG` is
     set (an isolated config dir is a deliberately parallel instance —
     different configs may legitimately run side by side).
  2. The 4 unit failures were check_singleton's deferred/claim paths:
     older tests set `CTRON_CONFIG` and never restore it — the value
     leaks into the process env and tripped the new bypass. Latent
     for weeks (nothing read it afterwards); the bypass surfaced it.
     check_singleton now saves/clears/restores both variables around
     itself. The ambient leak itself is left as-is (pre-existing test
     order dependencies, parallel-session territory) but documented.
- Verified: make 0 warnings, unit + all tuitest flows green —
  including a run with the user's real TUI open, which is the exact
  condition that used to starve the harness.
- Debugging lesson (cost an hour): bare `make` builds only
  `build/ctron` — the test binary is `make build/test_core`/`make
  test`; three "no effect" iterations ran a 2.5 h-old binary. Assert
  on timestamps when output refuses to change.

## 2026-10-14 — Fan Curve Startup Honesty & FAN PRESETS Modal Overlay

- User feature request: FAN editor in the TUI must initially display the
  active/live hardware curve directly from the EC hwmon instead of showing
  stale curves from `settings.ini`.
- Staging fix: `settings_load()` in `src/settings.c` no longer marks
  `hw->fan_staged = true` at startup, nor does it clobber live hardware
  curves when `hw->has_fan_curve` is true. Staged edits remain user-initiated
  intent during the active session. Custom curve persistence across reboots is
  honestly handled via SNAPSHOTS (`.ctr` profiles).
- New FAN PRESETS Modal Overlay (`m`):
  - Accessible via `m`/`M` hotkey or clicking `[ Presets (m) ]` in the FAN editor.
  - Floating modal window (`ui_window`) titled `FAN PRESETS`.
  - Independent target toggles: `Apply to: [✓ CPU] [✓ GPU]` (`c` / `g` hotkeys or clicks).
  - Four choices with curve previews:
    - `1: Quiet` (factory quiet curve)
    - `2: Balanced` (factory balanced curve)
    - `3: Performance` (factory performance curve)
    - `4: Active (Live)` (re-reads active curve from EC hardware)
  - Pressing `1..4` or `j`/`k` navigates and previews; `Enter` applies to
    targeted fan(s); `Esc`/`q` closes without changes.
  - Replaced legacy `Stk/Sil/Col/Ful` buttons in FAN view with `[ Presets (m) ]`.
- Live & parser implementation:
  - Pure RON parser `fan_ron_parse()` extracts factory curves from
    `/etc/asusd/fan_curves.ron`, falling back to built-in factory tables.
  - `hw_fan_hwmon_read_live()` reads active `pwm1`/`pwm2` sysfs nodes on demand.
  - Unit tests added in `tests/test_core.c` (`check_fan_presets`).
  - Integration flow added in `scripts/tui_smoke.py` (`flow_fan_presets`).
- Verified: warning-free `make` (-Wall -Wextra), `make test` green, `make install`
  installed to `~/.local/bin/ctron`.

## 2026-10-13 — M4 toggle: raise if in background, close only if active


- User feature request: pressing M4 when ctron is running in the background
  now brings it to the front instead of closing it; pressing M4 when it is
  already in the foreground / active closes it.
- Implemented in `scripts/ctron-toggle.sh`:
  - Collects ctron PID and ancestor PIDs (terminal, subshell).
  - KDE Plasma (KWin): loads a temporary KWin script via DBus to query
    `workspace.windowList()` and `workspace.activeWindow`. If found and active
    (not minimized), closes via `pkill -x ctron`. If found in background or
    minimized, activates it (`workspace.activeWindow = found`) and exits.
  - Hyprland: checks `hyprctl activewindow -j` pid; if active closes, else
    dispatches `hyprctl dispatch focuswindow pid:<pid>`.
  - Fallback: closes if window is untracked.
- Verified live with harness script on KDE Plasma 6 Wayland: background
  defocus -> `branch: raise` (window raised to front, ctron kept alive),
  subsequent press -> `branch: close` (clean quit).

## 2026-10-12 — LIGHT swatch dynamic placement + SECURITY.md

- LIGHT view swatch frame overflow fix: `draw_light` in `panel_workspace.c`
  used a fixed `x + 40` position for the custom hex swatch (`██████`),
  which stomped over the workspace panel's right border on narrow
  terminals (`w < 48`). The swatch now guards on row visibility
  (`LT_HEX < rows`), shifts dynamically to `x + w - 8` when narrow, and
  gracefully hides if horizontal space would overlap the hex value
  text (`sw_x < min_x`). Closes the final P4 audit item in NEXT.md.
- Added `SECURITY.md`: documents ctron's privilege model (`ut_priv_write`,
  `ryzenadj`, `nvidia-smi`), threat analysis, sudoers least-privilege
  recommendations, and a 3-phase hardening roadmap.
- Verified: `make` 0 warnings, `make test` green, `make tuitest` 10 flows
  green (including LIGHT view activation), `--doctor` and `--status` clean.

## 2026-10-11 — SIGTERM/SIGINT close the TUI cleanly (icon-close crash look)

- Field report after the single-instance guard: closing via the
  launcher icon (the toggle's `pkill` path) made the TUI "crash" —
  konsole held the window open with a termination notice. Cause:
  ctron died by signal with no teardown, so `konsole -e` treated it
  as a crash.
- The TUI now traps SIGTERM and SIGINT: the handler only raises a
  `sig_atomic_t` flag (async-signal-safe) and the main loop takes
  the NORMAL exit path on its next tick — worker joined, settings
  saved, notcurses stopped, singleton pidfile removed, exit 0. A
  signal is deliberately the force path: the staged-edits q-warning
  is bypassed, but preferences still save. The M4-toggle close now
  looks like a regular quit and konsole closes its window.
- Verified with a harness probe: SIGTERM → exit code 0 with the
  pidfile removed (was: signal death, stale pidfile), clean restart
  afterwards, normal `q` unaffected; tuitest green.

## 2026-10-11 — TUI single-instance guard (M4 stacking, root fix)

- User report: the double-press guard did not help — every extra M4
  press still opened another ctron. Diagnosis: the M4 service
  shortcut launches the command kglobalaccel CACHED at session
  start (component activation), not the current .desktop Exec —
  `gio launch` proved the desktop→toggle chain is live and correct
  (toggle ran, one ctron, close branch verified on a real window),
  so the key was still spawning the old `konsole -e ctron` directly,
  bypassing the toggle entirely. Service-shortcut Exec changes only
  take effect after the next re-login.
- Root fix, launcher-independent: the fullscreen TUI now claims a
  single-instance slot (`ut_singleton_enter` in util.c — pidfile in
  `$XDG_RUNTIME_DIR`, liveness verified via /proc/<pid>/comm so a
  stale file self-heals, fail-open when no runtime dir exists). A
  second TUI prints "ctron: TUI already running (pid N)", waits 2 s
  so the flash window stays readable, and exits 0; the running
  instance is untouched. `ut_singleton_leave()` removes the file on
  a clean quit (a SIGTERM-killed instance leaves a stale file that
  the next start overwrites). Headless CLI calls are unaffected.
- Verified: unit tests (decision helper + fork/live-child/claim/
  leave via `check_singleton`) and a two-Session harness run (A
  claims, B bows out with the message while A survives, A quits
  cleanly and the pidfile is gone). Residual, documented: the
  cached-command path can still race a very fast double-press
  inside konsole's spawn latency (~0.5 s) until the binding picks
  up the toggle Exec at the next re-login; the toggle's own stamp
  guard closes that window from the press itself.
- ctron-toggle also logs each invocation to
  `$XDG_RUNTIME_DIR/ctron-toggle.log` (branch taken, ctron count) —
  that is how "the key does not run the toggle" became a one-look
  diagnosis.

## 2026-10-11 — M4 toggle: double-press no longer stacks windows

- Follow-up to this morning's toggle: the user pressed M4 twice in
  quick succession and got TWO ctrons. The close branch was fine —
  the race is in the OPEN branch: the terminal needs up to ~1 s to
  produce the ctron process, and a second press inside that window
  found nothing in `pgrep` and opened another instance.
- Fix: the launch is now claimed atomically (noclobber stamp in
  `$XDG_RUNTIME_DIR`, falls back to /tmp). A second press while the
  claim is fresh (12 s) is a no-op; the launching toggle itself
  watches for the TUI to appear (up to 8 s) and clears the claim, so
  the guard is self-maintaining; a stale claim older than 12 s is
  stolen, so a killed toggle can never wedge the key permanently.
  The terminal is now started in the background instead of exec'd —
  the toggle briefly outlives it to manage the claim.
- Testing note (harness lesson repeated): the first scenario "passed"
  against a STALE installed copy — `make install` had not been re-run
  after the script rewrite, and the old exec-based binary duplicated
  right through the test without the assertions noticing (the close
  pkill kills duplicates too). Re-verified properly with an
  observable spawn log: double-press → exactly 1 spawn (old: 2),
  fresh claim → no spawn, stale claim (20 s) → stolen and proceeds,
  claim self-clears.

## 2026-10-11 — M4 key toggles ctron instead of stacking windows

- Field report (user, after re-login — the binding activates at
  session start as predicted): the M4/Armoury "Launch (3)" shortcut
  works, but every extra key press opened ANOTHER konsole/ctron
  window. Root cause: a desktop shortcut only spawns; it has no
  toggle sense.
- New `scripts/ctron-toggle.sh`, installed by `make install` to
  `~/.local/bin/ctron-toggle`: if a ctron process is running it is
  killed (the `konsole -e` window ends with its child), otherwise a
  terminal opens the TUI (`$TERMINAL` honoured, konsole fallback,
  clean error if neither exists). Short-lived headless CLI calls are
  gone before anyone can press the key twice, so `pgrep -x ctron`
  effectively targets the TUI.
- FA608PP wiring done live: `ctron.desktop` `Exec` now points at the
  toggle (absolute path), sycoca refreshed — the kglobalshortcutsrc
  binding is untouched (same desktop id, no re-login needed). Both
  branches verified: close branch with a dummy `ctron`-named process
  (killed, nothing opened), open branch accidentally proven when the
  first sandboxed test reaped its dummy between tool calls and the
  toggle opened a real konsole+ctron.
- README's M4 section and HARDWARE.md (EN + TR) now recommend
  `ctron-toggle` as the command and document why.

## 2026-10-10 — fan write verdict: the no-hwmon branch stops lying

- The last open P4 audit item. `ctrl_fan_write`'s decision tree had a
  blind spot: when the machine has no `asus_custom_fan_curve` hwmon
  (`vok == -1`, the sysfs block never ran), the branch logged plain
  "fan curves written", cleared `fan_staged` and returned success —
  regardless of the asusctl outcome. With asusctl absent or errored,
  NOTHING was written anywhere, yet the log claimed success and the
  CLI exited 0. Worse, the `fan_sysfs_dead` session latch makes this
  branch the steady state on machines whose sudoers do not cover the
  hwmon path, so the 10-08 honesty fix (which only covered the
  first-write `rc != 0` path) stopped applying after the latch
  tripped.
- New pure helper `fan_write_verdict(rc, vok_cpu, vok_gpu, a_ok)` →
  `{VERIFIED, VERIFY_FAIL, ASUSCTL, FAILED}` (exported in control.h,
  unit-tested as `check_fan_write_verdict`, 11-case matrix). The
  no-hwmon branch now splits three ways: asusctl ok → the honest
  "applied via asusctl/asusd (no fan hwmon; no read-back)" wording
  (staging cleared, success); asusctl errored → FAILED (staging kept
  so a retry survives, rc -1); no asusctl at all → "no write path"
  (staging kept, rc -1). Side effect (intended): fan CLI paths now
  exit non-zero when nothing landed. All other branches are
  behaviour-identical to before.
- The P4 list in NEXT.md is reconciled with reality: the daeboard
  recv timeout + both parser quirks, the pwm-enable check, `ui_row`'s
  byte-padding and `__pycache__` were already fixed by the 10-08
  line (11aae58 etc.); what genuinely remains is the LIGHT swatch
  x+40 overflow (cosmetic, narrow terminals).

## 2026-10-09 — apply performance: 20 s → 1 s (mode), 15 s → 7 s (snapshot)

- User report: mode switching took ~20 s. Profile: (1) cpufreq writes
  spawned one sudo tee per CPU (~32 × 0.12 s = 4 s per write) and a
  snapshot applies several (epp + freq rounds + re-assert); (2) the
  fan sysfs path (no sudoers rule for hwmon) burned ~34 doomed sudo
  spawns per apply; (3) the freq re-assert replayed even when the
  first write already failed (driver clamp — replay can't heal that).
- cpufreq_write_all now batches all present-cpu paths into ONE
  `sudo -n tee` invocation (paths built from hw->cpu_ids, matching
  the path-scoped sudoers rule exactly; single-valued per call).
  Fallback to per-node writes only if the batch fails. 4 s → 0.1 s.
- ctrl_fan_write gains a session latch: after the first total sysfs
  failure (no hwmon sudoers rule), subsequent writes skip straight
  to asusctl — ~4 s saved per apply on the FA608PP.
- profile_import's freq re-assert only replays steps that SUCCEEDED:
  a clamped first write can't be healed by retrying, it just burned
  another round of wait sleeps.
- The freq round loop caps at 3 full writes (storm guard against the
  async re-pin resetting ok_streak repeatedly).
- Fixed en route: `--mode <name>` was documented in the help text but
  never wired (the generic flag path called cmd_run("mode") which is
  not a key) — it now applies the named mode from modes.ini with a
  timing report ("mode 'turbo' applied (1.0 s)").
- Verified live: `--mode turbo` 1.0 s, `--mode quiet` 1.1 s (the
  remaining cost is kscreen-doctor + asusctl subprocess latency).
  Turbo snapshot from quiet: 7 s under the active driver clamp
  (honest-fail rounds; reboot clears); turbo→silent 4.9 s.

## 2026-10-09 — applies run on a background worker: live progress, no frozen TUI

- Snapshot/POWER/mode applies could block the UI thread for up to ~9 s
  (the quiet→performance CPU-ceiling window). ctron now has its first
  worker thread: the WRITE phase of those applies runs off-thread,
  the main loop keeps rendering, and the telemetry footer becomes a
  live status line — spinner + phase text ("CPU ceiling: waiting for
  the 5386 MHz window (round 4/8)") + percent + elapsed seconds +
  "reads paused · q waits" honesty notes. The new ut_progress channel
  is published by the write layer (freq rounds, fan write, the freq
  re-assert) and is silent on the CLI.
- Discipline kept: the worker touches hw + the write layer only — no
  notcurses, no g_ui mutation; hw_refresh_fast is paused while busy
  (single writer); everything user-facing (completion logs, toasts,
  pw_sync_from_hw, ctl_capture_mode) runs in the finalize step on the
  UI thread strictly AFTER apply_busy drops — which also preserves
  the harness key-ordering. ut_log/ut_log_get are mutex-protected
  (the worker logs while the footer reads); apply_busy/quit_pending
  are C11 atomics; q is never a kill: first q asks, the loop exits
  only after the worker joins. FAN Write stays synchronous by design
  (short, and its flow timing depends on it).
- Footer-newest lesson (found the hard way): the footer shows only
  the newest ring entry, so a worker log immediately followed by a
  finalize log was buried within one frame — each apply kind now
  emits exactly ONE completion line ("applied snapshot 'x' · 5.2 s",
  "power apply: ok · 1.4 s", "mode 'drift' applied · 0.8 s").
- Tests: ut_progress roundtrip + a 2-thread x200 ut_log hammer
  (ring intact, entries well-formed); tuitest 10 flows green with
  async-aware expectations ("reads paused" proves the spawn, the
  single completion line proves the finish).

## 2026-10-09 — quiet→performance freq transitions: the 2401 trap, three layers deep

- User report: "quiet'tayken 2401 limiti var, geçiş yapılmıyor" — a
  snapshot with "profile performance, freq 5386" applied from Quiet
  ended at 2401 every time. Root cause was three stacked:
  1. ctron's OWN clamp: hw_init snapshots cpuinfo while still Quiet
     (2401) and ctrl_set_cpu_max_mhz pre-clamped the request to that
     snapshot — writing and "verifying" 2401 while reporting success.
     The snapshot is now raise-only (cpu_limits_sweep never lowers
     cpu_mhz_max) and the writers no longer pre-clamp; the honest
     limits are the live-ceiling wait plus the hardware itself.
  2. the transition window: Quiet pins the cpuinfo ceiling at the
     base clock; after the profile switch amd-pstate re-opens it in
     ~2-3 s but never raises scaling_max on its own — a write landing
     inside the window is clamped and never re-applied. The write now
     waits for the live cpuinfo ceiling to allow the value.
  3. stability: a value that verified once could still be re-pinned
     moments later; success now requires verification on two
     consecutive rounds (0.9 s apart), and profile_import re-asserts
     the freq lines once more after every other step has settled.
- Live-proven both directions: quiet(2401) → turbo apply → 5386
  stable at +4 s and +7 s; back to silent → 2401/quiet with its exact
  curve. Apply cost in the quiet→performance case is ~5-6 s (the
  window is real); same-window standalone writes are unchanged.

## 2026-10-09 — snapshot apply: fan curves never applied (dead parser branch)

- User field report: switching between two snapshots changed nothing —
  fan curve and CPU clock limit. Root cause #1 (fan): profile_import's
  fan-curve branch matched the bare key "fan-curve", but the '=' split
  of the exported line "fan-curve cpu = temps pwms" puts the fan side
  IN THE KEY ("fan-curve cpu") — the branch was dead code since
  inception, every fan-curve step fell through to cmd_run as "unknown
  key", and snapshot curves silently never applied (the curves that
  reached the EC historically came from settings.ini + Write, not from
  snapshots). Fixed: the branch now matches the "fan-curve <side>" key
  and reads the two csv lists from the value. End-to-end proven live:
  applying the two user snapshots flips the EC table between their
  exact curves (turbo 40..90:255 / silent 38..105) with rc=0.
- Root cause #2 (freq): quiet->performance transitions make amd-pstate
  re-open the ceiling over ~3 s, so the freq write immediately after a
  profile-switch step could verify against the still-clamped value.
  ctrl_set_cpu_max_mhz now retries the read-back briefly (3 attempts,
  0.8 s apart) before declaring a driver clamp. Note: the ACQUIRED
  full clamp (all cores pinned to nominal 2401 after profile churn,
  cpuinfo still 5386) is the known kernel re-baseline — writes
  verify, then the driver re-pins; only a reboot clears it (documented
  in HANDOFF 10-02; reproduced live in this session).
- Tests: import roundtrip from a hand-written snapshot (curves + fan
  flags + fan_staged; harmless keys only so the unit test never
  touches hardware).

## 2026-10-09 — SNAPSHOTS: the PROFILES panel renewed (rename, preview, diff, notes, folder-ready)

- The panel is now SNAPSHOTS — "profile" already meant three other
  things (the platform profile row, the asusd auto-profile rows, the
  --profile flag). CLI: `ctron snapshot list|apply|export|delete` with
  `profile` kept as an alias; `.ctr` files and the profiles/ directory
  are unchanged (old snapshots keep working). Topbar chip 1:snapshots,
  HELP updated.
- Save preview: typing a name shows exactly what export will capture
  ("Performance · epp … · fans on +curves") — the in-memory-state
  surprise from the fan incident is visible before saving.
- Live diff: with a snapshot selected, the footer lists which tracked
  fields applying it would change ("Δ profile · hz · curve-cpu") or
  "≡ live"; metadata is parsed once per selection change, the diff
  itself is draw-time integer compares (no extra polls).
- Every export now stamps "# saved: YYYY-MM-DD HH:MM"; `c` edits a
  one-line "# note:" shown with the date in the summary.
- `r` renames in place (the delete-and-resave dance is gone).
- Folder-ready names: "oyun/turbo" saves to profiles/oyun/turbo.ctr
  ('/' groups one level deep, "."/".." segments are rejected, lists
  show "group/name"); a full grouping UI stays future work.
- Fixed en route (found by the new flow_snapshots harness test): the
  global s/ESC settings shortcut swallowed 's' whenever the panel was
  focused — keyboard saving had NEVER worked, only the Save button.
  The focused panel now owns 's'.
- Tests: check_snapshots (sanitize incl. traversal rejection, meta
  parse, diff equal/changed/curve, note roundtrip, rename semantics);
  tui_smoke flow_snapshots covers save→rename→note→delete against an
  isolated config (10 flows total).

## 2026-10-09 — fan graph live-temp marker sat ~20 °C right

- The live `▲` marker on the fan graph's temperature axis plotted the
  ABSOLUTE temperature into the 20-105 axis instead of subtracting
  FAN_TMIN like every other element (points, grid lines, labels): at
  73 °C it rendered at ~86 % of the width (the 90 °C area — user
  field report), and at ≥ 90 °C it fell off the graph entirely. The
  bug predates the value guides; it only became readable once the
  grid labels existed. One-line fix; positions now agree with the
  labels.

## 2026-10-08 — fan curves: honest asusctl apply path + graph value guides

- Write used to report FAILED ("needs root") whenever the hwmon sysfs
  nodes were unwritable — while the asusctl persistence calls behind
  it silently succeeded, storing AND applying the curves through
  asusd. That is exactly how the user's settings.ini curve reached the
  EC without them ever pressing a successful Write (the 100 %-fan
  mystery). ctrl_fan_write now tracks the asusctl results: sysfs
  verified → as before; sysfs unwritable but asusctl ok → success with
  an honest log ("applied via asusctl/asusd … no read-back — verify by
  ear/temps"); both failing → FAILED. Live-verified: `--fan-write`
  exits 0 on this box (no hwmon sudoers rule needed).
- FAN editor: `i` toggles dotted value guides on the graph — vertical
  every 10 °C (labels 40/60/80/100 on the temp axis) and horizontal at
  ~25/50/75 % PWM (64/128/192 labels on the left), so the °C→PWM
  mapping is readable off the curve instead of guessed from the chips.
  HELP documents it; `flow_fan_grid` (tui_smoke, 9th flow) covers
  toggle on/off.

## 2026-10-08 — tuitest fan Write click no longer touches the real config

- Incident: after a day of heavy `make tuitest` runs, switching to the
  Performance profile slammed the CPU fan to 100 %. Diagnosis: the
  EC's custom curve ramps to full PWM at 71 °C, the live profile
  pushes package temps past that, and the curve active on the EC was
  byte-identical to the user's settings.ini curve — the harness's
  "real Write click" flow had been pushing the user's staged ini
  curves onto the EC and into asusd's per-profile store on every run.
  The flow's "no-op re-write of the live curves" premise died with
  fan_staged (2026-10-03): settings.ini curves are staged at load, so
  the click wrote THEM, not the EC's values.
- Fix: flow_fan_buttons runs against an isolated CTRON_CONFIG with no
  settings.ini — the in-memory curves then equal the EC read at init
  and the Write click re-writes identical values (a true no-op). No
  ctron code changes; the curve values on the EC are the user's own
  tuning, untouched.

## 2026-10-08 — ut_exec SIGPIPE race: printing children failed as fake errors

- Root cause of the first ryzenadj round reporting FAILED while the
  journal showed every command running as root: `exec_run` skipped
  reading the pipe entirely when the caller passed no output buffer,
  so a child that PRINTS (ryzenadj's "Successfully set ...") raced our
  pclose for the pipe — its write hit a closed read end and it died on
  SIGPIPE (exit 141), turning a successful write into a fake failure.
  Latent forever because previous children (asusctl, tee, kscreen)
  printed nothing. The pipe is now always drained. Proven live:
  `--ppt 30,40,50` rc=0 with exactly one sudo journal entry.

## 2026-10-08 — PPT watt limits actually apply: armoury probe + ryzenadj write path

- Field research (OCCT): setting 30/40/50 W from ctron did nothing —
  package power hit 80 W. Diagnosis: the legacy nb-wmi nodes accept
  writes (readback shows them) but the SMU ignores them; the official
  asus-armoury attributes report ppt_* (and nv_*) "unavailable"
  (ENODEV) on FA608PP — firmware owns the power budget. `ryzenadj`
  (SMU mailbox, /dev/mem as root — verified working, limits pinned at
  40 W under load) is the only effective interface here.
- Landed: `hw->ppt_mode` probe at init (armoury current_value →
  ryzenadj on PATH → legacy nb-wmi; no sudo at init), a common
  `ppt_write()` used by set/off/restore, and the pure, unit-tested
  `ctrl_ppt_ryzen_cmd()` builder — fixed flag order, integers only,
  name-based mapping SPL→STAPM(-a), SPPT→SLOW(-c), FPPT→FAST(-b),
  W→mW. The ryzenadj invocation tries /usr/bin → /usr/sbin → bare
  name: sudoers matches the invoked path LITERALLY (merged-usr
  symlink is invisible to it), so the rule must name the path ctron
  calls. SECURITY INVARIANT: ctron never passes any other ryzenadj
  flag (no --tctl-temp) — documented in the header.
- Honesty: Dragon Range has no SMU read-back (monitoring table
  unsupported) — the log says "via ryzenadj (no read-back on this
  platform — re-apply after reboot/profile change)", never "verified".
  SMU limits are volatile: reboot/suspend/profile changes can clear
  them. `--doctor` shows the resolved interface ("ppt iface"). The
  ctrl_ppt_limits filename bug is fixed too (min/max → min_value/
  max_value — the old names never existed, so real firmware limits
  were never read).
- Known risk documented (not changed): NOPASSWD ryzenadj is broad
  (root can also raise --tctl-temp); arg-constrained sudoers rules are
  weak (* crosses argument boundaries) — HARDWARE.md carries an
  optional root-owned wrapper recipe for hardening. "ppt off" now has
  a real effect on this machine (was a silent no-op).

## 2026-10-08 — fan enable, and settings that did not do what they said

- Custom fan curves use the kernel values: write `1` to apply the curve, `2` for factory auto. A read of `2` is off. `0` was rejected, so the old "off" write did nothing and "on" selected automatic. The enable write is checked, and a verified curve includes the enable read-back. On the FA507NVR both curves read `2`, so `--status` now prints `(off)`.
- Settings "no sudo" skips the `sudo -n tee` fallback. "GPU temp (nvidia-smi)" is honoured, and the query is skipped while the NVIDIA display function's `runtime_status` is `suspended` or `suspending`.
- `--gpu-clock` uses `command -v nvidia-smi` instead of `/usr/sbin/nvidia-smi`.
- Mode steps split on commas that start a command, so `ppt 45,55,55` and fan-curve csv survive.
- The daeboard socket exchange gives up after 2 seconds. A `ctron` line must be that word; a line without `=` no longer hides a later assignment.
- Selected rows pad by display cells, so the `▸` marker does not shift the value column.
- `scripts/__pycache__/` is no longer tracked.
- CLI matches the TUI command set: `--gpu-clock`, `--ac-profile`, `--battery-profile`, `--freq core`, `--freq ccd`, and `--fan cpu|gpu on|off` are documented. `--status` shows a GPU-clock line and how many threads sit below the ceiling.

## 2026-10-07 — POWER: firmware-behaviour badge for the moving CPU ceiling

- The freq-watch experiments proved the CPU boost ceiling is
  firmware-owned under load (Dynamic Boost power shift + shared
  heatsink; sawtooth 4.3-5.4 GHz with the package at the ~95 °C soft
  limit) — yet the moving "CPU clock limit" row twice raised a
  "is this a bug?" report. The row now gets a `· firmware` badge
  whenever the package temp is >= 90 °C (hysteresis: clears <= 88),
  with a single rising-edge log line ("oscillation expected, not a
  bug"). Zero extra reads: the badge reuses cpu_temp, already sampled
  every poll. HELP view documents the behaviour; no CLI change (a
  one-shot --status badge would misread a momentary temp).

## 2026-10-06 — docs: CCD disable vs CPU boost ceilings (BIOS works, runtime does not)

- Three-run freq-watch comparison on the FA608PP (GPU 114 W): 16 cores
  sawtooth 4.3-5.4 GHz under load; CCD2 offlined via /sys clamps to
  the nominal 2500 MHz at the 95 °C soft limit — the firmware keeps
  budgeting power for 16 cores regardless of runtime offlining; CCD2
  disabled in BIOS re-configures the SMU for the true 8-core topology
  — CPU package power drops (~80 -> 45-60 W observed) and the ceiling
  holds 5386 MHz ~80-85 % of the time with shallow, fast-recovering
  dips. Recorded in HARDWARE.md (EN + TR): runtime CPU offlining is
  not a substitute for a BIOS CCD change (reboot required). No code
  changes.

## 2026-10-06 — util: failed writes close the fd; priv write rejects quotes

- `ut_write_file` chained `fwrite && fflush && fclose` in one
  expression, so a failed write or flush short-circuited past `fclose`
  and leaked the fd — P4 item 1's leak (an fopen failure never leaks:
  it returns before any fd exists; the leak bit when the open
  succeeded but the write or flush failed, e.g. a full or erroring
  filesystem). `fclose` now runs
  unconditionally and its result ANDs into the outcome.
- `ut_priv_write`'s sudo fallback built a single-quoted shell command
  from the raw value/path. Callers only ever pass fixed sysfs paths
  and plain numeric/text values, but a `'` in either is now rejected
  with a clean -1 BEFORE any exec — defense in depth, no behaviour
  change for existing callers.
- Test `check_util_write` in test_core: write roundtrip, the /dev/full
  leak path (/proc/self/fd counted across 50 failed writes), and both
  quote rejections (reached through an unwritable path, so the guard
  fires before the fallback and nothing is exec'd). Mutation-proven:
  with the old code the leak check fails with ~50 leaked fds.
  Closes P4 item 1 (NEXT.md).

## 2026-10-04 — hz validation ceiling 500 → 2000

- The 500 Hz ceiling in cmd_hz already rejected current hardware (540
  Hz panels exist) and — worse — ran after the `hz max` computation,
  so even `--hz max` would fail on such a panel. The TUI Refresh row
  was never affected (it cycles real compositor modes). The band is
  only a garbage filter — ctrl_set_hz snaps to the nearest supported
  mode anyway — so the ceiling is now 2000, far above any announced
  panel. Lower bound unchanged (30). Boundary-tested: 15 and 6000
  rejected, no hardware write on rejection.

## 2026-10-04 — GPU lock verify survives load; POWER stops writing unstaged CPU limits

- Field report: the GPU clock lock FAILED while the machine was under
  load, and POWER's Apply tried to write the CPU clock limit without
  the user ever touching that row (it kept moving during an OCCT run).
- GPU verify race fixed: the first sampling round could still read the
  PRE-lock boost clock (2500 > 1400) and fail an applied lock. The
  verifier now settles 250 ms first and takes up to three rounds — one
  stale high sample under load no longer fails an applied lock, a
  consistent overshoot still does. The CLI error now names the likely
  cause (sudoers rule for /usr/sbin/nvidia-smi) instead of a bare
  "command failed".
- POWER's staged-apply semantics corrected for the CPU clock limit:
  the write only fires when the user actually staged it (PW_T_CPUFREQ).
  The live limit legitimately oscillates on its own under load
  (amd-pstate renegotiates per-core ceilings — kernel behaviour, not a
  ctron bug), and mid-load writes then fail their own honest
  verification (the driver pins the ceilings). The row still shows the
  live drift; Apply just no longer fights the kernel over an untouched
  field.

## 2026-10-04 — GPU clock lock (-lgc): the laptop watt lever that works

- Field research (user): laptop GPU power limits are locked
  (`power.limit` reads `[N/A]`) and the existing NV rows (dynamic
  boost / temp target via nb-wmi) apply without any observable effect.
  The working lever is the core-clock lock: `nvidia-smi -lgc
  <mhz>,<mhz>`, reset via `-rgc`.
- Probes (RTX 5070 Laptop, driver 615.71.09): `-lgc` and `-rgc` both
  rc=0 ("All done"); a 1400 lock pins the core clock at 1395 MHz
  (driver clock grid) even with a light load running. The
  applications-clocks QUERIES are deprecated on this driver and the
  event-reason mask ignores the lock — there is no lock flag to read
  back. Rule 4 adapted: after a lock, sample
  `clocks.current.graphics` three times and require every sample at or
  below the lock (+2 MHz grid slack); the reset is rc-verified only,
  logged honestly as such.
- POWER view: the two dead NV rows are replaced by one **"GPU clock
  limit"** row — h/l walks reset + 800..2100 presets, `t` stages any
  exact value; wired into the staged-apply machinery (PW_T_GPUCLOCK).
  `gpu_mhz_max` (3090 here) is read once at init. New `gpu-clock
  <mhz|reset>` command key → CLI `--gpu-clock`, modes and .ctr
  profiles; `MS_GPUCLOCK` drift bit. `--nv-boost` / `--nv-temp` keys
  stay (FA507NVR may behave differently) but gained read-back
  verification: a mismatch now logs "written (read-back mismatch —
  may be stale or platform-owned)" instead of a bare ok.
- Privilege note: the lock needs a sudoers rule for the REAL path
  (`/usr/sbin/nvidia-smi`); without it the write fails cleanly
  (exit 1, never a prompt). The persistence-mode warning nvidia-smi
  prints is cosmetic on laptops — the driver stays loaded.
- Unit tests: `ctrl_gpu_clock_ok` sample verification, gpu-clock mask
  parsing and drift; tuitest 8 flows green.

## 2026-10-03 — CONTROLS Mode row shows the applied mode and its drift

- The Mode row used to show only the rotated picker candidate. It now
  tracks the applied mode: `-` before any apply, `name` while the
  system still matches the apply-time snapshot, `name*` once a tracked
  field drifted. The "live → candidate" arrow (`- → name`,
  `name* → other`) appears only once the user actually touches the
  picker — rotating with h/l or clicking the row — so startup and
  untouched rows stay bare (same "live → staged" language the POWER
  view already uses, without the unprompted noise).
- Session-only snapshot (no persistence), per the resource-minimal
  request: `mode_touch_mask` parses which set-point fields the steps
  touch (profile/epp/ppt/hz/battery/kbd/cpu-boost/panel-od/nv-boost/
  nv-temp/freq/fan curves; aura, the asusd auto rows and flush-style
  commands are untracked), `mode_snapshot` captures the readable ones —
  unreadable fields are dropped from the mask, what cannot be read
  cannot be tracked — and the draw-time check compares a dozen ints.
  Stale live reads (ppt 0, hz 0) never count as drift; an exact manual
  restore clears the star without re-applying. Cost: a handful of
  integer compares while drawing one panel, no extra polls or sysfs
  reads. Side effect worth having: when asusd's takeover re-applies its
  own profile, the row flips to `name*` on its own.
- Pure helpers live in modes.c (unit-tested: mask parsing, snapshot
  dropping, drift counting) and `flow_mode_drift` joins tui_smoke as
  the 8th flow (isolated config with a one-step mode; the real profile
  is saved before and restored after).

## 2026-10-03 — docs: KDE M4/Armoury Crate key binding

- The TUF's M4 key emits scancode `0x26` → `KEY_PROG3` → XKB
  `XF86Launch3`; KDE's shortcut picker shows it as **"Launch (3)"**.
  Verified on FA608PP with evtest (user capture) + kglobalaccel D-Bus;
  the binding is registered and persisted in `kglobalshortcutsrc`
  (final activation check pending a re-login).
- HARDWARE.md gained an EN + TR notes bullet and README a short
  "KDE: Armoury Crate (M4) key" section: the GUI binding steps, the
  evtest diagnosis path (`/dev/input/event11`, "Asus WMI hotkeys"),
  the XKB keycode = Linux input code **+ 8** trap, and the kwin-grab
  behaviour (bound keys never reach `show_key`-style tools). No code
  changes.

## 2026-10-03 — view hotkeys 5..8: keyboard access to POWER/LIGHT/HELP

- User report: 'f' opened the fan view but POWER, LIGHT and HELP were
  unreachable from the keyboard. Measured with the pty harness: the
  letters only worked from the workspace focus AND with shift (p/l are
  bound by the fan editor — pwm field / nudge — and by the power/light
  rows), from other panels they did nothing, and after an accidental
  'p' the typing field silently swallowed the next key (even '?').
- Fix: the digits **5 fan · 6 power · 7 light · 8 help** now switch the
  workspace view from ANY panel focus, continuing the 1..4 panel
  hotkey story; no view binds digits, overlays and typing fields keep
  precedence. The workspace tab labels show the keys (" 6:POWER "),
  the help view documents them, and the dead uppercase 'P' binding in
  the fan editor was removed ('p' still types pwm; 'P' now reliably
  means POWER).
- 'f'/'p'/'l' (lowercase where the view has not taken them) and
  shift+letters keep working as aliases. Regression: `flow_view_hotkeys`
  drives 6→5→7→8 from the default controls focus.

## 2026-10-03 — fan staging: live refresh no longer drops un-written curves

- Editing a fan curve (editor nudges/add/delete, mode/profile fan-curve
  steps, profile import, settings.ini load) now raises `hw->fan_staged`;
  `fan_hwmon_read` skips the curve refill while it is set (the pwm*_enable
  switches still track the hardware). Before, any `hw_refresh_live` —
  switching to POWER, applying a mode, an Apply pass — silently
  overwrote the in-memory curve from hwmon and the un-written edit was
  gone.
- `ctrl_fan_write` clears the flag after a verified write; on VERIFY
  FAILED the staging is kept so a retry 'w' is not clobbered in between.
- Regression flow `flow_fan_staging` in `scripts/tui_smoke.py` (runs
  against an isolated CTRON_CONFIG; the staged edit is never written to
  the EC). Proven to bite by mutation: with the guard removed the flow
  fails with "staged edit lost: chip reads (39, 22), staged (40, 22)".
  Notcurses emits diffs, so the flow keys on changed-digit runs and the
  full chip only after the full FAN repaint.

## 2026-10-03 — asusd.ron fallback reads the whole file

- `ut_read_file` reads the FIRST line only (fgets). The daemon-unreachable
  fallback in `hw_asusd_auto_read` parsed the multi-line asusd.ron with
  it — the file's first line is just "(", so every takeover parsed as
  off. New `ut_read_file_all()` (whole file, trailing newlines trimmed)
  feeds the extracted pure parser `hw_asusd_ron_parse` (unit-tested:
  armed/off/unknown/absent keys, NULL body); daemon path unchanged.

## 2026-10-03 — kbd sysfs fallback writes the numeric level

- `ctrl_set_kbd`'s direct-sysfs fallback wrote the name string
  ("low"/"med") into `/sys/class/leds/asus::kbd_backlight/brightness`,
  which takes an integer — the write always failed EINVAL, so without
  asusctl (or with write_pref=sysfs) keyboard brightness could never be
  set. It now writes the numeric level, matching `read_kbd`.

## 2026-10-03 — mode add: clean error on a full table

- `ctron mode add` appended at `modes[MODES_MAX]` when the table was
  full — an out-of-bounds stack write (the TUI's mode editor already
  guarded this). The 28th add on a fresh config (5 seeded modes + 27)
  now fails with "mode table full (max 32)", exit 1.

## 2026-10-03 — mode editor steps hold the full MODE_STEPS_MAX

- `tinput_t.buf` was 80 bytes, so editing a mode in the settings overlay
  silently truncated its steps to 79 characters and saved the truncated
  value back to modes.ini (MODE_STEPS_MAX is 220). The input buffer now
  holds the full field; display lines got snprintf precisions to match.

## 2026-10-03 — debug leftovers removed

- The TUI start-up printed a `DBGHZ count=...` line to stderr and ran
  `hw_refresh_live` twice (the debug block sat between the calls) — one
  refresh now. Also removed: the unreachable /tmp/kde-dump.txt block
  after `return 0` in `kde_query`, and the dead `sel`/`dot` remnants in
  the profiles panel.

## 2026-10-03 — warning-free build restored on gcc 16

- gcc 16.2.1 emits five `-Wformat-truncation` warnings in
  editor_daeboard.c (all safe-at-runtime snprintf truncations, but the
  warning-free rule is a rule) and `_GNU_SOURCE redefined` in
  test_core.c (the Makefile already passes -D_GNU_SOURCE). Fixed with
  precision specifiers / an include guard; semantics unchanged. The
  NixOS gcc on FA507NVR should be silent again too.

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

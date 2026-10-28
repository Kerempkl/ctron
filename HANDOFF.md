# Ctron Handoff

Latest session — **2026-10-28 (7th round, security audit → v0.8.1)**
**GLM / Kerempkl / FA608PP**: user asked for a full vulnerability hunt
over the whole tree ("tüm kodu detaylıca oku") — every source file read,
`gcc -fanalyzer` pass, dangerous-pattern greps. ONE real vulnerability
found, ASan-proven and FIXED in the same breath: `profile_set_note`'s
`off += snprintf(...)` ran past `out[4096]` on any >4 KB `.ctr` (next
size argument wrapped size_t → out-of-bounds stack WRITE); fixed with a
hard-clamped `note_append()`, >12 KB regression test in test_core, full
suite green under an ASan build. Also: ctron-toggle's KWin `.js` now
created noclobber (symlink-refusal proven with a probe), SECURITY.md
gained the daeboard install.sh never-add-to-sudoers warning + §2.4 fix
record + Phase 2.2 closure, `make HARDEN=1` opts into
stack-protector/fortify, `cut_step` memcpy→memmove. Everything else
audited CLEAN against the doc claims (argv-only privileged writes,
width-bounded sscanf, literal log formats, sanitize blocks traversal;
the 5 analyzer warnings are FPs). **v0.8.1** cut (VERSION+badge+flake
one commit). 0 warnings on normal and HARDEN builds, `make test`,
`make tuitest` (note flow exercised), `--status` live, `make install`
fresh. Push/tag on the user's side. Same day **review pass 2**: extra-
warnings build (-Wshadow &co) cleaned the daeboard `step`-param
shadows + pointer-before-guard, `detail_ram` `n` shadow, hw.h dup
include; **mode names now validated at both creation sites**
(`mode_name_ok`: no empty/`[`/`]`/newline — such names round-tripped
broken through modes.ini; 6 unit cases + live CLI check); first audit
of tui_smoke.py/tui_screenshot.py (clean); extra-warnings pass now
exactly 1 deliberate format-nonliteral. 0 warnings, make test, full
tuitest, install — rides the next version cut (no bump). Before that **2026-10-28 (6th round, professional telemetry)**
**GLM / Kerempkl / FA608PP**: research-driven professional pass —
web research over btop/bottom/glances/htop/zenith/ttyplot, user took
ALL four items: glances-style threshold zone colouring (value+curve
shout on 70/85/95 °C temp, 70/90/98 % util, low charge), btop value
column alignment (label/sparkline/right-aligned value), btop NET
dual-series charts (read/write, rx/tx, cpu/gpu fans in ONE chart,
shared union scale, sidx2/label2 on chartdef_t), bottom window-
average dotted reference line, digit hints in box titles (CPU 1 …
POWER 8). Caught+fixed in review: pair sentinel 0 collided with
TS_CPU_TEMP=0 → "(null)" captions; now -1. Before that **2026-10-28 (5th round, "1 2 3 ü yapalım")**
**GLM / Kerempkl / FA608PP**: the user picked three items and all
three landed — **FAN telemetry section** (8th: the RPM rings finally
chart; grid is a full 4×2, digits 1-8), **ctron-toggle TERM→KILL
escalation** (the 10-27 incident turned into code: a wedged TUI can
no longer wedge the key; proven with a fake comm=ctron process both
ways), **HARDWARE.md sensor map** (EN+TR, every 't' source with its
honest limits); **v0.8.0** cut. Item 4 (nvidia-smi load-latency
measurement) deliberately deferred by the user — see the session
section below. Before that **2026-10-28 (4th round, telemetry charts)**
**GLM / Kerempkl / FA608PP**: wash-fill charts + self-descriptions —
the area is a solid background color wash, the curve an eighth-block
bright edge, slice-peak columns, detail cap 12 rows, newest sample
marked with a bright ●, and every series carries a desc (detail:
under each chart; grid: caption over the mini) so graphs explain
what they measure. Install fresh.
Process note: one assistant turn in this round degenerated into
dozens of fake tool-call blocks — the honest recovery was to re-run
the unverified steps (full tuitest + install) fresh and only then
claim them; everything claimed below is real output. Before that **2026-10-27 (3rd round)**
**GLM / Kerempkl / FA608PP**: telemetry chart types matched to their
context after the user's "not useful where they're used" verdict —
detail pages draw two-tone solid block area charts (█ fill dim +
eighth-block top edge bright: crisp line over quiet area, rows*8
levels, no braille texture, no seams) and the grid boxes dropped
mini charts for "label value sparkline" rows (60 s shape beside the
live value); merged ON TOP of the 2nd round's sensor-fill state
(NET section, 7 sections, 1-7 digits). Before that **2026-10-27 (2nd round, "do everything doable")**
**GLM / Kerempkl / FA608PP**: the sensor shortlist is now EXHAUSTED
for this machine — ④⑤⑥ shipped, ⑦ proven NO-GO (Dragon Range
gpu_metrics CPU blocks are permanently 0xFF; only the gfx-rail slice
is live and now displayed), ⑧ parked (smartctl needs root beyond the
sudoers ruleset). Also: Tccd1/2, RAM limits+alarm (kernel flag, caught
live at 57/66 °C over 55), NVMe hottest-sensor, iGPU busy/DPM;
AGENTS.md map synced; the FAZ 2 CCD "manual" verification closed by
automated screen-dump vs sysfs proof; **v0.7.0** cut (see below).
Before that **2026-10-27** **GLM / Kerempkl / FA608PP**
(recommended-steps round: **v0.5.0 tag pushed to origin** — first
agent-side push, explicitly approved; **LICENSE (MIT)** added per the
user's "nothing to change, low cost" pick + README badge; telemetry
sensor series items ①+③ shipped as **v0.6.0** — a NET section
(per-interface rx/tx over the rings) and GPU pstate/throttle-reason
reads from the extended nvidia-smi query; tui-telem.png re-shot;
see below). Before that **2026-10-26** **GLM / Kerempkl / FA608PP**
(telemetry detail pages: the 't' fullscreen gained a j/k grid cursor
and Enter/1-6 section detail pages with full-width multi-row charts;
two cheap reads — iGPU voltages + battery health; **v0.5.0** cut —
see below). Before that **2026-10-26** **GLM / Kerempkl / FA608PP**
(security follow-up pass: the last privileged shell paths closed —
cpufreq/PPT exec argv vectors, daeboard `system()` gone; user's picks
1+2+3 of the review, items 4/5 deferred — see below). Before that
**2026-10-26** **GLM / Kerempkl / FA608PP**
(small docs pass: machine-local setup notes genericized across the
guides — see below). Before that **2026-10-26** **GLM / Kerempkl /
FA608PP** (performance audit → FAZ 4 planned and LANDED: items 13/14/15 in
three commits, key-burst CPU 8.7% → 3.3% — see below). Before that
**2026-10-26** **GLM / Kerempkl / FA608PP**
(comprehensive performance/optimization audit, measured → NEXT.md
FAZ 4; no code changes — see below). Before that
**2026-10-26** **GLM / Kerempkl / FA608PP** (roadmap
execution: FAZ 1 provenance fix + FAZ 3.10/3.8/3.9 security chain +
FAZ 3.11 sudoers doc + FAZ 3.12 wrapper probe + v0.3.1 + fullscreen
telemetry 't' + v0.4.0 — see
below). Before that
**2026-10-25** **GLM / Kerempkl / FA608PP** (README
redesign: GitHub landing page + real TUI screenshots — see below;
same-day earlier: roadmap rewrite + v0.3.0 release cut). Before that
**2026-10-18** **GLM / Kerempkl / FA608PP** (asusd
factory-default probe — findings below). Before that **2026-10-17**
**GLM / FA608PP** (fan preset honesty
package — Balanced table sync + source visibility, see below). Before
that **2026-10-16** **GLM / FA608PP** (review of the
10-12..14 parallel line + test-environment guard fixes — see below).
Before that **2026-10-14** **Antigravity / Kerempkl / FA608PP** (Fan
curve startup honesty & FAN PRESETS modal overlay — see below). Before that
**2026-10-13** **Antigravity / Kerempkl / FA608PP** (M4 toggle background
raise vs foreground close — see below). Before that **2026-10-12**
**Antigravity / Kerempkl / FA608PP** (LIGHT swatch overflow
fix + SECURITY.md — see below). Before that **2026-10-11** **GLM /

Kerempkl / FA608PP** (M4 key toggle — see below). Before that
**2026-10-10** **GLM / FA608PP** (fan write verdict —
the last open P4 item, see below). Before that **2026-10-09**
**Kerempkl + GLM / FA608PP** (apply worker + perf round, 6306ce3 +
ffabd19) and **2026-10-08** **Grok / FA507NVR**: audit follow-up,
rebased onto `07d19c2` (PPT/ryzenadj). Fan enable is `1` on / `2` auto;
`--status` shows both curves `(off)` while `pwmN_enable` is 2. CLI
matches the TUI command set (`--freq ccd`, `--fan cpu|gpu on|off`,
`--gpu-clock`, asusd auto profiles). `make` is warning-free and
`make test` passes. Not done live: writing enable `1`, a hung daeboard
socket, and `--gpu-clock`. The FA608PP sudoers rule was not checked. Before that **2026-10-06** **GLM / FA608PP** (util write-path
hardening — P4 item 1, see below). Before that **2026-10-04** **GLM /
FA608PP** (GPU clock lock -lgc + verify-race/unstaged-CPU fixes, hz
ceiling) and **2026-10-03** **GLM / FA608PP** (full code audit +
P1–P3 fixes). Before that **2026-10-02** **Kerempkl + GLM /
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

- Same-day follow-up 2 (user hit it AGAIN): the guard changed
  nothing — every M4 press still stacked a window. Root cause found
  by instrumenting the toggle with an invocation log + `gio launch`:
  the desktop→Exec→toggle chain is live and correct (toggle ran,
  single ctron, close verified on a real window), so kglobalaccel's
  service shortcut must be launching the command cached at SESSION
  START (component activation), bypassing the .desktop entirely.
  **Lesson: a service-shortcut Exec change only takes effect after
  re-login — the user was pressing the pre-toggle `konsole -e
  ctron` all along.**
- Landed the launcher-independent root fix: fullscreen-TUI
  single-instance guard (`ut_singleton_enter/leave` + pure
  `ut_singleton_stale`, unit-tested incl. forked live child; wired
  before hw_init in main.c). Second TUI bows out with a readable
  message; SIGTERM-killed instances leave a self-healing stale
  pidfile. Integration-proven with two harness Sessions (A claims /
  B bows out / A quits clean).
- Sandbox forensics that cost time, recorded: bare `pty.fork`
  children get STOPPED at birth in this environment (empty fd list,
  sys=-1) — use the harness Session (Popen+pty slave+setsid), it
  works; `script -qec` dies on stdin EOF unless fed by a pipe.
- User-facing status: stacking is dead from THIS boot on (guard in
  the cached-command path too); true M4 open/close arrives at the
  next re-login when the binding picks up the toggle Exec.
  Toggle keeps a one-line-per-press log in $XDG_RUNTIME_DIR for
  exactly this class of question.

- Same-day follow-up 3 (user report): closing via the launcher icon
  "crashed" the app and left the terminal open — the toggle's pkill
  SIGTERM'd ctron with no teardown, so konsole -e held the window
  with a crash notice. Fix: SIGTERM/SIGINT now set a sig_atomic_t
  flag (file-scope handler — nested functions are a GCC extension,
  not C11) checked at the top of the TUI loop; the signal takes the
  normal exit path (worker join, settings save, notcurses stop,
  pidfile removal, exit 0). Harness-proven: rc=0 + pidfile gone.
  User-facing: icon-close and M4-toggle-close now look like regular
  quits.

## Session 2026-10-28 (7) — security audit: profile_set_note overflow fixed, v0.8.1 (GLM / Kerempkl / FA608PP)

Full-tree read-only audit (~19 k lines) → one ASan-proven stack
overflow found and fixed the same session.

- **The bug** (src/profile.c `profile_set_note`): the note-rewrite
  loop did `off += snprintf(out + off, sizeof(out) - off, ...)`.
  snprintf's return is the length it WANTED to write, so a >4 KB
  `.ctr` pushed `off` past `out[4096]`; the following call's
  `sizeof(out) - off` underflowed size_t and the append wrote past
  the stack buffer with file content (a shared/hand-made snapshot +
  `c` in the TUI is the trigger). PoC: real `profile.c` linked under
  ASan → `stack-buffer-overflow WRITE of size 513` at the append.
- **The fix**: `note_append()` clamps `off` into `[0, n-2]` before and
  after every append — oversized snapshots are rewritten truncated,
  never out of bounds. Regression test (12 KB profile, note
  round-trip via `profile_meta_parse`) added to `check_profiles`.
  Whole `make test` also passes under an ASan build (build the binary
  under the name `test_core` — the singleton tests compare comm).
- **Verified clean** (code vs SECURITY.md claims): privileged writes
  argv-only; popen/ut_exec commands code-constant; sscanf width-
  bounded; ut_log formats literal; profile_sanitize_name blocks
  `.`/`..`; pidfile O_EXCL|O_NOFOLLOW + uid gate; daeboard socket
  exchange 2 s-capped. `gcc -fanalyzer`: 5 warnings, all false
  positives or benign (child fds close at exec via O_CLOEXEC;
  ctrl_ppt_limits writes all outputs; fan_normalize fills order[];
  cut_step's memcpy regions were adjacent, now memmove anyway).
- **ctron-toggle.sh**: KWin `.js` created via `(set -C; cat > file)`
  (stamp's rule); symlink→/etc/passwd probe refused, target
  untouched; one retry on collision, else the KWin branch is skipped
  with a log line.
- **SECURITY.md**: §2.4 records the find+fix; Phase 2.2 marked DONE
  (js noclobber; $TERMINAL already single-quoted argv, no shell);
  §4.1 daeboard warning — NEVER add a sudoers rule for
  `~/Documents/daeboard/install.sh` (user-writable path as root).
- **Makefile**: `HARDEN=1` → `-fstack-protector-strong
  -D_FORTIFY_SOURCE=2` (opt-in like LTO).
- **v0.8.1** (PATCH): VERSION + README badge + flake in one commit.
- Verified: 0 warnings (`make`, `make HARDEN=1`), `make test`,
  `make tuitest` (snapshots note flow included), `--status` live,
  `make install` fresh in the same breath.

Signed: **GLM 5.3** — audit, fix, tests, docs, release.

*Date: 2026-10-28*

## Session 2026-10-28 (5) — FAN section + toggle escalation + sensor map, v0.8.0 (GLM / Kerempkl / FA608PP)

- Follow-up ("devam edelim"): the deferred item 4 ran — **FAZ 4/16
  measured and CLOSED as evidence-backed no**. Exact-query latencies
  (driver 615.71): baseline p50 20.6 / p95 30.3 ms; NVENC 4K60 real
  encoder load (75 % · 77 W) p50 28.9 / p95 32.3 ms; GL graphics
  stress via stress-ng DRI_PRIME=1 (86 % · 85 W) p50 21.2 / p95
  24.4 ms — real load does not lengthen the query. Only artificial
  8× parallel self-queries grew it (p95 79 ms; no real scenario,
  still 37× under the 3 s timeout). Background sampler stays
  unbuilt; NEXT.md item 16 now carries the numbers.
- User picked 3 of the 4 offered items ("1 2 3 ü yapalım"); item 4
  (nvidia-smi load-latency measurement for the conditional FAZ 4/16
  background thread) deferred on purpose.
- **FAN section (8th)**: discovered during the "ne yapabiliriz"
  audit — TS_FAN_CPU/GPU have been sampled every tick since v0.4 but
  no CD_* list ever referenced them. New section between NET and
  POWER (grid = full 4×2 at 2 cols): cpu/gpu RPM wash charts (new
  U_RPM unit), grid info "custom curve cpu on · gpu on · staged",
  detail pointer to the FAN workspace view. Footers/HELP 1-7 → 1-8;
  NET stays digit '6' so the smoke flow needed only the section-tuple
  + label updates. Live grid dump verified the "custom curve" line.
- **ctron-toggle escalation**: close_ctron() = polite TERM, ~2 s
  poll (4×0.5), then `pkill -9 -x ctron` + a log line; all three
  close sites (KWin ACTIVE, Hyprland active, fallback) route through
  it. Proof: a fake comm=ctron binary (build/fakectron/, cleaned
  after) — TERM-trapping variant died via escalation with the log
  line, polite variant died cleanly with none. The combined-run
  "case 2 fail" was the toggle's KWin branch picking a different
  window match in that invocation, not the close path — isolated
  rerun proved both cases. make install refreshed the installed
  script FIRST (lesson 9).
- **HARDWARE.md**: telemetry sensor map appended to both language
  notes (every 't' source + honest limits: Tccd label gate,
  nvidia-smi field traps incl. the latched 6-field fallback, SoC PPT
  semantics, gpu_metrics 0xFF + RAPL root = no sudoless CPU watt,
  spd5118 rarity, net exclusions, battery caveats, PD-on-barrel).
- Parallel-line coordination held: re-read panel_telem_full.c before
  editing (it gained the desc field + grid_rows pattern this
  morning); FAN entries written in the NEW 4-field chartdef shape.
- **v0.8.0** cut (MINOR). Tag+main push user-side (v0.6.0/v0.7.0
  tags also still unpushed — one `git push origin main v0.6.0 v0.7.0
  v0.8.0` covers all).
- Verified: 0 warnings, make test, full tuitest, install same
  breath; zero stray ctron processes after every pty step (the new
  discipline held).

## Session 2026-10-27 (3) — chart types matched to context (GLM / Kerempkl / FA608PP)

- User: "kullanılan grafik türleri kullanıldıkları yerlerde pek
  kullanışlı değiller başka grafik türlerini değerlendirelim" —
  re-review with fresh screenshots confirmed both failure modes:
  grid minis (2-3 rows braille) = grey blobs, axis labels eating
  narrow boxes, boxes "busy"; detail braille = dot texture in front
  of the shape, ragged top edge, spikes drowned in fill.
- User delegated the per-type picks ("önerilenleri sen seç").
- Detail: TWO-TONE SOLID BLOCK AREA — █ fill in dim accent2, top
  cell an eighth-block (▁▂▃▄▅▆▇█) in bright accent = crisp line over
  quiet area; rows*8 vertical levels; one ui_putln per same-color
  run (a cell carries one color). Y-axis labels + stats rows kept.
- Grid: mini charts GONE; grid_rows() = one "label value sparkline"
  row per series (telem_spark single line) + info line. Boxes quiet,
  readable at a glance; CD_* lists still shared with details.
- Merge note: landed on top of round 2's sensor-fill state (7
  sections incl. NET, digits 1-7, Tccd/RAM-alarm/NVMe-hottest/
  iGPU-busy lines all preserved in the new row format).
- Verified: 0 warnings, make test, full tuitest, screenshots (grid
  calm + CPU detail two-tone confirmed), install in the same breath.

## Session 2026-10-27 (2) — sensor-fill round + CCD proof (GLM / Kerempkl / FA608PP)

- User: "token bol — listelerdeki yapılabilir her şeyi yap". Scope:
  everything doable on THIS machine; the FA507NVR/reboot/external-
  monitor field items stay with their owners.
- **④ shipped**: `amdgpu_dev_dir()` (glob, cached once) +
  `gpu_busy_percent` + `pp_dpm_sclk` (new pure `hw_parse_pp_dpm`,
  unit-tested incl. the no-active-marker case) → iGPU detail "busy N%
  · sclk DPM 2/3 @ 2200 MHz"; `--status` iGPU busy line.
- **⑤ shipped**: k10temp temp3/temp4 as Tccd1/Tccd2 — the label files
  are read ONCE into a static cache (`-2` unchecked / `-1` absent) so
  a different chip layout can never mislabel a sensor; surfaces: CPU
  grid+detail (" · Tccd 80/95 °C", silent when absent) and --status.
  NVMe temp2/temp3 → per-drive hottest on the STORAGE detail.
- **⑥ shipped**: spd5118 temp1_max/crit + temp1_max_alarm. The kernel
  flag was live during the user's stress run (modules 57/66 °C over
  max 55): grid "modules 57/66 °C !>55", detail "limits max 55 · crit
  85 °C · over max: module 1,2".
- **⑦ CLOSED AS NO-GO, with proof**: gpu_metrics reverse-engineered
  live (120-byte blob, signature `78 00 02 01`, kernel 7.2). Six
  paired samples: the only moving power field (offset 28, u32) tracks
  GPU busy 0→10 W while the whole-package PPT swings 7–27 W with CPU
  load — it is average_gfx_power, and every CPU-scale block is
  permanently 0xFF. So Dragon Range firmware never fills the CPU
  fields; CPU power is not honestly readable without root (RAPL
  energy_uj 0400). The ONE live slice (gfx rail) ships as
  `hw_metrics_gfx_mw` (4-byte signature gate + 200 W range check,
  unit-tested): iGPU detail "APU PPT = whole-package (CPU+iGPU) · gfx
  rail 10.0 W".
- **CCD field item closed by automated proof**: POWER → 'c' overlay
  dumped through the screenshot-tool Screen emulator; columns CCD1
  c0·16…c7·23 / CCD2 c8·24…c15·31 matched the sysfs L3 groups
  (0-7,16-23 / 8-15,24-31) EXACTLY. Note: this boot has BOTH CCDs
  online (32 present), unlike the 10-10 single-CCD observation —
  present-mask is firmware/boot dependent, the ctron side is correct
  either way (topology always from thread_siblings/L3).
- AGENTS.md architecture map synced (main.c/telem.c/daeboard.c/ui
  SECTIONS note/tuitest) — the map had drifted since v0.2.
- **v0.7.0 cut** (MINOR: new reads/surfaces): triple + tag per
  policy. Tag+main push stays with the user.
- Verified: 0 warnings; `make test` (pp_dpm/metrics cases); full
  `make tuitest`; `make install` same breath (0.7.0 live).
- Post-session incident report ("uygulama açılmıyor"): a ctron leaked
  from the agent's 21:08 pty probe was still alive (dead pty + setsid
  = no SIGHUP; spinning in R state, ignoring q and SIGTERM; SIGKILL
  needed). ctron-toggle's `pgrep -x ctron` saw it and ran the CLOSE
  branch on every M4 press instead of opening. Killed; fresh launch
  verified; no ctron code involved (the real quit paths were proven
  10-11). Agent discipline recorded: verify probe children are dead
  after every Session (close() alone is not enough).

## Session 2026-10-27 — recommended-steps round: v0.5.0 push + MIT + NET/pstate (v0.6.0) (GLM / Kerempkl / FA608PP)

- Started from the morning's comprehensive md/status audit (every
  open item cross-checked against code/git — stale memories found and
  corrected: the asusd probe RAN 10-18 and its provenance follow-up
  landed 10-26; the P4 swatch x+40 was fixed 10-12; FAZ 1/3/4 fully
  closed). The user then approved the three recommended steps.
- **v0.5.0 tag pushed to origin** — first agent-side push, explicitly
  approved ("önerilen adımları yapalım"); verified on the remote
  (main itself was already synced by the user). v0.6.0's tag+main
  push stays with the user per the usual flow.
- **LICENSE = MIT** (user: "nothing to change, low cost"), plus the
  README license badge. LICENSE item closed.
- **NET telemetry section** (7th): `hw_net_if_whole` classifier
  (physical+wifi only; lo/veth/docker/br-/virbr/tap/tun/wg/tailscale
  excluded — double-count guard like diskstats_whole, unit-tested),
  `read_net_ifs` cumulative counters + operstate each fast poll,
  `TS_NET_RX/TX` series with the disk delta rule (counter reset = one
  honest unknown tick). Grid: rx/tx charts + "wlan0 up · enp3s0
  down"; detail: per-if totals + grand total; `--status` NET line;
  shared `hw_fmt_bytes`.
- **GPU pstate + throttle reasons**: the one nvidia-smi query grew
  `pstate` + ten named `clocks_event_reasons` flags. Driver facts
  learned by probing: dashed field names in `--help` are NOT valid
  queries (must use underscores); `clocks_event_reasons.active`
  returns a HEX bitmask on driver 580 (unknown bits like 0x400), so
  the self-labeled "Active"/"Not Active" named fields are parsed
  instead; an invalid field rejects the WHOLE query → one failure
  latches a permanent fallback to the old 6-field form (a transient
  failure would also latch — honest degradation, temps never lost).
  Surfaces: GPU grid info, detail "pstate P4 · throttle: none/…"
  (`hw_gpu_throttle_str`, unit-tested), `--status` pstate + throttle
  line when active.
- Smoke lessons added to the pile: the digit-jump probe initially
  asserted `rx`, which is fragment-prone coming from STORAGE ("read"
  shares the 'r' cell — the diff skips it) AND '7' opens POWER, not
  NET (7 sections: NET is '6'); fixed with digit '6' + probe
  `total`. Diagnosed by replaying the screenshot tool's Screen
  emulator over the pty stream — that text-grid dump is the fastest
  way to see what the TUI actually rendered.
- Sensor shortlist status after this session: ② done earlier, ①+③
  done now → remaining ④ half (gpu_busy_percent, pp_dpm levels), ⑤
  Tccd1 + NVMe Sensor 1/2, ⑥ RAM thresholds, ⑦ CPU power (RAPL root /
  gpu_metrics parse), ⑧ smartctl.
- **v0.6.0 cut** (MINOR: new sections/reads): VERSION + README badge
  + flake triple in the release commit, annotated tag on top.
- Verified: 0 warnings; `make test`; full `make tuitest`; `make
  install` in the same breath (0.6.0 live on ~/.local/bin).

## Session 2026-10-26 — telemetry: section detail pages + v0.5.0 (GLM / Kerempkl / FA608PP)

- Started as a read-only sensor inventory the user asked for (full
  hwmon/nvidia-smi/battery/PD sweep — the shortlist for the NEXT
  telemetry expansion: PD port watts, GPU pstate/throttle reasons,
  Tccd1, NVMe sensor 1/2, network rx/tx; RAPL package energy exists
  but is root-only 0400). The user then picked the layout feature:
  grid navigation + per-section full pages.
- UX as shipped (user plan + agreed additions): j/k/arrows move the
  grid cursor (row-major, wrap), Enter/Space opens the selected
  detail, 1-6 jumps directly, h/l flips detail sections, esc backs
  detail→grid→close, t/T close from anywhere, q quits (rule 5 kept).
  Detail entry/switch runs `hw_refresh_live` so live-only info lines
  (PPT/EPP/boost/health/limits) are never stale. `telem_sel`
  persists across close/reopen; `telem_detail` resets on close.
- Charts: new pure accessor `telem_at(idx, back)` (unit-tested);
  the panel-side renderer maps the whole ring window to columns
  (slice max per column — spikes survive), half blocks give 2×rows
  levels, scale = series range or window min/max (AUTO), flat
  windows stand on the middle like the sparkline. Charts shrink then
  drop (newest-last) on short terminals; info lines survive alone.
- Reads added: amdgpu `in0/in1_input` → `igpu_vddgfx_mv/vddnb_mv`
  (fast poll), battery `charge_full(_design)` (+`energy_*` fallback)
  → `bat_health` % (live snapshot). Honest "--" when absent.
- Mouse: grid boxes register `TGT_PANEL_TELEM` targets (click =
  select, second click = open); click outside the overlay closes it,
  matching every other overlay. The detail page has no targets yet.
- Harness lesson (cost a debug round): the notcurses render diff
  skips unchanged cells — multi-word probes like "esc grid" arrive
  fragmented ("esc`[10G`grid") because the spaces between words
  didn't change. Assert single words instead (detail: `h/l`, grid:
  `j/k`). Second lesson: bash double quotes pass `\r` as two chars —
  screenshot-script key sequences need `$'...\r...'` ANSI quoting.
- Verified: make 0 warnings; make test; full make tuitest (telem
  flow extended: cursor → enter → h/l → digit jump → esc back →
  stray key still swallowed → close/reopen → q); screenshots of the
  grid + CPU/POWER detail pages reviewed; make install. User field
  test (real terminal + mouse) still pending, as usual.
- Release: **v0.5.0** cut per policy (MINOR = new UX): VERSION +
  README badge + flake.nix in the release commit, annotated tag on
  top. Push stays with the user. Sensor-expansion series (PD watts,
  pstate, ...) is the natural next task — inventory above.
- Same-day follow-up (user report "grafikler çok bozuk"): block area
  charts replaced by btop-style **braille line graphs** (2×4 dots per
  cell, vertical interpolation between points, no cell seams) plus a
  new chart_scale for AUTO series — 25%-of-magnitude minimum span
  (near-flat series render flat, not full-height noise), 5% pad,
  exponential follow against window-slide rescale jumps; fixed 0-100
  ranges untouched. No -lm added (dmax3 helper, not fmax/fabs).
  STALE INSTALL BIT AGAIN, for the exact reason the lesson exists:
  the user tested in the 20:07→20:27 window before `make install`
  ran ("bişey değişmedi" — nothing changed, because nothing HAD
  changed in ~/.local/bin). Also remind them a RUNNING ctron does
  not hot-swap: quit + relaunch after any install.
- Same-day take 3 (user: "grafik te ne gösterildiği anlaşılmıyor +
  silik yazılar okunmuyor + yeni grafikler sadece detayda"): charts
  became braille AREA graphs (dots filled bottom-up — shape reads at
  a glance), info/stats lines went muted→text color (telemetry-local;
  palettes untouched), and the grid boxes now chart the SAME series
  as the detail pages via shared CD_* lists + one stack_charts()
  stacker (grid: compact, no stats row, 1-row min; detail: stats row,
  2-row min). metric()/sparkline rows deleted from the grid;
  telem_spark itself survives (LIVE strip + tests). Installed in the
  same breath this time.
- Same-day take 4 (user: "neyi temsil ettiğini anlayamıyorum" + iGPU
  PPT observation + PD request): charts got a y-axis of their real
  scale (max top-left / min bottom-left, unit included) — the graph
  now self-describes; amdgpu power1_input honestly relabelled "APU
  PPT" (whole-APU package power, CPU+iGPU — user spotted the
  mislabel) with an explainer line; POWER gained TS_PD_MW + a usb-c
  chart (read_pd_ports: type="USB" supplies, µV×µA→mW; honest "--"
  while unplugged — this box is on barrel AC, ports read 0) and
  battery electricals (voltage_now/current_now → "14.69 V · 2.76 A")
  beside the health %. Series count now 18.
- Same-day take 5 (user request): grid navigation is now SPATIAL —
  arrows/hjkl move in 2D on the 2×3 grid (row-wise left/right,
  column-wise up/down, edges clamp, no wrap; column count mirrors
  the draw pass via rc_overlay). Smoke proves it: j+l from CPU must
  reach RAM ("swap"), linear stepping would have hit iGPU. Detail
  h/l stays a linear flip.

## Session 2026-10-26 — security: privileged writes fully shell-free (GLM / Kerempkl / FA608PP)

- User asked "security ile ilgili neler yapılabilir"; the read-only
  review found the FAZ 3 chain intact but two privileged paths still
  through `/bin/sh` and daeboard.c never migrated. User picked items
  1+2+3; deferred: 4 (install.sh-sudoers honesty note) and 5
  (toggle-js noclobber + env-hygiene doc lines).
- `cpufreq_write_all` (control.c) now feeds the value through
  `ut_write_via_argv`: one `timeout 3 sudo -n tee` with every path as
  an argv word. Journal-proven single invocation (all 16 present cpus
  in one sudo); the old 4 KB command-string ceiling that refused
  machines beyond a few dozen cpus is gone. Per-node
  `ut_priv_write` fallback unchanged.
- `ctrl_ppt_ryzen_cmd`/`ctrl_ppt_wrapper_cmd` became `ctrl_ppt_*_argv`
  vector builders (integer words in a caller-supplied
  `char bufs[3][16]`; `ut_exec_argv` execs under its 3 s cap). Tests
  assert the words; flag order + W→mW mapping unchanged. PPT
  deliberately NOT live-tested (volatile SMU state, current user
  intent unknown) — `ut_exec_argv` itself is live-proven by the
  nvidia-smi paths.
- daeboard: all four `system()` calls (start/install probes,
  systemd-run, install.sh) and `--doctor`'s probe exec argv vectors;
  env-derived paths are exec'd as words, the manual quote-rejection
  checks left with the shell.
- Verified: clean `make` (0 warnings), `make test` ok, `--doctor` +
  `--status` healthy, EPP roundtrip through the real
  `sudo -n tee /sys/devices/system/cpu/*` rule (power →
  balance_performance → power, read back). `--doctor`'s
  "sudo -n: unavailable" is the honest answer on this box — `sudo -n
  true` is not in the NOPASSWD set (same verdict the old probe gave;
  not a regression).
- Commits: 12bd760 (control + tests), 53fd272 (daeboard + doctor),
  this docs commit. `make install` refreshed ~/.local/bin. No version
  cut (hardening, no API/UX change — rides the next release).

## Session 2026-10-26 — docs: machine-local setup notes genericized (GLM / Kerempkl / FA608PP)

- Pass over the public docs replacing per-host setup-state sentences
  with generic guidance: SECURITY.md §4.4 NixOS paragraph reworded,
  one 10-08 handoff line and one changelog line trimmed. Generic
  sudoers wording and rule examples untouched — host-local setup
  state doesn't belong in the guides.
- Accepted by the user: pushed history still carries the old wording
  (a true removal would need history rewrite + force push — out of
  proportion for this).
- No code change; make/test untouched.

## Session 2026-10-26 — FAZ 4 executed: items 13/14/15 landed (GLM / Kerempkl / FA608PP)

- Plan mode first (approved): one Explore pass pinned the safety
  assumptions — no tuitest flow depends on a fresh post-key hardware
  read (all UI-text/log asserts), write actions self-refresh (apply
  worker hw_refresh_live, POWER entry, kbd/bat in-place updates, fan
  Write verifies internally), notcurses 3.0.17 has get_nblock, and the
  main link rule omits CFLAGS (LTO needed its own link flag).
- **a7d82b5 hw: sweep split** — cpu_limits_sweep(hw, full): static
  column (present + cpuinfo_min/max) only at init / refresh_live /
  ≥5 s; per-tick pass = scaling_max only → 48 → 17 reads. refresh_live
  zeroes the throttle (always full after actions). --status verified
  byte-identical modulo the live iGPU MHz power-gating flap (5-run
  proof that the flap predates the change).
- **e2dd603 ui: rate-limit + batching** — notcurses_get_nblock drains
  the queued input (each key/mouse dispatched individually, CSI
  '['/']' filter and RESIZE handling preserved), then ONE
  maybe_refresh: 120 ms floor for key-driven refreshes, force on
  timeout/resize/apply-finalize (which also zeroes the timestamp).
  Probes: key-held 8.73% → 3.33% (2.6 → 1.0 ms per key), idle 1.10 →
  1.00%. The leftover ~1 ms/key is the draw phase (real feedback) —
  panel-level dirty flags deliberately out of scope; stopped here.
- **347e881 build: strip + LTO** — install -s on the binary only
  (285 → 261 KB; the 150–180 KB estimate was WRONG — no -g in the
  build, strip only drops the symtab; recorded honestly in NEXT).
  LTO=1 → -flto=auto on compile+link via LDLTO (first try with plain
  -flto printed an lto-wrapper serial-LTRANS note; =auto silences it);
  0 warnings, tests green, LTO binary 276 KB. Default build untouched;
  -march=native stays parked (portability rule).
- Verified per commit: make 0 warnings, make test, make tuitest (the
  back-to-back key flows stress the batching path), --status/--doctor,
  make install (installed --version 0.4.0 live). Docs synced (NEXT
  FAZ 4 items marked BITTI with post-change numbers, CHANGELOG,
  HANDOFF). No release cut — internal perf rides the next MINOR.
- Open in FAZ 4: item 16 (nvidia-smi worker) stays conditional on a
  field observation of the query lengthening under load.

## Session 2026-10-26 — performance/optimization audit → NEXT.md FAZ 4 (GLM / Kerempkl / FA608PP)

- User asked for a comprehensive look at optimization opportunities.
  Question-first: no code changed, repo left clean; findings written to
  NEXT.md as FAZ 4 with the raw numbers (CHANGELOG entry alongside).
- Method: hot-path code read (ui_run loop, hw_refresh_fast, telem,
  draw helpers) + live measurements — pty harness `Session` +
  `/proc/<pid>/stat` utime/stime sampling for the TUI, `/usr/bin/time`
  for --status and the external tools. Probe was throwaway (tmp),
  deliberately not committed; FAZ 4 header records the method so it
  can be rebuilt.
- Measurements (FA608PP, 160×50, 250 ms poll): idle **1.10%** CPU,
  telemetry fullscreen 1.13% (draw cost negligible; grep-proven zero
  malloc/exec/strdup in all ui/*.c draw paths), key-held burst ~30 Hz
  **8.7%** = 2.6 ms per keypress (every key runs a full
  hw_refresh_fast + full redraw; fast poll ≈ 69 file reads/tick, 48
  from cpu_limits_sweep), nvidia-smi extended query 20–30 ms every 2 s
  on the UI thread, --status 0.21 s end-to-end (kscreen-doctor /
  asusctl / busctl / nvidia-smi 10–30 ms each — startup is NOT a
  problem), binary 285 KB unstripped.
- Verdict: earlier wins (path cache 10-01, batched writes 10-09, 2 s
  GPU query cache 10-26) did their job; what remains is polish. FAZ 4
  ranking: 13 key→refresh decoupling (only measured win worth chasing;
  rate-limit or input batching), 14 cpu_limits_sweep split (static
  cpuinfo_* throttled → 48→17 reads), 15 install strip + optional LTO
  (free; march=native explicitly parked against the portability
  rule), 16 conditional nvidia-smi worker (only if load-lengthening
  is observed). Weak-justification items recorded with reasons-not-to
  (fd/pread cache vs suspend-resume model, statvfs cache, erase_region,
  telem-sampling-always as deliberate design).
- Harness lesson hit AGAIN, sharper: a bare pty with no responder
  never returns from notcurses_init (0.00% CPU, stuck process) — the
  harness Session with its query responder is the only reliable driver
  for CPU measurements too, not just for flows.

## Session 2026-10-26 — telemetry open fix: stale install + 'T' everywhere (GLM / Kerempkl / FA608PP)

- User: "telemetry açılmıyor". Root cause: ~/.local/bin/ctron was
  still 0.2.1 — nobody ran `make install` after the telemetry landed
  (LESSON: after landing a user-facing feature, `make install` in the
  same breath; the user launches the installed binary). Reinstalled →
  0.4.0 verified.
- Real UX trap tightened while there: lowercase 't' stays the fan/
  power exact-value key when the workspace is focused in those views;
  'T' now opens telemetry from anywhere (POWER dropped its uppercase
  typing alias; fan editor only ever bound lowercase). At startup
  (focus=profiles) plain 't' already worked.
- tuitest pins all three open paths. Note: the v0.4.0 tag (954fcb1)
  predates this tweak by one commit — it rides the next release.

## Session 2026-10-26 — fullscreen sectioned telemetry 't' + v0.4.0 (GLM / Kerempkl / FA608PP)

- User request evolved the parked mini-graph decision into: "telemetry
  fullscreen on 't', split into SECTIONS (CPU, GPU/iGPU, RAM, storage,
  ...)". Data sources probed live first (amdgpu hwmon serves the iGPU
  without gpu_metrics parsing; spd5118 x2 RAM module temps; nvme x2;
  meminfo/loadavg/diskstats; dGPU query extended in-place) — then
  built in four green-committed steps:
  1. 5d0bf8a pure telem core (X-macro series table, 240-slot rings,
     250 ms gate, block sparkline; unit-tested).
  2. 59cdc97 hw read layer (extended nvidia-smi single query, iGPU/
     RAM/NVMe hwmon via hw_hwmon_path_nth, diskstats whole-disk
     parser, statvfs; --status block verified live: GPU 6.08 W, iGPU
     57 °C/600 MHz).
  3. 6beee01 UI: section registry (SECTIONS[] = the whole layout),
     responsive 2-col grid, contextual 't' (FAN/POWER typing views
     keep their 't'), t/T/esc close, q quits, all else swallowed,
     read-only. tuitest flow_telem_full green; live screenshot
     docs/tui-telem.png visually verified (6 sections, real values,
     no clipping) and added to README.
  4. v0.4.0 cut (this commit): VERSION + README badge + flake version
     together per policy; NEXT.md mini-graph item closed (superseded
     by the 't' screen), PLANS.md dash plan marked superseded (only
     the ASCII-art picker stays parked).
- Flexibility ask honoured: a section is one SECTIONS[] entry + one
  draw fn; a metric is one TELEM_SERIES row + one telem_poll
  extractor line — reorder/remove without touching anything else.

## Session 2026-10-26 — display multi-monitor parsing + flake.nix (GLM / Kerempkl / FA608PP)

- "Yapılacakları bitirelim" round. Roadmap FAZ 2 item 6 (code half)
  closed: kde backend picks the eDP* panel block (else first) and
  parses Modes only within it; hypr backend derives name + geometry +
  modes from ONE focused monitor object (hypr_obj_from/hypr_mon_obj).
  Offline two-monitor tests for both (new disp_hypr_test_parse seam);
  live single-monitor regression identical (165 Hz / 60,165 on
  FA608PP). Live HDMI-A-1 proof explicitly moved to the FAZ 2
  field-test items (hypr on FA507NVR, kde on FA608PP with an external
  monitor).
- flake.nix written from scratch (no nix on this machine — NOT
  build-verified here; the retired v1 copies no longer carry one):
  packages.<sys>.ctron + default, devShell = the documented
  nix-shell toolchain. flake version now rides the release commit
  (policy line added). Arcioth checklist: nix build, nix develop +
  make, generate+commit flake.lock on first run.
- FAZ 1 item 3 stays open pending that verification. Remaining agent
  work in the roadmap: none — the rest is FAZ 2 field tests
  (Arcioth/user), the mini-graph scope decision (user), parked.

## Session 2026-10-26 — FAZ 3.12 wrapper probe + v0.3.1 cut (GLM / Kerempkl / FA608PP)

- ctrl_ppt_wrapper_cmd: PPT writes in ryzenadj-mode probe
  /usr/local/bin/ctron-ppt (access X_OK) FIRST; installed + allowed
  wrapper means ryzenadj is never invoked directly. Falls through to
  the existing candidates on missing wrapper / denied rule — inert on
  FA608PP (wrapper not installed there), zero behaviour change.
- **v0.3.1 cut** (PATCH per the agent-managed semver policy): the
  post-v0.3.0 accumulation is hardening/fixes/docs only — pidfile
  O_EXCL claim, shell-free privileged writes, argv-vector exec with
  the 2>&1 honesty fix, provenance wording, sudoers §4, wrapper
  probe. VERSION + README badge bumped in the release commit;
  annotated tag on it, push stays user-side.
- With this, **FAZ 3 is fully closed** (items 8-12). Remaining
  roadmap: FAZ 1 flake.nix (Arcioth), FAZ 2 field tests (Arcioth +
  FA608PP by hand), mini-graph scope decision (user), parked list.
- Verified: make 0 warnings, unit + tuitest green, wrapper cmd tests
  pinned (bare ints, default path).

## Session 2026-10-26 — FAZ 3.11: code-verified sudoers documentation (GLM / Kerempkl / FA608PP)

- SECURITY.md section 4 written from the code and live machine facts:
  the sudo invocation table (tee path families, ryzenadj try-order
  /usr/bin → /usr/sbin → bare, nvidia-smi absolute from command -v,
  daeboard, sudo -n true probes), minimal ruleset, three-integers
  ctron-ppt wrapper, NixOS store-path notes, verification checklist.
- Merged-usr literal-matching footgun documented (sudoers matches the
  literal string; bare names resolve via secure_path sbin-first) —
  the "password required while sudo -n true works" symptom explained.
- Live-verified: both asus fan hwmons resolve under
  /sys/devices/platform/asus-nb-wmi/* (tee rule family covers curve
  points + pwm enable). Sandbox cannot read /etc/sudoers.d (by
  design); the machine's current file is the user's path-scoped set
  from 10-08.
- Opened NEXT.md FAZ 3 item 12: the ~10-line wrapper probe in
  ctrl_ppt_ryzen_cmd that would let ctron itself use §4.3. FAZ 3
  items 8-11 all closed; docs-only change (no code, no build impact).

## Session 2026-10-26 — FAZ 3.9: argv exec for nvidia-smi/kscreen-doctor/hyprctl (GLM / Kerempkl / FA608PP)

- ut_exec_argv family added (line / raw-ANSI / both-stderr-merged;
  fork+execvp under `timeout 3`; capture drains fully so a chatty
  child cannot fake a SIGPIPE failure). All nvidia-smi (temp,
  max-clock, verify sampler, sudo -lgc/-rgc), kscreen-doctor (-o,
  mode apply) and hyprctl (monitors -j, eval, keyword fallback) calls
  migrated off shell strings.
- Found + fixed an honesty gap while migrating: the old lock/reset
  `2>&1` was a no-op (exec_run appends `2>/dev/null` after it), so
  stderr was silently dropped; _both genuinely merges now and FAILED
  logs include sudo's own complaint.
- Deliberately left on ut_exec: ut_have_cmd + nvidia_smi_bin (command
  -v is a shell builtin) and asusctl/ryzenadj (ints-only args) —
  documented in SECURITY.md Phase 2.1.
- Tests: check_argv_exec (capture, first-line trim, rc passthrough,
  127 on missing binary, ANSI strip, stderr merge, 100k-line drain).
  Live: --status reads GPU temp/max-clock + 165 Hz/modes through the
  new paths. make 0 warnings, unit + tuitest green. FAZ 3'te kalan:
  3.11 sudoers doc (kod değil).

## Session 2026-10-26 — FAZ 3.8: ut_priv_write goes shell-free (GLM / Kerempkl / FA608PP)

- Roadmap FAZ 3 item 8 executed (SECURITY.md Phase 1.1). The sudo
  fallback had built `printf '%s' | sudo -n tee` through popen's
  /bin/sh with a pre-exec single-quote rejection as the only guard.
  New: ut_write_via_argv() — pipe2(O_CLOEXEC), fork, child dup2 +
  execvp of a pure argv vector (`timeout 3 sudo -n tee <path>`, the
  3 s cap now an argv word); parent ignores SIGPIPE around the feed
  write, the child's exit status decides. Quote rejection removed —
  nothing parses the arguments anymore.
- ut_write_via_argv is exported as a generic runner (FAZ 3.9's
  ut_exec_argv will grow next to it) and unit-tested without sudo:
  real tee round-trip with quotes verbatim, early-exit child must not
  SIGPIPE the runner, unwritable path fails cleanly.
- Verified: make 0 warnings, unit + tuitest green (the fan Write flow
  runs the new path live). SECURITY.md Phase 1.1 + 1.3 marked done.
  Next: FAZ 3.9 (ut_exec_argv + nvidia-smi/kscreen-doctor/hyprctl
  migration), FAZ 3.11 (sudoers doc).

## Session 2026-10-26 — FAZ 3.10: singleton/toggle claim hardening (GLM / Kerempkl / FA608PP)

- Roadmap FAZ 3 item 10 executed. Audit verdict: the toggle script's
  stamp was already symlink-safe (noclobber create, `rm -f` steal);
  the TUI pidfile claim was NOT — ut_write_file's fopen("w") followed
  a planted symlink in the /tmp fallback and would have clobbered its
  target on the stale-takeover write.
- ut_singleton_enter rewritten around open(O_CREAT|O_EXCL|O_NOFOLLOW,
  0600) with an lstat trust gate (regular + euid-owned) before any
  read/holder decision, bounded unlink+retry takeover, fail-open on
  un-takeable foreign entries (sticky /tmp). util.h contract updated.
- ctron-toggle.sh: umask 077 + log() symlink guard.
- New unit coverage pins the attack: planted symlink survives, victim
  file byte-identical, retaken pidfile regular 0600. make 0 warnings,
  unit + tuitest green. Next candidates: FAZ 3.8 ut_priv_write
  fork+execve, FAZ 3.9 ut_exec_argv.

## Session 2026-10-26 — roadmap execution: FAZ 1 provenance fix (GLM / Kerempkl / FA608PP)

- First roadmap item executed (NEXT.md FAZ 1 item 2): S_SNAPSHOT
  provenance wording upgraded to the 10-18 probe finding — asusd's
  own defaults are GENERIC fallbacks, so this machine's ron (which
  the snapshot matches exactly) is the best available factory
  reference, most plausibly EC-seeded at asusd first init; the
  residual caveat (ron rewritten on every curve change, pre-10-17
  edits may be baked in) stays in the comment. fan.h preset comment +
  FAN_PSRC_BUILTIN note + presets-modal label ("built-in snapshot
  2026-10-17" → "EC-seeded snapshot 10-17") aligned. pwmN_enable
  "factory" wording untouched (kernel semantics).
- HARDWARE.md EN+TR gained the `asusctl fan-curve --default` footgun
  bullet (installs GENERIC curves, not factory).
- Wording-only; unit tests pin values not strings and tuitest's
  flow_fan_presets pins title+log only — verified unaffected.
  make 0 warnings, make test + make tuitest green.
- Next candidates (not started): FAZ 3.10 pidfile O_EXCL audit,
  FAZ 3.8 ut_priv_write fork+execve; FAZ 1 flake.nix waits for
  Arcioth-side verification.

## Session 2026-10-25 — README redesign: GitHub landing page + real TUI screenshots (GLM / Kerempkl / FA608PP)

- User asked for a better-looking GitHub page. Delivered: hero header
  (centered title, tagline, shields badges: version/language/deps/
  platform), features table, honesty principles, TUI key table,
  collapsible `<details>` for display backends / M4 key / code layout,
  documentation index table.
- **Real screenshots, not mockups:** `scripts/tui_screenshot.py`
  (permanent tool) runs the live binary on a pty (Session adapted from
  tui_smoke.py, real xterm-256 palette + COLORTERM=truecolor in the
  responder), replays the ANSI stream through a small built-in terminal
  emulator (CSI/SGR subset — no pyte/tmux on this machine, no pip) and
  renders the screen grid to PNG with PIL/DejaVu Mono. Captures against
  an isolated CTRON_CONFIG. Output: `docs/tui-fan.png` (fan editor,
  hero) + `docs/tui-power.png` (POWER view). All three candidate frames
  (incl. the presets modal) vision-verified clean — coherent panels,
  no garbled cells.
- Truth fixes surfaced by the rewrite: CLI reference was stale —
  regenerated from the real `--help` (`--setup`/`--tui` removed;
  `--freq core|ccd`, `--ac/--battery-profile`, daeboard flags,
  `--fan cpu|gpu on|off` added; duplicated `--ppt` entry collapsed);
  snapshot dir corrected to `profiles/*.ctr`; parked list synced with
  NEXT.md's unified list.
- NEXT.md version policy: README `version` badge updates in the same
  release commit as the VERSION bump.
- No code changes; `make` untouched. Version stays v0.3.0 (docs-only).

## Session 2026-10-25 — roadmap rewrite + v0.3.0 release cut (GLM / Kerempkl / FA608PP)

- Docs audit first (user asked for a roadmap + critique of the files):
  NEXT.md had become an archive (6/8 "SIRADAKİ İŞLER" items BİTTİ,
  numbering broken — four items numbered "2"), the tctl gauge was
  listed half-open though it was closed as impossible-honestly, and
  open work was scattered across NEXT.md / PLANS.md / README /
  SECURITY.md / HANDOFF plus agent memory (v0.2.1 tag, FA507NVR live
  tests, 10-18 provenance follow-ups). PLANS.md still said "Not built"
  for daeboard sections that shipped long ago.
- NEXT.md rewritten as the single-source roadmap: version policy,
  FAZ 1 maintenance & honesty, FAZ 2 field-test debt, FAZ 3 security
  hardening (from SECURITY.md Phases 1-2), mini telemetry graphs parked
  with a scope decision pending (user chose "park" for now), one
  unified parked list; README Roadmap synced and now points at NEXT.md.
- Version numbering delegated to the agent by the user (10-25).
  Policy: semver, annotated tag on the VERSION-bump commit, tags
  pushed by the user only. First cut **v0.3.0** — MINOR-grade
  accumulation since 0.2.1 — tag sits on the release commit, local.
- Verified: make 0 warnings, make test green, `--version` = 0.3.0.

## Session 2026-10-18 — asusd factory-default probe (GLM / Kerempkl / FA608PP)

- Goal: learn the "standard" fan curves. Plan approved (restore-after,
  user's choice): ~/asusd-default-probe.sh, user-run with sudo once;
  ron moved aside → asusd restart → capture → restore. Clean run:
  REGENERATED, restore md5-identical, service active, profile
  Performance intact.
- **Finding 1:** asusd's regenerated defaults are GENERIC fallbacks —
  every profile `enabled: false`, quiet has a 255 °C sentinel temp
  point, 20 °C starts, formula-looking ramps. NOT the model's factory
  curves. Therefore the pre-existing ron content (EC-seeded at asusd's
  first init, most plausibly) is the real factory reference — and
  ctron's S_SNAPSHOT_* tables now match it exactly for all three
  profiles (Balanced synced 10-17; Quiet/Perf were already exact).
  The 10-17 provenance caveat is now bounded: alternative source is
  only the EC/user edits, not another machine's table.
- **Finding 2 (footgun):** `asusctl fan-curve --default` installs
  those GENERIC curves — do not use it expecting factory behaviour.
  Also `asusctl --version` is not a flag on this build (report uses
  `--help`).
- Live == Performance preset (user's observation) re-confirmed as
  correct: active profile is Performance and the EC curve is
  byte-identical to the ron's performance section.
- Artifacts: ~/asusd-default-probe/ (ron-before = factory reference
  copy, ron-defaults = asusd generic fallback, diff, report). Script
  kept for future re-verification. No code changes — docs only
  (CHANGELOG note + this section). Possible follow-ups (offered, not
  done): reword the S_SNAPSHOT provenance comment/modal label from
  "not verified factory" to "asusd default = generic; this snapshot is
  EC-seeded"; HARDWARE.md note for the --default footgun.

## Session 2026-10-17 — fan preset honesty package (GLM / FA608PP)

- User suspected the "ASUS factory" preset curves were wrong. Verified
  against the live /etc/asusd/fan_curves.ron: Quiet/Perf identical,
  Balanced CPU pwm off on 4/8 points, Balanced GPU a completely
  different (step) curve — almost certainly from another TUF model.
- Plan was self-reviewed once before approval (v2): modal height
  unchanged (source line replaced the "Curve preview:" label row),
  honesty moved onto the Apply button label ("Load into editor"),
  info struct slimmed to src+enabled, tuitest verified untouched.
- Landed: tables synced to this machine's ron (S_FACTORY_* →
  S_SNAPSHOT_*, provenance comment — NOT verified factory, ctron's own
  writes land in the ron); fan_preset_info_t source/enabled plumbing
  (fan_preset_get +5th param, fan_ron_parse +2, pure fan_preset_builtin
  seam); modal source line incl. per-fan "off (factory runs)" — this
  machine's Quiet has BOTH fans disabled in the ron (loading it does
  nothing audible; now it says so); Active fallback chain labelled;
  tiny-frame row clipping fixed; HELP 'm' line updated.
- Provenance caveat for FA507NVR: its own ron differs (no
  asus-armoury on some installs → built-in snapshot fires; the
  snapshot is FA608PP data — acceptable as a no-asusd fallback, now
  visibly labelled as such).
- Verified: make 0 warnings, unit (Balanced 2×8 pin + enabled:false +
  src paths) + tuitest green, installed; live harness checks of the
  modal (ron/live-EC sources, Load-into-editor, CPU+GPU-off wording).
- Commit by the agent (path-scoped).

## Session 2026-10-16 — parallel-line review + test-environment guard fixes (GLM / FA608PP)

- Reviewed the three landed parallel commits (9137b37, 9488f32,
  d6aae0c): swatch clamp correct (visibility guard + min_x check),
  toggle v3 structurally sound — the stamp guard survives on the
  open path, KWin raise via D-Bus scripting with journalctl result
  read, Hyprland branch, fallback close degrades to v2 behaviour;
  FAN PRESETS modal follows the overlay pattern and does not touch
  the SIGTERM handler region. Notes, not bugs: hyprctl dispatch rc
  is trusted (Hyprland's known "exit 0 anyway" pattern), journalctl
  result read is racy-but-bounded.
- Found the harness red at HEAD (9–10 flows) and 4 unit failures —
  reproduced even at c98fef4 in a worktree, so environment, not the
  new code. Root causes: (1) the 654a80f singleton guard bowed every
  harness instance out while a real TUI ran on the desktop — guard
  now fails open on CTRON_TEST (harness Session sets it) or
  CTRON_CONFIG (isolated config = deliberately parallel); (2) older
  unit tests leak CTRON_CONFIG into the process env (set, never
  restored — latent for weeks) and the bypass tripped on it;
  check_singleton now saves/clears/restores the variables. The
  ambient leak is documented, not fixed (cross-session test-order
  dependencies).
- Harness lesson: bare `make` does not rebuild build/test_core —
  three debug iterations ran a stale binary until timestamps were
  checked. Sandbox note: pty.fork children arrive STOPPED here; the
  harness Session (Popen+pty+setsid) remains the reliable driver.
- Verified: 0 warnings, unit + all tui flows green, including with
  the user's real TUI open. Installed.

## Session 2026-10-14 — Fan curve startup honesty & FAN PRESETS modal overlay (Antigravity / Kerempkl / FA608PP)

- User feature request:
  1. The FAN view in the TUI must initially display the active/live hardware
     curve directly from the EC hwmon instead of showing stale curves from
     `settings.ini`.
  2. Pressing `m` (or clicking a button) in the FAN editor opens a floating
     modal overlay titled `FAN PRESETS`.
  3. Four presets offered: `1: Quiet`, `2: Balanced`, `3: Performance`,
     `4: Active (Live)`.
  4. User can selectively apply to CPU, GPU, or both using independent toggle
     buttons: `Apply to: [✓ CPU] [✓ GPU]` (`c` / `g` hotkeys or mouse clicks).
  5. Selecting a preset previews it without auto-applying; pressing `Enter`
     loads it into the editor; `w` writes to hardware as usual.
- Diagnosed & Fixed Stale Startup Curves:
  - `settings_load()` previously set `hw->fan_staged = true`, locking the TUI
    into the curve saved in `settings.ini` and blocking `fan_hwmon_read()`.
  - Fix: `settings_load()` no longer marks `fan_staged = true`, and preserves
    live hwmon curves read at `hw_init()`. Persistence across sessions is handled
    cleanly via SNAPSHOTS (`.ctr` profiles).
- Implemented FAN PRESETS Modal Overlay:
  - `editor_fan_presets_open/draw/key/act` in `src/ui/editor_fan.c`.
  - Pure RON parser `fan_ron_parse()` extracts factory curves from
    `/etc/asusd/fan_curves.ron`, falling back to built-in factory tables.
  - `hw_fan_hwmon_read_live()` reads active `pwm1`/`pwm2` sysfs nodes on demand.
  - Bottom row in FAN view replaced legacy `Stk/Sil/Col/Ful` with `[ Presets (m) ]`.
- Verification:
  - Warning-free build (`make`, `-Wall -Wextra`).
  - Unit tests in `tests/test_core.c` (`check_fan_presets`).
  - TUI smoke test in `scripts/tui_smoke.py` (`flow_fan_presets`).
  - Live `./build/ctron --status` verifies real active EC curves read cleanly.
  - Installed to `~/.local/bin/ctron` via `make install`.

## Session 2026-10-13 — M4 toggle: background raise vs foreground close (Antigravity / Kerempkl / FA608PP)


- User request: pressing M4 when ctron is running in the background now
  brings it to the front instead of closing it; pressing M4 when already
  in the foreground / active closes it.
- Implemented in `scripts/ctron-toggle.sh`:
  - Gathers ctron PID and ancestor PIDs (terminal, subshell).
  - KDE Plasma (KWin): loads a temporary KWin script via DBus to query
    `workspace.windowList()` and `workspace.activeWindow`. If found and
    active, exits to `pkill -x ctron`. If found in background or
    minimized, restores/activates via `workspace.activeWindow = found`.
  - Hyprland: checks `hyprctl activewindow -j` PID; if active closes,
    else focuses via `hyprctl dispatch focuswindow pid:<pid>`.
  - Fallback: closes if window is untracked.
- Verified live with live test harness on KDE Plasma 6 Wayland:
  defocused konsole -> `branch: raise` (window raised to front, ctron
  kept alive), subsequent press -> `branch: close` (clean quit).
- Installed to `~/.local/bin/ctron-toggle` via `make install`.

## Session 2026-10-12 — LIGHT swatch dynamic placement + SECURITY.md (Antigravity / Kerempkl / FA608PP)

- P4 item 4 closed: `draw_light` in `panel_workspace.c` had a hardcoded
  `x + 40` swatch offset (`██████`) that stomped on the workspace
  box's right border on narrow windows (`w < 48`). Fixed: row visibility
  guard (`LT_HEX < rows`), dynamic positioning to `x + w - 8` when narrow,
  and clean omission if horizontal space would overlap the hex value
  text (`sw_x < min_x`).
- Added `SECURITY.md`: detailed breakdown of ctron's privilege and
  execution model, attack surface analysis (sudoers, subshells, IPC,
  arguments), and a 3-phase hardening roadmap.
- Verification: `make` warning-free (0 warnings), `make test` green,
  `make tuitest` 10 flows green (including LIGHT view activation),
  `./build/ctron --doctor` and live `./build/ctron --status` green.

## Session 2026-10-11 — M4 key toggle (GLM / Kerempkl / FA608PP)

- User field-tested the M4 binding after re-login: **works** (the
  app-component activation at session start is confirmed — that
  open item closes), but each extra press opened another
  konsole/ctron window. Fix: `scripts/ctron-toggle.sh` (pgrep/pkill
  toggle, `$TERMINAL`→konsole fallback), installed by `make install`
  to `~/.local/bin/ctron-toggle`; `ctron.desktop` `Exec` repointed to
  it + kbuildsycoca6 refresh — the kglobalshortcutsrc binding was
  never touched (same desktop id ⇒ no re-login).
- Verified: close branch with a dummy process whose comm is `ctron`
  (symlink trick — `exec -a` changes argv, NOT comm; first attempt
  missed this). Open branch got accidental live proof: the sandbox
  reaps background processes BETWEEN tool calls, the first test's
  dummy died, the toggle opened a real konsole+ctron (stray windows
  cleaned up). Sandbox lessons: keep a multi-step process test inside
  ONE tool call; guard the open branch with `TERMINAL=true` when
  testing.
- Same-day follow-up (user hit the race): double-pressing M4 opened
  TWO ctrons — the open branch races the terminal's ~1 s startup
  before the ctron process exists. Fix: atomic noclobber claim
  (`$XDG_RUNTIME_DIR/ctron-toggle.launching`); a fresh claim (<12 s)
  swallows extra presses, the launcher watches for the TUI (≤8 s) and
  clears the claim itself, claims older than 12 s are stolen (a
  killed toggle can never wedge the key). Terminal now backgrounded,
  not exec'd. **Harness lesson (again, sharper): the retest after the
  rewrite "passed" against the STALE installed copy — always
  `make install` before verifying an installed-path script; assert
  on an observable spawn log, not on exit codes, when duplicates are
  the bug.** Re-verified: double-press → 1 spawn (old script: 2),
  fresh claim → no spawn, stale claim → stolen.
- README M4 section + HARDWARE.md EN/TR now recommend the toggle.
  make/test untouched (no C change). Commit by the agent
  (path-scoped).

## Session 2026-10-10 — fan write verdict (GLM / FA608PP)

- Planned via plan mode after an Explore pass revealed the repo had
  moved (HEAD `ffabd19`): most of NEXT.md's P4 list was already fixed
  by the 10-08 line (`11aae58` — daeboard 2 s recv cap, both parser
  quirks with regression tests, pwm-enable checks). The one genuinely
  open item: `ctrl_fan_write`'s no-hwmon branch (`vok == -1`) ignored
  `a_ok` entirely — asusctl absent/errored still logged "fan curves
  written", cleared staging, returned success (CLI exit 0). The
  `fan_sysfs_dead` latch makes that branch the steady state on
  sudoers-less machines, so it mattered more than it looked.
- Landed: pure `fan_write_verdict()` in control.c/h (enum verdict;
  the plan's `enable_bad` parameter was dropped at implementation —
  verdict-irrelevant, only suppresses a duplicate log at the call
  site). Decision tree rewired onto the switch; the no-hwmon branch
  splits into ASUSCTL (honest wording, staging cleared, rc 0) and two
  FAILED forms (staging kept for retry, rc -1). Intended side effect:
  fan CLI paths exit non-zero when nothing landed.
- `check_fan_write_verdict` (11-case matrix) in test_core — note the
  file had moved too (parallel sessions): edits were re-read and
  merged, not replaced.
- Verification: `make` 0 warnings, `make test`, `make tuitest` (all
  flows — the real fan write path is unchanged), `make install`, live
  no-op `--fan-write` rc 0. The new branches are live-unreachable on
  FA608PP (hwmon + asusctl both present) — unit tests carry them;
  FA507NVR sees the new ASUSCTL wording whenever its sysfs path is
  unwritable (field note).
- Docs: NEXT.md P4 list reconciled with the code (items 2/3/5 marked
  done, item 4 mostly done — only the LIGHT swatch `x+40` overflow
  remains, cosmetic). CHANGELOG entry added.
- Commit written by the agent per the 10-09 protocol (path-scoped).

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

### Session 2026-10-04 — GPU clock lock (-lgc) replaces the dead NV rows

- User research: laptop GPU power limits are locked and the NV
  boost/temp rows do nothing observable. Two sudo probes (user-run,
  outputs in ~/gpu-probe*.out) proved on RTX 5070 / driver 615.71.09:
  `-lgc`/`-rgc` both rc=0, a 1400 lock pins current at 1395 MHz under
  load, applications-clocks QUERIES are deprecated and the
  event-reason mask ignores the lock. Harness lesson from the probe
  round: the tool's `/tmp` is an isolated namespace — scripts handed
  to the user must live in the real home; and `sudo bash` resets
  HOME=/root (output paths must be absolute).
- Landed: `ctrl_gpu_clock_lock/reset` (ut_exec_raw + "GPU clocks set
  to" check + rule-4-as-sampling via `ctrl_gpu_clock_ok`, unit-tested
  with the 1395 grid-snap case), `hw->gpu_clock_lock/gpu_mhz_max`
  (max read once at init), POWER rows: NV boost+temp REPLACED by one
  "GPU clock limit" row (h/l walk reset+800..2100, t exact,
  PW_T_GPUCLOCK staging), `gpu-clock <mhz|reset>` command key (CLI
  `--gpu-clock`, modes, .ctr) + `MS_GPUCLOCK` drift bit.
  `--nv-boost/--nv-temp` stay (FA507NVR may differ) but now verify by
  read-back ("written (read-back mismatch — may be stale or
  platform-owned)"). Doctor shows the lock state + max.
- Privileges: needs a sudoers rule for `/usr/sbin/nvidia-smi` (real
  path); without it everything fails cleanly. make 0 warnings, make
  test, make tuitest (8 flows), installed. Field test of the actual
  lock under a real game load still on the user's list.

### Same-day follow-up — load-race verify fix + unstaged-CPU-apply gate

- User field report (during an OCCT run): GPU lock FAILED under load,
  and POWER's Apply wrote the CPU clock limit although the row was
  never touched (the live limit oscillates on its own — amd-pstate
  renegotiation, documented kernel behaviour).
- Fixed: gpu_clock_verify settles 250 ms then takes up to 3 rounds of
  3 samples (a stale pre-lock high sample no longer fails an applied
  lock); CLI gpu-clock errors now name the sudoers cause; pw_apply /
  pw_diff_summary / pw_recompute_dirty gate the CPU clock limit on
  PW_T_CPUFREQ — an untouched row is displayed honestly (live drift
  visible) but Apply no longer writes it mid-load (such writes fail
  their own verification anyway while the driver pins the ceilings).
  make 0 warnings, test + tuitest (8 flows), installed.

### Same-day mini — hz validation ceiling 500 → 2000

- User spotted the hardcoded 500 in cmd_hz. It already rejected
  current hardware (540 Hz panels) and ran after the `hz max`
  computation, so `--hz max` would fail on such a panel too. The TUI
  row never went through it (real modes only). Ceiling now 2000 —
  the band is a garbage filter; ctrl_set_hz snaps to the nearest real
  mode. 15/6000 boundary-tested, no write on rejection.

### Session 2026-10-06 — CCD disable experiment resolved (docs only, GLM / FA608PP)

- The freq-watch2 sampler (nproc column + 3 min window) settled the
  earlier open CCD question (amd-pstate clock-behavior notes): under
  GPU 114 W, 16 cores sawtooth 4.3-5.4 GHz; **runtime** CPU offline
  does NOT change the firmware power policy — the machine clamped to
  the nominal 2500 MHz at the 95 °C soft limit; **BIOS-level** CCD
  disable re-configures the SMU for the true 8-core topology — CPU
  package power dropped ~80 -> 45-60 W (user's btop observation) and
  the ceiling held 5386 ~80-85 % of the time, dips shallow and
  fast-recovering. Gaming on 8C/16T single-CCD is sensible
  (cross-CCD latency gone + sustained max boost); scripting runtime
  offlining is not a substitute — BIOS change + reboot required.
  Documented in HARDWARE.md EN/TR. No code changes; sampler scripts
  live in ~ (freq-watch*.sh/out).

### Same-day follow-up — fan Write honesty + graph guides (user reports)

- User: Write said FAILED (no hwmon sudoers rule), and the graph is
  hard to read. First finding closed the 100 %-fan loop: the asusctl
  persistence calls after the failed sysfs write SUCCEEDED — asusd
  stored AND applied the curves, so a "FAILED" Write did land. That is
  how the settings.ini curve reached the EC without a successful Write
  (user: "fan eğrisini ben uygulatmamıştım" — correct, they never had
  a successful one).
- Landed: ctrl_fan_write tracks asusctl results — sysfs verified as
  before; sysfs unwritable + asusctl ok → rc=0 with an honest
  "applied via asusctl/asusd (no read-back)" log; both dead → FAILED.
  Live: `--fan-write` rc=0 here. FAN editor gained the `i` value-guide
  toggle (10 °C verticals + 25 % horizontals at 64/128/192 PWM with
  axis labels); HELP updated; flow_fan_grid (9th flow) green.
  make 0 warnings, test + tuitest (9 flows), installed.

### Same-day incident — tuitest fan Write touched the real config (fixed)

- User report after the day's heavy tuitest runs: switching to
  Performance slams the CPU fan to 100 %. Diagnosis: EC custom curve
  ramps to 255 at 71 °C; Performance pushes temps past it; the EC
  curve was byte-identical to the user's settings.ini curve —
  flow_fan_buttons' "real Write click" had been writing the user's
  staged ini curves (fan_staged since 10-03 broke the "no-op"
  premise) to the EC + asusd's per-profile store on every run.
- Fix: the flow now runs with an isolated CTRON_CONFIG (no
  settings.ini → curves = EC read at init → Write re-applies
  identical values). tuitest 8 flows green. The EC curve values are
  the user's own tuning — untouched; relief options (factory curves
  via `--fan off`, or editing the curve so 255 starts later) were
  handed to the user.

### Session 2026-10-09 — apply performance round (GLM / FA608PP)

- User: "mod değişimi 20 saniyeye yakın". Root causes: per-CPU sudo
  tee spawns (4 s per cpufreq_write_all call × several per apply),
  doomed fan sysfs attempts (34 sudo spawns per apply, no rule), and
  a useless re-assert replay under clamp. Fixes: batched tee (one
  invocation, paths from cpu_ids), fan sysfs session latch, replay
  gated on first-pass success, write storm cap (3). Also wired the
  never-implemented --mode flag. Live: mode 1.0 s, snapshot 7 s
  under clamp (vs 15), snapshot turbo→silent 4.9 s. make 0 warnings,
  tests + tuitest green, installed.

### Session 2026-10-09 — background apply worker (GLM / FA608PP)

- User ask: don't freeze the TUI during applies; show a countdown.
  Landed as ctron's first thread: write phase off-thread, live status
  line in the telemetry footer (spinner/phase/%/elapsed + reads-paused
  + q-waits), single-writer discipline (poll paused while busy),
  finalize-on-UI-thread after apply_busy drops (atomic), q never kills
  (first q queues the quit; join before settings_save). ut_log fully
  mutexed; ut_progress channel added (CLI silent). -pthread in
  CFLAGS. FAN Write intentionally still synchronous.
- Debugging gems: (1) footer shows only the NEWEST log — worker +
  finalize lines buried each other within a frame → one combined
  completion line per kind; (2) the agent sandbox DOES allow
  pthread_create (probed via ctypes) — the early flow failures were
  the burying bug + a stale "applying..." expectation, not threading.
- make 0 warnings, make test (progress + 2x200 log hammer), make
  tuitest (10 flows, async-aware), installed. Field test: quiet→
  turbo snapshot apply while watching the TUI stay alive.

### Session 2026-10-09 — the 2401 trap (GLM / FA608PP)

- User: switching to turbo from Quiet never lifted the 2401 ceiling.
  Three-layer fix (see CHANGELOG): raise-only cpu_mhz_max snapshot +
  no pre-clamp in the writers; wait-for-live-ceiling write rounds with
  two-round stability verification; profile_import re-asserts freq
  after the other steps settle. Pinwatch probe (no writes): quiet→perf
  opens cpuinfo in ~2.5 s and scaling NEVER follows on its own —
  someone must re-write; the old code wrote 2401 happily (its own
  stale clamp) and called it verified.
- Machine left on 30w-silent (quiet/2401/silent curve). make 0
  warnings, tests + tuitest green, installed.

### Session 2026-10-09 — snapshot apply fixed: curves were dead code (GLM / FA608PP)

- User switched between two real snapshots: fan curve + CPU limit did
  not change. STEPDBG instrumentation (reverted) showed fan-curve steps
  failing as "unknown key 'fan-curve cpu'" — the import branch matched
  a key the '=' split can never produce. Fixed + unit-tested with a
  hand-written snapshot (no hardware writes in tests); live-proven:
  EC table flips exactly between the two user curves.
- freq: verify now retries (3 x 0.8 s) to bridge the ~3 s ceiling
  re-open after a profile switch. The deeper behaviour reproduced
  live: after quiet->performance churn amd-pstate re-baselines all
  cores to nominal 2401 (cpuinfo 5386) — the documented 10-02 acquired
  state; reboot-only. ctron's live row shows it honestly.
- Machine left on: turbo curve EC + performance profile + clamped
  ceilings (reboot to clear, or re-apply a snapshot afterwards).

### Session 2026-10-09 — SNAPSHOTS panel renewal (GLM / FA608PP)

- User decisions: rename to SNAPSHOTS, features 1-4 (save preview,
  live diff, timestamp+note, rename), folder infrastructure ready but
  no grouping UI yet. All landed: profile.c grew profile_capture_line,
  profile_meta_parse/meta_diff (pure, unit-tested), profile_rename,
  profile_set_note, a "# saved:" header on export, '/'-grouping names
  (one level, traversal-safe), and a 1-level-deep list; the panel has
  a 3-mode typing field (save/rename/note) + summary/diff footer;
  CLI snapshot (profile alias). Meta is cached per selection change —
  the diff is draw-time int compares, zero extra polls.
- Bug found by the new flow: the global s/ESC settings shortcut ate
  's' with the panel focused — keyboard save never worked (button
  only). The focused panel now owns 's'.
- make 0 warnings, make test (check_snapshots), make tuitest (10
  flows), installed; live CLI verified (empty list, grouped export
  test/grp1, alias). Committed by the agent.

### Session 2026-10-09 — fan graph live-temp marker fix (GLM / FA608PP)

- User field report: CPU at 73 °C but the `▲` marker sat in the 90s.
  Root cause: the marker plotted the absolute temperature into the
  20-105 axis (missing `- FAN_TMIN`), unlike points/grid/labels — so
  73 °C landed at ~86 % of the width and ≥ 90 °C fell off the graph.
  One-line fix in editor_fan.c; make 0 warnings, test + tuitest
  green, installed. Committed by the agent under the new commit
  protocol.

### Session 2026-10-08 — PPT via ryzenadj + ut_exec SIGPIPE root-cause (GLM / FA608PP)

- User OCCT field test: `--ppt 30/40/50` did nothing (package hit
  80 W). Full diagnosis: legacy nb-wmi accepts-and-ignores; asus-armoury
  says ppt_*/nv_* "unavailable" (ENODEV) — firmware owns the budget;
  ryzenadj (SMU mailbox, /dev/mem as root) WORKS (user-verified: limits
  pinned under load). Also found: ctrl_ppt_limits read non-existent
  "min"/"max" attribute files (real names min_value/max_value) — real
  firmware limits were never read anywhere.
- Landed: `hw->ppt_mode` probe (armoury → ryzenadj on PATH → legacy;
  no sudo at init), common `ppt_write()` for set/off/restore, pure
  unit-tested `ctrl_ppt_ryzen_cmd()` (fixed flag order SPL→-a,
  SPPT→-c, FPPT→-b, W→mW, ints only; invocation paths /usr/bin →
  /usr/sbin → bare because sudoers matches paths LITERALLY — merged-usr
  /usr/sbin→bin symlink is invisible to it; user's manual `sudo
  ryzenadj` matched the /usr/bin rule via secure_path). doctor shows
  "ppt iface". Honesty: no read-back on Dragon Range — log says so +
  "re-apply after reboot/profile change" (SMU limits are volatile).
  Security invariant: ctron never passes other ryzenadj flags
  (no --tctl-temp); NOPASSWD-ryzenadj breadth + optional root-owned
  wrapper recipe documented in HARDWARE.md. "ppt off" now real here.
- **Debugging gem**: first live run FAILED although journal showed
  every command running as root. Isolation program → `ut_exec` with
  out=NULL returned 141 (SIGPIPE): exec_run never read the pipe when
  the caller wanted no output, so a PRINTING child (ryzenadj's
  "Successfully set ...") raced pclose and died writing to a closed
  read end — success became a fake failure. Latent forever (asusctl/
  tee/kscreen print nothing). Fix: always drain. Live proof: rc=0,
  exactly one sudo journal entry. This also makes every future
  printing tool (planned: none) safe.
- make 0 warnings, make test (incl. check_ppt_ryzen_cmd), make tuitest
  (8 flows), installed. User field protocol pending: OCCT with
  `ctron --ppt 30,40,50` (expect the ordered triple to pin ~40-50 W),
  then `--ppt off`, and after the next reboot confirm the limits
  cleared (volatility).

### Session 2026-10-07 — POWER firmware-behaviour badge (GLM / FA608PP)

- Knowledge-to-UX follow-up: the moving CPU ceiling raised "bug?"
  reports twice, so the row now carries a `· firmware` badge while the
  package temp is >= 90 °C (hysteresis 88/90, draw-time statics — the
  asusd-conflict precedent) plus one rising-edge log line. Zero extra
  reads; HELP documents it; --status deliberately left badge-free
  (one-shot temp would misread). make 0 warnings, test + tuitest
  (8 flows), installed.

### Session 2026-10-06 — util write-path hardening (P4 item 1, GLM / FA608PP)

- Quick pick from the P4 audit leftovers: `ut_write_file`'s single
  `fwrite && fflush && fclose` expression leaked the fd whenever the
  write or flush failed (the short-circuit skipped fclose). fclose now
  runs unconditionally and its result ANDs into the outcome.
  `ut_priv_write`'s sudo fallback rejects a `'` in the value or path
  with a clean -1 before any exec (callers pass fixed sysfs paths and
  plain values — no behaviour change).
- `check_util_write` in test_core: write roundtrip, the /dev/full leak
  path (/proc/self/fd counted across 50 failed writes), and both quote
  rejections (reached through an unwritable path, nothing exec'd).
  Mutation-proven: with the old code the leak check fails at ~50 leaked
  fds. P4 item 1 CLOSED in NEXT.md (both halves).
- make 0 warnings, make test, make tuitest (8 flows), installed.
  Uncommitted per protocol.

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

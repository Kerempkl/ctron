# Ctron Handoff

*Older sessions (2026-09-19 through 2026-10-30) are archived verbatim in
[docs/history/handoff-2026-09-10.md](docs/history/handoff-2026-09-10.md).*

---

## Latest session — 2026-11-26 (`v0.14.0` release: packaging, man page, shell completions, privilege helpers)

**GLM 5.4 / Kerempkl / FA608PP** — the user asked to start `v0.14.0`
(`REVIEW.md` §2.2, 4.1, 4.2, 4.4). Everything in the planned scope landed
and the release was cut. The privilege-reduction design was corrected on
the way: two of its assumptions did not survive measurement.

- **Build (`VERSION`, `Makefile`, `src/main.c`, `scripts/check-version.sh`,
  `scripts/check-help.sh`, `flake.nix`):** the version is written once, in
  the root `VERSION` file, and handed to the compiler as `-DCTRON_VERSION`
  outside `CFLAGS` (a build outside `make` says `dev`). `make install`
  honours `PREFIX`/`DESTDIR`/`BINDIR`/`MANDIR`/`DATADIR`, keeps the
  user-local default and now also installs the man page and the three
  completions; `STRIP=0`, `make uninstall`, `LDFLAGS` on the link rule.
  `make check-version` (VERSION, README badge, `flake.nix`, `PKGBUILD`,
  newest dated CHANGELOG heading) and `make check-help` (README flag block
  == `ctron --help`). `flake.nix` changed in one comment only: the literal
  `version` stays and is checked; reading `VERSION` from nix is not
  verified (no nix on this machine).
- **`man/ctron.1` and `completions/` (bash, zsh, fish), `make comptest`:**
  the man page covers every flag of `--help`; the completions cover flags,
  values, subcommands and the user's own mode / snapshot / fan-curve
  names, read straight from the config directory. They never run ctron for
  that: its subcommands initialise the hardware layer (an nvidia-smi
  query) and can create files on a fresh machine (`modes_write_defaults`,
  `profile_list` → `ut_mkdir_p`). `--dry-run` is offered for `apply` only —
  under it, `delete` acts for real (see "Found on the way").
- **Privilege helpers (`contrib/`), with the two measurements that changed
  the design:**
  1. **udev `GROUP=`/`MODE=` do not touch sysfs.** Measured on systemd 262:
     `/dev/rtc0` is `root:clock 0660` (the default rule) while
     `/sys/class/rtc/rtc0/*` stays `root:root`. The rule `REVIEW.md` §2.2
     planned could not work; the grant has to be an explicit
     `chgrp`/`chmod`.
  2. **tmpfiles runs too early for the ASUS nodes.** Same boot:
     `systemd-tmpfiles-setup` finished at 2.2 s, `asus_wmi` initialised at
     5.1 s and created the fan-curve hwmon, the platform attributes, the
     LED and the battery nodes after that. Some files are even created
     after their device's "add" event (asus-armoury's attributes, the ASUS
     battery extension's `charge_control_end_threshold`), read off the
     kernel sources (`asus_wmi_add()` order, `asus_fw_attr_add()`).

  Final shape: `ctron.tmpfiles.conf` holds the node list (`z` lines: cpufreq
  ceilings + EPP, boost, platform profile, six asus-nb-wmi attributes,
  armoury PPT `current_value`, battery threshold). `99-ctron.rules` grants
  the fan-curve hwmon's own 34 files with `find`/`chgrp`/`chmod` (per
  device: the sibling hwmon `asus` carries `pwmN_enable` with another
  meaning) and starts a one-shot `systemd-run --on-active=1s` that re-runs
  the tmpfiles file once the driver is done. The keyboard backlight is left
  alone on purpose (OpenRGB's rule opens it to everyone where installed).
  The group is one `ENV{CTRON_GROUP}="ctron"` line.
- **`contrib/ctron-ppt`, `sudoers.ctron.example`, `ctron.desktop`,
  `PKGBUILD`:** the 4.3 wrapper as a file, stricter than the snippet; the
  4.2 ruleset to copy through `visudo`; a launcher entry that `make
  install` deliberately does not place (a hand-edited
  `~/.local/share/applications/ctron.desktop` must survive); an Arch
  PKGBUILD with the unit tests as `check()`, the entry, the two optional
  units (pointed at `/usr/bin/ctron`) and the helpers under
  `/usr/share/doc/ctron/contrib`.
- **Smaller fixes:** `ctron --help` says `mode show <name>` /
  `delete <name>` (it said `<n>`; README block in step). `ctron-toggle`
  finds a packaged `ctron` on `PATH` when `~/.local/bin/ctron` is absent
  (the home copy still wins). `SECURITY.md` lost a stale note — `"no
  sudo"` gates every privileged path since `v0.10.0`, not only
  `ut_priv_write` — and README its stale sentence about PPT limits without
  BIOS values (`v0.13.0` refuses `--ppt off` there).
- **Verification:** `make` 0 warnings; `make test`; `make HARDEN=1 LTO=1`
  and `make CC=clang` 0 warnings with tests green; `clang --analyze` 0
  findings on all 23 units (the same command flags a planted bug); UBSan +
  LSan build through `CFLAGS`/`LDFLAGS` in the environment, tests and
  `--doctor` / `--status --json` / dry-runs clean; `make tuitest` all flows
  pass (exit 0); `make comptest` 3 × 61/61 (bash 5.3, zsh 5.9.2, fish
  4.9.3), each shell catching a planted mutation; `make check-version` and
  `make check-help`. Both install layouts staged through `DESTDIR` plus
  `uninstall` — which caught `install -s` being applied to the toggle
  script, fixed before any commit. `makepkg` from a local git source: 0
  warnings under the system flags, `check()` green, packaged files
  byte-identical to the tree. `udevadm verify` Success; tmpfiles dry-run on
  the live tree and real application on a scratch root including the
  `--prefix=` calls; the `find` expression matching exactly 34 files; the
  wrapper against a stand-in ryzenadj; the toggle with stubbed
  `pgrep`/`pkill` in three scenarios; `visudo -cf` parsed OK;
  `desktop-file-validate` clean. Each of the four commits before the
  release commit was exported and built on its own (`make`, `make test`
  and the checks that exist at that commit).
- **Not verified (also in `CHANGELOG.md`, `NEXT.md` 8–11, `SECURITY.md`
  4.6):** the udev rule's boot path (udev running `RUN`, the timer firing)
  needs root; `PKGBUILD`'s `git+https://…#tag=` source needs the pushed
  tag; `shellcheck` and `namcap` are not installed here; Nix is untouched
  beyond a comment (Arcioth); the GitHub `hardware` label does not exist
  yet.
- **Found on the way, left open (`NEXT.md`):** `--dry-run` is ignored by
  `snapshot|fancurve delete` and `mode add|delete`; `--ppt on` with nothing
  saved is a silent no-op; `--tui` without a terminal exits 1 where the
  no-argument path exits 2; live resize and the stdin hang-up spin are
  still in `src/ui/ui.c`.
- **Release (`v0.14.0`):** bumped `VERSION`, the `README.md` badge,
  `flake.nix` and `contrib/PKGBUILD` in one commit with the `CHANGELOG.md`
  heading, updated `NEXT.md`/`REVIEW.md`, annotated tag `v0.14.0` (local;
  push is the user's).
- **Next:** **`v0.15.0`** — offline test shield (`CTRON_SYSFS_ROOT`,
  `tui_smoke.py --offline`), CI (`make check-version`, `make check-help`
  and `make comptest` are ready to run there), the `src/hw.c` and UI module
  split. Field first: the udev boot check after a reboot (`SECURITY.md`
  4.6) and the tag-sourced `makepkg`.

Signed: **GLM 5.4** — `v0.14.0` packaging release.

---

## Previous session — 2026-11-25 (`v0.13.0` release: Power `r` revert, honest PPT limits, same-boot memory & Boot snapshot)

**Antigravity / Kerempkl / FA608PP** — closed the `1 Power` revert gaps, added honest same-boot memory (`boot_id`) for write-only PPT and GPU clock lock without auto-writing on TUI open, and added optional `Boot snapshot` selection in `,` Preferences + `ctron --apply-startup`:

- **`1 Power` / `CORE LIMITS` / `3 Display & Light` revert & honest PPT (`panel_workspace.c`, `editor_corefreq.c`, `control.{h,c}`, `cmds.c`, `panel_profiles.c`, `ui_internal.h`):**
  - Added `pw_ppt_base()` and unified `pw_revert_all()` (`hw_refresh_live` + full Power + `CORE LIMITS` revert + toast/log), wired to `r` in both `1 Power` and inline `CORE LIMITS` (Snapshot Editor's `prof_in_cores` keeps `panel_profiles_cores_reload()`).
  - When no PPT has been applied since boot and kernel read-back is unknown, `pw_ppt_base()` returns `0/0/0` so startup and `r` show honest `--` (no fake `45/55/55 W` or highlighted `Q45` pill); stepping from `--` seeds `45/55/55` and displays `-- → 45 W ●`, `e` opens an empty input instead of `"0"`, and `r` reverts cleanly back to `--`.
  - `ctrl_ppt_limits()` now returns `bool` (`true` only when all three `asus-armoury` `max_value` attributes are readable from BIOS). When BIOS returns `ENODEV` (confirmed in live `7.3.1-1-cachyos` `asus-armoury.ko.zst` — `FA608PP` is absent from its DMI table), `PPT limits` displays `-- (BIOS limits unavailable)` and both TUI (`PW_PPT_LIMITS`) and CLI (`--ppt off` / `--dry-run`) refuse instead of writing guessed `90/120/120 W`; `--doctor` / `--doctor --json` report `ppt limits` status.
  - Cleared `ctl_mode_pick` on revert, added `fmt_val_ppt()` showing `(set)` / `-- → N W ●`, fixed `PPT limits` `on → off → on` restoring baseline watts when BIOS limits exist, fixed missing `pw_recompute_dirty()` in `editor_corefreq_key()` (`Enter` exact and `a`/`A`), and added `lt_eff = 0` + `hw_refresh_live` + toast/log to `3 Display & Light` `r`.
- **Same-boot memory & `Boot snapshot` (`hw.h`, `control.c`, `settings.{h,c}`, `profile.{h,c}`, `panel_settings.c`, `ui.c`, `main.c`, `contrib/`):**
  - Added `hw->ppt_set_{valid,spl,sppt,fppt,off}` and `hw->gpu_clock_from_mem` on `hw_state_t` (updated only on `rc == 0` in `control.c` — zero worker/UI data race, never wiped by `hw_refresh_live()`, and never fabricating live `hw->ppt_spl` in `--status --json`).
  - Added `settings_boot_id()` (`/proc/sys/kernel/random/boot_id`), `g_prefs.startup_snapshot` (default `""` = `off`, strict path-traversal rejection in `settings_set_startup_snapshot()`), atomic `settings_on_snapshot_renamed()` / `settings_on_snapshot_deleted()` hooks in `profile_rename()`/`profile_delete()`, and `if (hw->cpu_mhz_limit <= 0)` guard in `settings_load()` (plus removed duplicate `settings_load()` in `ui_run()`).
  - Added `Boot snapshot` row in `,` Preferences (`off` + reloaded `.ctr` list + `(missing!)` indicator, persisted immediately), `settings_save()` in `finalize_apply()` / CLI `snapshot apply` / `--mode` / `fancurve apply`, `ctron --apply-startup [--dry-run]`, `--doctor` / `--doctor --json` reporting, `contrib/ctron-startup.service` (`--user`), and `contrib/ctron-resume@.service` (system).
- **Verification:** `make clean && make HARDEN=1 && make test` and standard `make clean && make && make test` 0 warnings (including `check_boot_memory_and_startup_snap()` and BIOS-unavailable `ppt off` checks in `tests/test_core.c`); UBSan clean; `clang --analyze` 0 findings on all modified C files; `nm -u build/ctron` 0 `popen`/`pclose`/`system` symbols; `--doctor` / `--doctor --json` valid JSON; `README.md` `--help` block byte-identical; `make tuitest` **15/15 flows (65/65 assertions) PASS** (exit 0, including clean-boot unset PPT `--` + BIOS refusal + same-boot `(set)` + `Boot snapshot`).

- **Release (`v0.13.0`):** bumped `VERSION` (`src/main.c`, `README.md`, `flake.nix`) to `0.13.0`, cut `CHANGELOG.md` `[0.13.0] — 2026-11-25`, shifted Packaging/`udev` to `v0.14.0` and Offline CI/Modular cleanup to `v0.15.0` in `NEXT.md`/`REVIEW.md`, and created annotated tag `v0.13.0` (ready for user to push).
- **Next:** **`v0.14.0`** — Packaging, `udev` + `tmpfiles.d` privilege reduction, `man/ctron.1`, shell completions (`bash`/`zsh`/`fish`), `CONTRIBUTING.md`.

Signed: **Antigravity** — `v0.13.0` release (Power `r` revert, honest PPT limits, same-boot memory & Boot snapshot).

---

## Previous session — 2026-11-25 (Fan curve subsystem overhaul & Saved Fan Curves in `4 Snapshots`)

**Antigravity / Kerempkl / FA608PP** — audited the entire fan curve subsystem end-to-end against live FA608PP hardware (`/etc/asusd/fan_curves.ron` + `asus_custom_fan_curve` `hwmon11`), fixed 9 root-cause reliability/UX bugs, and added first-class **Saved Fan Curves** (`~/.config/ctron/fan_curves/*.fc`) with a split `4 Snapshots` workspace:

- **Live diagnosis & fan curve fixes (`fan.{h,c}`, `control.c`, `hw.c`, `editor_fan.c`):**
  - Live read of the host showed the `Performance` GPU curve stuck at `temp: 61..68`, `pwm: 53..53` (~20% / 2800 RPM) from cascading editor + `asusctl` bugs:
    1. `asusctl 6.5.0` exits `0` even when printing `[ERROR asusctl]` (`< 8` points, D-Bus error); `ctrl_fan_write()` now expands curves to 8 points (`fan_to_hw8()`), refreshes live `platform_profile`, checks `asusctl` stdout/stderr via `asusctl_exec_ok()`, avoids overwriting stored RON points when a fan is set to factory auto (`pwmN_enable = 2`), bails on the first denied sysfs node instead of 34 doomed `sudo -n` spawns, and **verifies `pwm{1,2}_enable` + all 16 hwmon points on read-back** (`FAN_W_VERIFY_FAIL` returns `-1`).
    2. `fan_preset_get()` previously read `/etc/asusd/fan_curves.ron` before `fan_preset_builtin()`, so user writes overwrote the `Quiet`/`Balanced`/`Performance` presets; it now always returns uncorrupted built-in OEM reference curves (`FAN_PSRC_BUILTIN`), and `ctrl_fan_preset("stock")` genuinely sets `fan_cpu_on = fan_gpu_on = false` (EC auto).
    3. `fan_enforce_from()` / `fan_normalize()` now cascade colliding left/right neighbours outward without reordering nudged point indices (eliminating left-nudge freeze on tightly packed points), enforce monotonic non-decreasing PWM (`0..255`), and keep a 2-point minimum floor in `fan_del_point()`.
    4. Fixed `editor_fan.c` `+`/`a` duplicating `fe_x` at `T+1` (now auto-places in widest temperature gap), auto-staged `fan_*_on = true` when editing points, implemented `r` (`Revert` to live EC), preserved staged enable flags across `hw_refresh_live()`, tracked external `Fn+F5` profile + `pwm{1,2}_enable` transitions in `hw_refresh_fast()`, and added `%` axis/chip/input support.
- **Saved Fan Curves & split `4 Snapshots` pane (`profile.{h,c}`, `settings.{h,c}`, `panel_profiles.c`, `editor_fan.c`, `cmds.c`, `main.c`, `ui.c`):**
  - Added `fcurve_meta_t` & `fcurve_*` CRUD/diff/dry-run (`~/.config/ctron/fan_curves/<name>.fc`, `group/name` subdirectories, `# saved:` / `# note:`, selective CPU/GPU `— (skip)` or `off (factory auto)`).
  - Split the left pane of `4 Snapshots` into top `SNAPSHOTS` and bottom `FAN CURVES` (`f` or click switches focus). Saved fan curves support `Enter` apply (`APPLY_FAN_CURVE` worker), `s`/`n` save (also `s` inside `2 Fans`), `r` rename, `c` note, `uu` live overwrite, `dd` delete, and `e` 18-row interactive **Fan Curve Inspector & Editor** (`h`/`l` PWM, `<`/`>` °C, `e` exact `<temp> <pwm|pct%>`, `x` skip, `f` live, `m` OEM ref, `o` open in `2 Fans` graph, `w`/`Enter` save).
  - Selectable inside `EDIT SNAPSHOT` on `CPU fan curve` / `GPU fan curve` (`h`/`l` cycles OEM presets + saved curves, `a` applies to both CPU + GPU, `e` accepts curve name or CSV), inside `2 Fans` → `m` (`FAN PRESETS & SAVED CURVES`), and via CLI (`--fan <curve>`, `ctron fancurve list|apply [--dry-run]|diff|export|delete <name>`).
- **Verification:** `make clean && make HARDEN=1 && make test` and standard `make && make test` 0 warnings (including `check_fcurves_and_fan_fixes()` in `tests/test_core.c`); UBSan (`-fsanitize=undefined -fno-sanitize-recover=all`) clean; `clang --analyze` 0 findings across all 10 modified C files; `nm -u build/ctron` 0 `popen`/`pclose`/`system` symbols; `make tuitest` **14/14 flows (57/57 assertions) PASS** (exit 0).

Signed: **Antigravity** — fan curve subsystem overhaul & Saved Fan Curves.

---

## Previous session — 2026-11-25 (Snapshot Editor: PPT row edits watts by hand)

**GLM 5.4 / Kerempkl / FA608PP** — user follow-up on the widened snapshot
scope: the `PPT (SPL/SPPT/FPPT)` row stepped only through the
Q45/B60/P80/off presets, so hand-entered watts were one `h`/`l` away from
being replaced wholesale (a typed `50,60,70` snapped to B60/P80).

- **`step_field()` SF_PPT (`panel_profiles.c`):** preset cycling replaced
  with exactly `1 Power`'s stepping — all three limits ±5 W per press,
  clamped into `ctrl_ppt_limits()` windows, ordering kept by
  `ctrl_ppt_order()`; entering from `off`/omitted starts at the live
  values (seeded already) or `60/75/75`. `e` remains the manual path
  (`SPL,SPPT,FPPT`, one value = all three, `off`); `Q45`/`B60`/`P80` still
  parse as typing shortcuts, they just no longer own the stepping. Hint
  texts now lead with the watts format.
- **Verified:** 0 warnings, unit tests green; `make tuitest` all flows
  pass with a new step in `flow_snapshots` (type `62,77,77`, press `l` →
  `67/82/82`, save → `\nppt = 67,82,82\n` in the `.ctr`); screenshot
  over a seeded `52,64,70` snapshot shows `57 / 69 / 75 W` after one `l`.
- Still `[Unreleased]`; version cut remains the user's call (see the
  2026-11-24 entry below).

Signed: **GLM 5.4** — snapshot PPT row manual watt stepping.

---

## Previous session — 2026-11-24 (Snapshot scope: keyboard colour, asusd profiles, per-core grid)

**GLM 5.4 / Kerempkl / FA608PP** — user asked to widen what `4 Snapshots`
can carry ("keyboard light modes, colours, per-core frequency limits, and
the like"). Three things landed, all on existing write doors (no new
privilege):

- **What the machine really offers, measured first (asusctl 6.5.0):**
  - asusd's TUF aura interface (`xyz.ljones.Aura` on
    `/xyz/ljones/aura/tuf`) reports `SupportedBasicModes = [0]`: this
    keyboard has **one effect, `static`** — only the colour changes.
  - `asusctl aura effect breathe …` is rejected with
    `org.freedesktop.DBus.Error.NotSupported` **and still exits 0**.
    ctron trusted that status and logged `aura: breathe … ok` while the
    LEDs never changed.
  - Only `static = 0` has a proven mode number (the daemon's file names
    it while the bus reports 0). The other effects' indices are not
    documented, so nothing maps them by position.

- **Keyboard colour (`hw.{h,c}`, `control.{h,c}`, `cmds.c`, `util.{h,c}`):**
  - New read layer: `hw_aura_dbus_parse()` / `hw_aura_modes_parse()` /
    `hw_aura_ron_parse()` (pure, tested against the exact daemon output),
    `hw_aura_read()` (D-Bus first, `/etc/asusd/aura_tuf.ron` when the
    daemon is down, nothing while daeboard is up — it owns the keyboard),
    `hw_aura_mode_label()` / `hw_aura_static_only()` /
    `hw_aura_effects_label()`.
  - `ctrl_set_aura_ex()` applies effect + hex and **fails unless the
    LEDs read back** in mode and colour (3 tries, 60 ms apart);
    `ctrl_set_aura()` / `ctrl_set_aura_hex()` are its two shapes.
    `ctrl_aura_supported()` is the pure verdict shared by `--dry-run` and
    the real write, so an unsupported effect is refused without touching
    asusctl. `aura = <effect> <hex>` no longer forces `static`.
  - `ut_ascii_ieq()`: `strcasecmp()` folds `I`/`i` differently under the
    TUI's `tr_TR`, so the colour name `Ice` never matched `"ice"`.
  - `--status` gained an `Aura` line, `--status --json` a
    `keyboard.aura` object, `--doctor` the interface and effect list.

- **Snapshot model (`profile.{h,c}`):** `profile_meta_t` carries `aura`
  (`"static 00ffff"`) and `asusd_ac` / `asusd_bat` → `.ctr` lines
  `aura = …` in `[light]`, `ac-profile = …` / `battery-profile = …` in
  `[perf]`. A non-static live effect is never captured (it could not be
  replayed honestly); `profile_aura_differs()` counts only a proven mode
  change or a colour change. New pure `profile_core_rule()` applies
  CPU-ceiling rules (`<mhz>`, `core <cpu-id> <mhz>` with the SMT sibling,
  `ccd <n> <mhz>`, `max`, `clear`, joined by `,`/`;`) all-or-nothing.

- **Snapshot Editor (`panel_profiles.c`, `editor_corefreq.c`, `ui.c`,
  `ui_internal.h`):** 12 → 15 fields (Keyboard colour, Asusd AC profile,
  Asusd battery profile). On the `CPU ceiling` row, `c` opens the CORE
  LIMITS grid **in snapshot mode**: same cells and keys, `x` drops a
  thread from the file, `r` undoes to the saved list, leaving folds the
  staged threads back into `prof_edit`. It writes through its own
  `prof_cf_staged[]` pool — the first version shared `cf_staged[]` and a
  screenshot caught the rail showing `Power ● / Edit: 1 pending` for a
  snapshot edit. `e` on that row takes the joined rules above.

- **`3 Display & Light` (`panel_workspace.c`):** colour rows start from
  the live keyboard (a custom colour shows as `#rrggbb (live)` until
  touched) and a static-only keyboard says so on the `Aura effect` row
  instead of cycling through 11 effects the hardware rejects.

- **Verification:** `make clean && make HARDEN=1 && make test` and the
  standard build 0 warnings; 81 new CHECKs in `check_snapshot_scope()`
  (parsers, normalise/split, `.ctr` round-trip, diff cases, every rule
  shape incl. atomic failure); UBSan + LSan builds clean for the tests
  and the live CLI; `clang --analyze` 0 findings on the 10 changed units;
  `nm -u` 0 `popen`/`pclose`/`system`; `make tuitest` **13/13 flows (52/52
  assertions) PASS** with the extended snapshot flow (grid open, CCD staged, rule saved as 16
  per-core lines, colour stepped). Live: `--aura static 00fffe` read back
  `00fffe`, restored to `00ffff`; `--aura breathe red` refused before and
  after `--dry-run` with the LEDs unchanged. `docs/tui-snap.png` re-shot
  (15 fields) against a seeded `CTRON_SHOT_CONFIG`.

- **Notes for the next session:**
  - Every snapshot exported from live hardware now carries
    `ac-profile` / `battery-profile` (whatever asusd currently enforces,
    `off` on this machine). Applying it re-asserts that value — a
    `busctl set-property` of the same value plus the read-back, no change
    to the active profile (`-1` takes the bus path only).
  - Not added on purpose: `nv-boost` / `nv-temp` (dropped from POWER by a
    user decision), `battery-oneshot` (an action, not a state), non-static
    AURA effects (no proven mode numbers). Listed under `NEXT.md`.
  - No version cut: this sits in `[Unreleased]`; `NEXT.md` still plans
    `v0.13.0` as the packaging release, so whether this ships as its own
    MINOR is the user's call.

Signed: **GLM 5.4** — snapshot scope widening, aura read layer + verified writes.

---

## Previous session — 2026-11-24 (GitHub visual refresh: README screenshots for Konsept C)

**GLM / Kerempkl / FA608PP** — the GitHub page still carried the pre-v0.12.0
Bento screenshots (`docs/tui-*.png` dated 2026-11-19) even though `origin/main`
was current through `d4fc51e`. Re-shot every README image against the shipped
Konsept C TUI:

- **Re-shot `docs/tui-{fan,power,telem}.png`** (views `2`/`1`/`5`, 160×50,
  `Native` theme): left rail + full-height workspace, inline `CORE LIMITS`
  pane inside `1 Power`, 8-section Telemetry grid with live numbers.
- **Added `docs/tui-snap.png`** (view `4`): two-pane snapshot list + 12-field
  inspector with a `Δ N` badge and `(live: …)` diffs. Seeded an isolated
  config with `ctron snapshot export` (stock / hand-edited `gaming` /
  `quiet/office` group entry) and added an opt-in `CTRON_SHOT_CONFIG=<dir>`
  env to `scripts/tui_screenshot.py` so future shots can run against a
  prepared config dir instead of an empty throwaway.
- **README:** embedded the snapshot image in the view-`4` paragraph and fixed
  `u u` → `uu` (matches code/CHANGELOG wording).
- **Follow-up (same session): duplicated device name fixed.** The FA608PP
  firmware repeats the board name in DMI `product_name`
  (`ASUS TUF Gaming A16 FA608PP_FA608PP`), which ctron copied verbatim into
  the TUI header, `--status`/`--doctor`/`--json`, and `.ctr` `# device:`
  headers. New pure `hw_sanitize_model()` (`hw.c`/`hw.h`, declared next to
  `hw_cpu_list_parse`) collapses the repeated word-bounded trailing token;
  unit-tested in `check_model_sanitize()` (8 cases: space/underscore/whole-
  string dedupe + 5 unchanged negative cases). Also re-seeded the screenshot
  config so the `gaming` example snapshot is technically sane (user caught
  the old one: 80 W CPU PPT **and** a GPU clock clamp is contradictory) —
  now `ppt = 45,55,65`, no `gpu-clock` lock, note "GPU left at stock clocks".
  All 4 PNGs re-shot again and re-verified visually (single `FA608PP`
  everywhere, `1 Power` inline CORE LIMITS, 8-section Telemetry, Snapshots
  PPT 45/55/65 with no GPU lock row).
- **Verified:** real `--help` byte-identical to the README flag block,
  `--version` 0.12.0 = README badge, all 4 image refs resolve, each PNG
  visually inspected (Konsept C rail + workspace confirmed), script
  `py_compile` OK, `make test` green (including the new model-sanitize
  cases), `make` 0 warnings, live `--status`/`--doctor` show the single
  `FA608PP`.
- **Field note:** tags `v0.10.0`, `v0.11.0`, `v0.12.0` are still local-only —
  GitHub releases lag until they are pushed (user's call).

Signed: **GLM 5.3** — README + screenshot refresh for v0.12.0.

---

## Previous session — 2026-11-24 (Snapshot backend audit & two-pane interactive Snapshot Editor)

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

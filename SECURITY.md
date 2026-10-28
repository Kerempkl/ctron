# ctron — Security Model & Hardening Guide

This document outlines the current security model, potential attack
surfaces, and actionable hardening recommendations for ctron.

---

## 1. Current Security & Privilege Model

ctron operates primarily as an **unprivileged user-space process** (TUI or CLI).
When hardware settings need to be modified, it relies on a strict fallback chain:

```
User Action (TUI / CLI / mode / snapshot)
  └── ctrl_*() write functions (src/control.c)
        └── ut_priv_write() (src/util.c)
              ├── 1. Direct sysfs write (fopen / fwrite) -> if file is writable by user
              └── 2. sudo -n tee <path> -> passwordless sudo fallback (non-interactive)
```

Other privileged operations:
- **CPU Watt Limits (PPT):** Direct execution of `ryzenadj` via `sudo -n`
  (`ctrl_ppt_ryzen_cmd`).
- **GPU Core-Clock Lock:** Direct execution of `nvidia-smi -lgc` via `sudo -n`.
- **Keyboard Backlight / Aura:** Communicated via `asusctl` CLI or `daeboard`
  UNIX domain socket (`AF_UNIX`).

---

## 2. Threat Analysis & Attack Surfaces

### 2.1. Privilege Escalation via Sudoers (`sudo -n`)
- **Current State:** Many users run ctron on systems where `sudoers` has
  `NOPASSWD: ALL` or broad binary rules.
- **Risk:**
  - `ryzenadj` running with full root privileges can write arbitrary registers
    to the SMU mailbox (e.g., `--tctl-temp` to raise thermal cutoffs).
  - Wildcards in `sudoers` (e.g., `/usr/bin/ryzenadj *`) do not restrict argument
    boundaries, allowing malicious flags if invoked outside ctron.

### 2.2. Subshell Execution (`/bin/sh -c`)
- **Current State:** No privileged path uses a shell: `ut_priv_write`
  and the batched cpufreq `sudo -n tee` exec pure argv vectors
  (`ut_write_via_argv`), the PPT calls go through `ctrl_ppt_*_argv` +
  `ut_exec_argv`, and daeboard start/install exec argv vectors
  (`system()` is gone from the tree).
- **Residual Risk:** The remaining shell users are read-only and
  code-constant: the `asusctl`/`busctl` calls (constant or integer
  arguments) and the two `command -v` probes (`command` is a builtin).
  Their `PATH` sensitivity is inherited, but nothing user- or
  config-derived is ever parsed by a shell.
  *(Writes resolved 2026-10-26 in two passes — Phase 1.1 for
  `ut_priv_write`, then the same-day follow-up for cpufreq/PPT/daeboard;
  see Phase 2.1.)*

### 2.3. IPC & File Race Conditions
- **Current State:**
  - Singleton lock: `$XDG_RUNTIME_DIR/ctron.pid` (falls back to `/tmp/ctron-<uid>.pid`).
  - Toggle claim: `$XDG_RUNTIME_DIR/ctron-toggle.launching`.
  - `daeboard` socket: `/run/daeboard/daeboard.sock` (mode `0660`, group `wheel`).
- **Residual Risk:** If `$XDG_RUNTIME_DIR` is not set and `/tmp` is shared,
  symlink or TOCTOU attacks must be strictly prevented using `O_CREAT | O_EXCL`
  and verifying ownership.

### 2.4. Uncontrolled Configuration & File Parsing
- **Profile names:** Sanitized by `profile_sanitize_name()` to strictly allow
  `[A-Za-z0-9._-]` and safely handled `/` segment separators (`.` and `..` rejected).
- **Modes file (`modes.ini`):** Fixed buffer parsing. Stack overflow protections
  were audited on 2026-10-03 (32-entry table cap, `MODE_STEPS_MAX` truncation guards).
- **Snapshot note rewrite:** the 2026-10-28 full-tree audit found a real stack
  buffer overflow in `profile_set_note()` — a `>4 KB .ctr` drove the offset
  past the buffer because `snprintf`'s return value is the WANTED length
  (ASan-proven with a PoC against the real function). Fixed the same day
  (`note_append()` clamps the offset; oversized files are rewritten truncated,
  never out of bounds) and locked with a regression test in `tests/test_core.c`.

---

## 3. Hardening Recommendations & Roadmap

### Phase 1: Immediate Hardening (Quick Wins)

1. **Eliminate Subshell in Privileged Writes (`fork` + `execve`):**
   - **Status: DONE 2026-10-26** — `ut_write_via_argv()` (src/util.c):
     `pipe2(O_CLOEXEC)` + fork, the child dup2's the read end onto stdin
     and `execvp`s a pure argv vector — `timeout 3 sudo -n tee <path>`
     (the 3 s cap survives as an argv word, not a shell string). The
     parent survives a child that exits before reading (SIGPIPE ignored
     around the feed write; the exit status decides), and the old
     single-quote rejection is gone because nothing parses the
     arguments anymore.

2. **Deploy Least-Privilege Sudoers Wrapper for `ryzenadj`:**
   - **Status: documented 2026-10-26** — superseded by section 4:
     §4.2 is the minimal code-verified ruleset, §4.3 the stricter
     wrapper. The wrapper needs one small code follow-up (tracked in
     NEXT.md FAZ 3) before ctron itself can use it; until then it only
     tightens what other tools on the machine can do.

3. **Secure Atomic File Creation:**
   - Ensure all pidfile and timestamp claim operations use `open()` with
     `O_CREAT | O_EXCL | O_NOFOLLOW` and `0600` permissions, especially when
     falling back to `/tmp`.
   - **Status: DONE 2026-10-26** — the singleton pidfile claims with
     `O_CREAT|O_EXCL|O_NOFOLLOW, 0600` behind an lstat ownership gate;
     the toggle stamp was already symlink-safe (noclobber create,
     `rm -f` steal) and its log now refuses symlinked targets
     (`umask 077` for stamp/log/js).

---

### Phase 2: Execution & Subprocess Hardening

1. **Direct Execution for All Helpers (`ut_exec_argv`):**
   - **Status: DONE 2026-10-26** — `ut_exec_argv` / `_raw` / `_both` in
     src/util.c (fork + execvp under `timeout 3`, stdout captured with
     full drain so a chatty child cannot fake a SIGPIPE failure). Every
     `nvidia-smi` call (hw temp + max-clock reads, the lock verify
     sampler, `sudo -n … -lgc/-rgc`), every `kscreen-doctor` call
     (`-o` query + mode apply) and every `hyprctl` call (`monitors -j`,
     `eval`, `keyword` fallback) now exec argv vectors. `ut_have_cmd`
     and `nvidia_smi_bin` keep the shell only because `command -v` is a
     builtin. The same-day follow-up pass closed the privileged rest:
     cpufreq's batched `sudo -n tee` feeds through `ut_write_via_argv`
     (journal-proven one invocation for every cpu path), ryzenadj and
     the ctron-ppt wrapper build argv vectors (`ctrl_ppt_*_argv`), and
     daeboard's start/install calls plus the `--doctor` sudo probe
     dropped `system()` for `ut_exec_argv`. Remaining `ut_exec` users
     are the unprivileged asusctl/busctl calls with constant/integer
     arguments.

2. **Environment Variable Hygiene:**
   - In `ctron-toggle.sh`, sanitize `$TERMINAL` to ensure it resolves to a valid
     executable without arguments or escape sequences.
   - Ignore or sanitize `LD_PRELOAD` / `IFS` in setuid/sudo invocations.
   - **Status: DONE 2026-10-28** — the KWin helper `.js` is now created with
     `set -C` (noclobber, the stamp's rule), so a planted symlink or foreign
     regular file at the pid-based name in the shared `/tmp` fallback is
     never written through (retry on collision, else the KWin branch is
     skipped). `$TERMINAL` needs no sanitizing: it is checked with
     `command -v` and then exec'd as ONE quoted argv word — no shell ever
     parses it. `LD_PRELOAD`/`IFS` hygiene is moot: nothing in ctron runs
     setuid, and sudo scrubs the environment itself.

---

### Phase 3: Structural Privilege Separation (Long-term)

1. **Dedicated Helper / Polkit Action:**
   - Replace `sudo -n tee` with a minimal Polkit action (`org.ctron.set-profile`,
     `org.ctron.set-fan-curve`, `org.ctron.set-limits`) or a tiny systemd
     dbus/socket-activated helper daemon.
   - Allows fine-grained authorization via Polkit rules without granting
     arbitrary `sudo` privileges.

---

## 4. Recommended `/etc/sudoers.d/ctron` (code-verified, v0.3.0)

Everything here is derived from what the binary actually execs with
`sudo -n` — nothing else needs root. Apply with
`visudo -f /etc/sudoers.d/ctron` (mode 0440) and verify per §4.5.

### 4.1 What ctron runs with sudo

| Purpose | Exact invocation | Where |
|---|---|---|
| sysfs writes: fan curve points + `pwmN_enable` (asus-nb-wmi hwmon), PPT/panel-OD via asus-armoury attributes, `platform_profile`, per-core cpufreq `scaling_*` + global `boost`, charge threshold, kbd brightness | `sudo -n tee <path>` | `ut_priv_write` (util.c) |
| PPT fallback | if `/usr/local/bin/ctron-ppt` is installed and executable it is tried FIRST: `sudo -n /usr/local/bin/ctron-ppt <mW> <mW> <mW>`; otherwise `sudo -n <path> -a <mW> -c <mW> -b <mW>` — candidates in order `/usr/bin/ryzenadj`, `/usr/sbin/ryzenadj`, bare `ryzenadj`; values are integers validated in code | `ctrl_ppt_wrapper_cmd` / `ctrl_ppt_ryzen_cmd` (control.c) |
| GPU clock lock / reset | `sudo -n /usr/sbin/nvidia-smi -lgc <m>,<m>` / `-rgc` — the absolute path comes from `command -v` at runtime | `ctrl_gpu_clock_*` (control.c) |
| daeboard control (optional) | `sudo -n systemd-run --unit=daeboard --collect <bin>` and `sudo -n <install-script>` | daeboard.c |
| capability probes | `sudo -n true` | `--doctor`, daeboard |

**daeboard warning (read before adding any daeboard sudoers line):**
`db_start` execs a binary resolved from `DAEBOARD_BIN` / `~/Documents/`,
and `db_install` execs `~/Documents/daeboard/install.sh` — both are
USER-WRITABLE paths. ctron itself only execs them as argv words, but a
sudoers rule that allows them grants root to whatever sits at that path:
any local process running as your user could replace the script and run
it as root. The default §4.2 ruleset deliberately contains neither, so
out of the box these calls fail cleanly. Never add a sudoers line for
the install script; if you need the systemd-run rule, accept that it
amounts to `NOPASSWD:` for your user (the optional line is commented
out for exactly this reason).

**Merged-usr note (the footgun):** `/usr/sbin` is a symlink to
`/usr/bin` on Arch/CachyOS, so both spellings are the same file — but
sudoers matches the **literal** string ctron sends. Bare names
(`tee`, `systemd-run`) are resolved by sudo's `secure_path` (sbin
first); `nvidia-smi` arrives as whatever `command -v` printed
(`/usr/sbin/nvidia-smi` on the verified machine); `ryzenadj` arrives
as `/usr/bin/ryzenadj` (first candidate). If a write fails with
"password required" while `sudo -n true` works, the rule spelling
does not match the literal — check with `command -v` and §4.5.

### 4.2 Minimal ruleset

```sudoers
# /etc/sudoers.d/ctron — apply with visudo -f, mode 0440
Cmnd_Alias CTRON_SYSFS = /usr/sbin/tee /sys/devices/platform/asus-nb-wmi/*, \
                         /usr/sbin/tee /sys/class/firmware-attributes/asus-armoury/*, \
                         /usr/sbin/tee /sys/devices/system/cpu/*, \
                         /usr/sbin/tee /sys/firmware/acpi/platform_profile, \
                         /usr/sbin/tee /sys/class/power_supply/*, \
                         /usr/sbin/tee /sys/class/leds/*, \
                         /usr/bin/ryzenadj *, \
                         /usr/sbin/nvidia-smi -lgc *, \
                         /usr/sbin/nvidia-smi -rgc
youruser ALL=(root) NOPASSWD: CTRON_SYSFS

# optional, only if the daeboard control rows are used:
# youruser ALL=(root) NOPASSWD: /usr/sbin/systemd-run --unit=daeboard --collect *
```

Residual risk kept honest: sudoers `*` spans the whole argument
string, so `ryzenadj *` still allows *other* ryzenadj flags if typed
manually (e.g. `--tctl-temp`) — that is §2.1's escalation concern. If
your `/usr/bin/tee` spelling differs, adjust with
`command -v tee`. Kbd brightness and most LED nodes are usually
user-writable, so that line is belt-and-braces.

### 4.3 Stricter variant: the `ctron-ppt` wrapper

Root-owned, mode 0755, only ever three integers:

```sh
#!/bin/sh
# /usr/local/bin/ctron-ppt
set -eu
[ "$#" -eq 3 ] || exit 2
case "$1$2$3" in *[!0-9]*) exit 2 ;; esac
exec /usr/bin/ryzenadj -a "$1" -c "$2" -b "$3"
```

```sudoers
youruser ALL=(root) NOPASSWD: /usr/local/bin/ctron-ppt
```

Replace the `ryzenadj *` line with this and the SMU surface shrinks
to exactly three watt values. Since 2026-10-26 ctron probes
`/usr/local/bin/ctron-ppt` first (`access(X_OK)` at write time), so an
installed + allowed wrapper means ryzenadj is never invoked directly
by ctron. A missing wrapper or a denied sudo rule falls through to
the direct candidates (§4.1) — no configuration needed.

### 4.4 NixOS (FA507NVR)

Store paths change per generation, so a literal sudoers path goes
stale at the next `nixos-rebuild`. Prefer `security.sudo.extraRules`
with the generation-independent spelling
(`/run/current-system/sw/bin/...`), or package the §4.3 wrapper and
allow only that. This half is unverified on the NixOS machine (roadmap
FAZ 2, Arcioth); until it is, treat any NixOS host's sudoers state as
machine-local and re-check the rule spelling after each rebuild.

### 4.5 Verifying an install

```sh
visudo -cf /etc/sudoers.d/ctron     # syntax
sudo -n -l                          # the rules as sudo sees them
./ctron --doctor                    # "sudo -n : passwordless"
./ctron --freq <current-ceiling>    # real round-trip: writes and reads back
```

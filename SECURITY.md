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
- **Current State:** `ut_priv_write()` builds a shell command string and runs it:
  `sh -c "echo '...' | sudo -n tee '...'"` (via `popen`/`system`).
- **Mitigation in place:** `ut_priv_write` rejects single quotes (`'`) in paths
  and values prior to execution.
- **Residual Risk:** Invoking `/bin/sh` introduces shell parsing overhead,
  environment variable sensitivity (`PATH`, `IFS`), and potential injection risk
  if sanitization ever misses an edge case.
  *(Resolved 2026-10-26 for the privileged write path — `ut_priv_write`
  exec's a pure argv vector, no shell; see Phase 1.1. The read-only
  `ut_exec` helpers still use a shell until Phase 2 lands.)*

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
   - Instead of granting `NOPASSWD` to `/usr/bin/ryzenadj`, deploy a strict
     root-owned wrapper script that accepts *only* three integers:
     ```sh
     #!/bin/sh
     # /usr/local/bin/ctron-ppt
     set -eu
     [ "$#" -eq 3 ] || exit 2
     case "$1$2$3" in *[!0-9]*) exit 2;; esac
     exec /usr/bin/ryzenadj -a "$1" -c "$2" -b "$3"
     ```
   - In `/etc/sudoers.d/ctron`:
     ```sudoers
     %wheel ALL=(ALL) NOPASSWD: /usr/local/bin/ctron-ppt
     %wheel ALL=(ALL) NOPASSWD: /usr/sbin/nvidia-smi -lgc *, /usr/sbin/nvidia-smi -rgc
     %wheel ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/devices/platform/asus-nb-wmi/*, /usr/bin/tee /sys/devices/system/cpu/cpufreq/policy*/*
     ```

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
     builtin. Remaining `ut_exec` users (asusctl, ryzenadj with
     ints-only args) are intentionally left to a later pass.

2. **Environment Variable Hygiene:**
   - In `ctron-toggle.sh`, sanitize `$TERMINAL` to ensure it resolves to a valid
     executable without arguments or escape sequences.
   - Ignore or sanitize `LD_PRELOAD` / `IFS` in setuid/sudo invocations.

---

### Phase 3: Structural Privilege Separation (Long-term)

1. **Dedicated Helper / Polkit Action:**
   - Replace `sudo -n tee` with a minimal Polkit action (`org.ctron.set-profile`,
     `org.ctron.set-fan-curve`, `org.ctron.set-limits`) or a tiny systemd
     dbus/socket-activated helper daemon.
   - Allows fine-grained authorization via Polkit rules without granting
     arbitrary `sudo` privileges.

# ctron — later plans

Not built. `PLAN.md` is the v2 that is already in the tree. Nothing in
this file runs unless someone opens it on purpose.

## Fullscreen dashboard telemetry

A last-look screen: many live graphs and a full read of the machine,
plus ASCII art the user picks. It is not the default. No-args `ctron`
stays the control TUI. `--watch` stays the one-line headless line.
`--status` stays a snapshot and an exit.

Open it only when asked:

- a flag, `ctron --dash`, for a keybinding or a "show me everything" moment
- a key from the control TUI (proposed: `d`), which covers the panels
  until quit

`q` from the flag exits. `q` from the TUI returns to the control screen.
No writes on this screen. The poll loop only reads, same as `--watch`
(`g_prefs.poll_ms`).

### Graphs

Several at once, each a rolling ASCII plot (block or braille), not one
number. Keep a ring of samples in memory. Do not write a log file.

- CPU temp, GPU temp
- CPU clock
- fan RPM, cpu and gpu
- battery percent
- battery power, using `hw_fmt_power`: `dis 31.6W` while discharging,
  `chg 12.0W` while charging, `0.0W` when idle, `--` when unknown

### Details

One block next to the graphs, all read-only:

- model, CPU, kernel, distro (`/etc/os-release`)
- profile, EPP, display Hz and backend
- battery percent, status, watts, charge limit, AC
- PPT, NV boost, NV temp target, panel overdrive, CPU boost, keyboard
- fan curves, short form, cpu and gpu

Unknown stays `--`. Do not invent a number.

### ASCII art

The user chooses the picture. It is drawn once per frame beside the
graphs, small enough that the plots still fit.

1. **Their distro.** Built-in pictures for the ones we actually boot:
   NixOS, Arch, CachyOS. Detect from `os-release`, and let them override
   the guess.
2. **Or something else.** A file of their own ASCII
   (`~/.config/ctron/art.txt`, or a path they set), or a plain ctron
   mark when they want no logo.

Stored in `settings.ini`, next to the other prefs:

```ini
dash_art = distro
# distro | nixos | arch | cachyos | file | none
dash_art_file =
```

The picker is a row in the settings overlay. Startup does not ask.
Missing file or unknown name falls back to `none` and logs one line.

### First cut does not include

- starting this screen by default
- hardware writes, modes, or fan edits from the dashboard
- a new library (still notcurses only)
- history on disk
- per-core graphs (one clock and one temp is enough until the layout holds)

## daeboard client

Not built. daeboard is a separate root daemon
(`~/Documents/daeboard`, socket `/run/daeboard/daeboard.sock`). While it
is running it owns the FA507NVR keyboard color. ctron must not also call
`asusctl aura` or write `kbd_rgb_mode` in that same action.

The socket is `AF_UNIX` `SOCK_SEQPACKET`, mode `0660`, group `wheel`.
ctron stays unprivileged. One `send` is one command, at most 64 bytes.
One `recv` is the reply: `pong`, `ok`, or `err`. No new library.

Commands the daemon accepts today:

| Send | Reply | Effect |
|---|---|---|
| `ping` | `pong` | daemon is up |
| `brightness N` | `ok` | `N` is `0`–`3` |
| `color RRGGBB` | `ok` | six hex digits, sets the normal static color |
| `quit` | `ok` | daemon restores the normal color and exits |

`brightness` maps onto the existing levels: off `0`, low `1`, med `2`, high `3`.

### Steps

1. Add `src/daeboard.c` / `daeboard.h`. Connect, send, recv, close. No link to the daemon tree.
2. Probe once when a light command runs, and when the LIGHT view opens. `ping` / `pong` means use the socket. Any failure means keep `ctrl_set_kbd` and `ctrl_set_aura` as they are.
3. `ctrl_set_kbd`: if the probe worked, send `brightness` and the digit. Do not also run `asusctl leds` or the sysfs write.
4. `ctrl_set_aura_hex`, and `ctrl_set_aura` when the effect is `static`: send `color` plus the six hex digits. Do not also run `asusctl aura`.
5. Any other aura effect (breathe, rainbow, pulse, and the rest) has no socket command yet. If the probe worked, log one line and refuse. Do not fall through to asusctl, or the two writers fight.
6. Do not send `quit` from a panel or a mode. Stopping the daemon is not a light change.
7. The poll loop stays read-only. Brightness can still be read from `/sys/class/leds/asus::kbd_backlight/brightness`. `kbd_rgb_mode` is write-only; do not read it.
8. A color sent while Meta or Backspace is held is stored as the normal color and shows when that gesture ends. The TUI does not need a special case beyond not expecting the keys to change mid-hold.
9. Unit-test the bytes that would be sent (`brightness 3`, `color ccfffe`) in `tests/test_core.c`. No live socket in that test.
10. `EACCES` on the socket means the user is not in `wheel`. Log it. Do not sudo.

### First cut does not include

- editing daeboard's gestures
- reading keystrokes
- starting the daemon
- a NixOS unit
- rainbow, breathe, or pulse through the socket

## daeboard control

Not built. This is the step after the client above. ctron can start the
daemon, stop it, and edit what a key does. The daemon still owns the
keys and the LED. ctron does not read evdev.

Two kinds of action, stored in one file
`~/.config/ctron/daeboard.binds`. A light bind is a macro: a list of
color shifts and pauses, played when the key goes down and, if wanted,
when it comes up. A ctron line is a `cmd_run` pair. The same key may
have both. The root daemon never execs the ctron line.

```
[enter]
down = color 000000 120, color ccfffe 120, color 000000 120, color ccfffe 120

[backspace]
down = color ff0000 0
up = fade 2000

[leftmeta]
down = breathe 0
up = color ccfffe 0

[f6]
ctron = profile performance
```

A step is one of:

| Step | Meaning |
|---|---|
| `color RRGGBB ms` | one static write, then hold `ms` |
| `brightness N ms` | `N` is 0–3, then hold `ms` |
| `breathe ms` | firmware breathe; `0` means until the key comes up |
| `fade ms` | move from the color on screen to the resting color across `ms` |
| `rest` | the saved static color, no pause |

`ms` is the pause. `0` holds that step until the matching key-up, or
until a newer key takes the LED. The examples above are the three
gestures the daemon does today, written as macros, not special cases.

ctron is the only writer of the file. The daemon runs as root, so it
does not look at `$HOME`. It is started with
`--binds ~/.config/ctron/daeboard.binds`. After a change ctron sends
`reload`. The daemon compiles each macro into a short frame list at
reload and does not parse text when a key arrives. One `timerfd` stays
armed only while a pause is running. No malloc on the key path. A cap
of 32 steps per edge is enough for a backlight macro; longer files are
`err` at reload. Smoother fades and tighter scheduling wait until the
editor works.

A `ctron` line runs only when a user-level follower is connected.
`ctron --follow` is that follower: headless, not root, started from the
user session. The open TUI can be the follower instead, so a second
process is not required while ctron is on screen. If nobody is
listening, the light half still happens and the ctron half does not.

The daemon tells followers `fire f6`. It does not send the key code,
and it does not send keys that are not in the file.

### Why not the other two shapes

Socket-only, as the daemon is today (`color`, `brightness`, `quit`),
can start and stop and can set the resting color. It cannot rebind a
key. The gestures stay compiled in.

Letting the daemon run `ctron --profile …` itself would work with the
TUI closed and with no follower. It also makes a root process a
launcher. That stays out.

### Start, stop, install

ctron already refuses a write when `sudo -n` is missing. Same rule here.
The TUI does not compile daeboard.

How a machine gets the binary, in the order a packager should document:

1. **Install script** in the daeboard repo, for any systemd distro that
   is not NixOS. It installs the built binary and a system unit.
   Passwordless sudo only when ctron launches it.
2. **AUR** (`daeboard`, and `daeboard-git` if a VCS package is worth
   it). Not published yet. ctron's Install row can say the package name
   and stop. It does not run `yay`.
3. **NixOS**, this machine. No unit dropped into `/etc`, no
   `nixos-rebuild` from the TUI. Start is the transient unit below.
   A module under `~/Documents/nixos` stays a hand edit.
4. **Flatpak.** Not a target. The daemon needs root writes to
   `asus::kbd_backlight` and a read of the real keyboard evdev node.
   A sandbox that can do both is not a Flatpak anymore. The docs should
   say that in one paragraph so nobody files it as a missing package.

- **Start.** `sudo -n systemd-run --unit=daeboard --collect` on the
  binary path. Probe with `ping` afterwards. The path is
  `daeboard` on `PATH`, or `daeboard_bin` in `settings.ini`.
- **Stop.** One explicit control sends `quit`. Modes and profiles do
  not. `quit` restores the resting color and exits. This is the
  exception to "do not send quit" in the client section above.

### What the socket grows

`reload` is the edit path. The macro does not ride inside one command.
Still `ok` / `err`. `err` names the line when the file does not compile.

| Send | Reply |
|---|---|
| `reload` | `ok`, or `err` plus a line number |
| `color RRGGBB` | `ok`, resting color, already implemented |
| `brightness N` | `ok`, already implemented |
| `quit` | `ok`, stop, from the Stop control only |

`ping` stays the running check.

### Editor in the TUI

A row on the LIGHT view, not a new screen.

- Running or stopped, from `ping`.
- Start, Stop, and Install. Install runs the script when the binary is
  missing and sudo -n works, or names the AUR package. It does not
  compile, and it does not touch NixOS.
- Resting color and brightness.
- Per key: a down list and an up list of steps (color, brightness,
  breathe, fade, rest, each with a pause). Plus an optional `cmd_run`
  pair. Saving writes `daeboard.binds` and sends `reload`.

### First cut of this section does not include

- the daemon executing ctron
- a raw key stream on the socket
- compiling daeboard from the TUI
- `nixos-rebuild` or Flatpak
- per-key color (this keyboard is one zone)
- tightening the frame clock beyond the 32-step list

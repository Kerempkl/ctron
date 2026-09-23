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

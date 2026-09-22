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

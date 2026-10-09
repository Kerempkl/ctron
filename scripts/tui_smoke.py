#!/usr/bin/env python3
"""ctron TUI smoke harness.

Runs the real ./build/ctron TUI under a pty and drives it with keys and
mouse clicks. A responder answers notcurses's terminal queries during
the FIRST ~1 s only (late replies parse as ESC and open windows — the
lesson recorded in NEXT.md), then stays silent while still draining
output.

NOTE: the fan-editor click flow performs a real `ctrl_fan_write` (a
no-op re-write of the live curves) — it needs the same privileges as
the TUI itself. Flows that only read still run fine without sudo.

Usage:  scripts/tui_smoke.py [path-to-ctron]     exit 0 = all pass
"""

import os
import pty
import re
import select
import shutil
import signal
import subprocess
import sys
import termios
import tempfile
import threading
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/ctron"
ROWS, COLS = 50, 160
RESPONDER_WINDOW_S = 1.0
TICK = 0.05

failures = []


def fail(flow, msg):
    failures.append((flow, msg))
    print(f"  FAIL [{flow}] {msg}")


def ok(flow, msg):
    print(f"  ok   [{flow}] {msg}")


class Session:
    """One ctron process on a pty, with a bounded output buffer."""

    def __init__(self, flow):
        self.flow = flow
        self.master, slave = pty.openpty()
        termios.tcsetattr(slave, termios.TCSANOW, termios.tcgetattr(slave))
        import fcntl
        import struct
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", ROWS, COLS, 0, 0))
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env.pop("LINES", None)
        env.pop("COLUMNS", None)
        self.proc = subprocess.Popen(
            [BIN], stdin=slave, stdout=slave, stderr=slave,
            env=env, close_fds=True, preexec_fn=os.setsid)
        os.close(slave)
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.t0 = time.monotonic()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()
        # give notcurses a moment to send its queries, then answer once
        deadline = time.monotonic() + RESPONDER_WINDOW_S
        while time.monotonic() < deadline:
            time.sleep(0.02)
            self._respond()

    # ---- plumbing -------------------------------------------------------

    def _reader(self):
        while True:
            try:
                data = os.read(self.master, 65536)
            except OSError:
                return
            if not data:
                return
            with self.lock:
                self.buf += data

    def _tail(self):
        with self.lock:
            return bytes(self.buf[-256:])

    def _send(self, data):
        os.write(self.master, data)

    def _respond(self):
        """Answer notcurses's init queries. Replies follow the exact
        formats terminals use; anything unknown is left unanswered."""
        tail = self._tail()
        replies = []
        if b"\x1b[6n" in tail:                       # cursor position
            replies.append(b"\x1b[1;1R")
        if re.search(rb"\x1b\[\??[0-9]*c", tail):    # DA1
            replies.append(b"\x1b[?62;1;2;6;9;15;22c")
        if b"\x1b[>c" in tail:                       # secondary DA
            replies.append(b"\x1b[>41;361;0c")
        if b"\x1b[>0q" in tail:                      # XTVERSION
            replies.append(b"\x1bP>|tui_smoke.py\x1b\\")
        if re.search(rb"\x1b\[\??>u", tail):         # kitty keyboard
            replies.append(b"\x1b[?0u")
        if b"\x1b[?2026$p" in tail:                  # sync mode query
            replies.append(b"\x1b[?2026;2$y")
        for m in re.finditer(rb"\x1b\]4;(\d+);\?", tail):   # OSC 4 palette
            replies.append(b"\x1b]4;" + m.group(1) +
                          b";rgb:0000/0000/0000\x1b\\")
        if b"\x1b]10;?" in tail:                     # default fg
            replies.append(b"\x1b]10;rgb:c5c5/c5c5/c5c5\x1b\\")
        if b"\x1b]11;?" in tail:                     # default bg
            replies.append(b"\x1b]11;rgb:0000/0000/0000\x1b\\")
        for r in replies:
            self._send(r)

    # ---- driving ----------------------------------------------------------

    def bytes_since(self, mark):
        with self.lock:
            return bytes(self.buf[mark:])

    def mark(self):
        with self.lock:
            return len(self.buf)

    def wait_render(self, timeout=4.0):
        """Wait until output stops flowing (frame drawn)."""
        prev = -1
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            time.sleep(0.15)
            with self.lock:
                cur = len(self.buf)
            if cur == prev:
                return True
            prev = cur
        return False

    def wait_for(self, needle, timeout=4.0, since=None):
        """Wait until `needle` appears in newly emitted bytes."""
        start = self.mark() if since is None else since
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if needle in self.bytes_since(start):
                return True
            time.sleep(TICK)
        return False

    def key(self, k):
        self._send(k if isinstance(k, bytes) else k.encode())

    def click(self, col, row):
        """SGR mouse press+release at 1-based (col, row)."""
        self._send(f"\x1b[<0;{col};{row}M".encode())
        time.sleep(0.05)
        self._send(f"\x1b[<0;{col};{row}m".encode())

    def alive(self):
        return self.proc.poll() is None

    def quit_expect0(self, timeout=5.0):
        self.key(b"q")
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return None
        return self.proc.returncode

    def close(self):
        if self.alive():
            try:
                self.proc.send_signal(signal.SIGKILL)
                self.proc.wait(timeout=2)
            except Exception:
                pass
        try:
            os.close(self.master)
        except OSError:
            pass


def fan_row_geometry():
    """Control-row screen coordinates for ROWSxCOLS, matching
    ui_layout defaults (topbar 1, left 33%, telem 5)."""
    lw = max(30, min(COLS * 33 // 100, COLS - 20))
    x = lw + 2                     # first button cell (CPU)
    y = 1 + 1                      # below topbar + control row
    # CPU | space | GPU | sp | + | sp | - | sp | Write
    return {"cpu": x, "gpu": x + 4, "add": x + 8, "del": x + 10,
            "write": x + 13, "y": y + 1}   # 1-based click row


def flow_open_quit():
    s = Session("open_quit")
    try:
        if not s.wait_render(6.0):
            fail("open_quit", "no frame rendered (notcurses_init stuck?)")
            return
        if not s.alive():
            fail("open_quit", "process died during startup")
            return
        rc = s.quit_expect0()
        if rc == 0:
            ok("open_quit", "opened, rendered, q -> exit 0")
        else:
            fail("open_quit", f"q exit code {rc!r}")
    finally:
        s.close()


def flow_settings_overlay():
    s = Session("settings")
    try:
        s.wait_render()
        m = s.mark()
        s.key(b"\x1b")             # ESC opens the settings overlay
        if not s.wait_for(b"LAYOUT", since=m):
            fail("settings", "ESC did not open the settings overlay")
        else:
            ok("settings", "ESC overlay shows LAYOUT")
        rc = s.quit_expect0()
        if rc == 0:
            ok("settings", "q from overlay -> exit 0")
        else:
            fail("settings", f"q from overlay exit {rc!r}")
    finally:
        s.close()


def flow_power_stage_apply():
    s = Session("power")
    try:
        s.wait_render()
        s.key(b"3")                # focus workspace
        s.key(b"P")                # uppercase: POWER view ('p' is the
        s.key(b"j")                #   fan editor's pwm key)
        s.key(b"l")                # stage a value
        s.key(b"h")                # stage it back -> net-zero staging,
        time.sleep(0.3)            #   Apply logs but writes nothing
        m = s.mark()
        s.key(b"w")                # apply
        if s.wait_for(b"power apply", since=m, timeout=5.0):
            ok("power", "apply wrote the log line")
        else:
            fail("power", "no 'power apply' log after w")
        s.key(b"r")                # revert
        rc = s.quit_expect0()
        if rc != 0:
            fail("power", f"q exit {rc!r} after apply+revert")
        else:
            ok("power", "apply, revert, clean exit")
    finally:
        s.close()


def flow_corefreq_overlay():
    """Per-core editor overlay: opens, stages without writing, closes."""
    s = Session("corefreq")
    try:
        s.wait_render()
        s.key(b"3")                # focus workspace
        s.key(b"P")                # POWER view
        m = s.mark()
        s.key(b"c")                # open the CORE LIMITS overlay
        if not s.wait_for(b"CORE LIMITS", since=m):
            fail("corefreq", "'c' did not open the overlay")
            return
        ok("corefreq", "overlay opened from POWER")
        # topology view: this FA608PP run shows CCD headers and paired
        # thread labels (c0·16); other topologies fall back to cNN cells
        if s.wait_for(b"CCD1", since=m):
            if s.wait_for(b"c0", since=m):
                ok("corefreq", "core mode: CCD headers + core cells")
            else:
                fail("corefreq", "CCD header but no core cells")
                return
        s.key(b"j")                # move selection
        s.key(b"l")                # stage +100 (no write)
        s.key(b"l")
        s.key(b"r")                # revert staging
        m = s.mark()
        s.key(b"\x1b")             # ESC closes
        time.sleep(0.3)
        if not s.alive():
            fail("corefreq", "process died in the overlay")
            return
        ok("corefreq", "staged, reverted, closed, still alive")
        rc = s.quit_expect0()
        if rc != 0:
            fail("corefreq", f"q exit {rc!r}")
        else:
            ok("corefreq", "clean exit after overlay use")
    finally:
        s.close()


def flow_fan_buttons():
    """Regression for the ACT id collision (Write click opened Help).

    Runs against an ISOLATED CTRON_CONFIG with no settings.ini: the
    in-memory curves then equal the live EC read at init, so the Write
    click re-writes identical values (true no-op). Since fan_staged
    (2026-10-03) the old assumption "Write is a no-op re-write of the
    live curves" broke against the REAL config: settings.ini curves
    get staged at load and the click pushed them onto the EC and into
    asusd's per-profile store — observed live as the user's aggressive
    curve ramping the CPU fan to 100% under the Performance profile
    (2026-10-08)."""
    tmpcfg = tempfile.mkdtemp(prefix="ctron-tui-")
    oldcfg = os.environ.get("CTRON_CONFIG")
    os.environ["CTRON_CONFIG"] = tmpcfg
    s = None
    try:
        s = Session("fan_buttons")
        s.wait_render()
        g = fan_row_geometry()
        for name in ("cpu", "gpu", "add", "del"):
            m = s.mark()
            s.click(g[name], g["y"])
            time.sleep(0.25)
            new = s.bytes_since(m)
            if b"PRIVILEGES" in new or b"Platform profile" in new:
                fail("fan_buttons", f"click on {name} switched views")
                return
        ok("fan_buttons", "control-row clicks stay in the fan editor")
        m = s.mark()
        s.click(g["write"], g["y"])
        if s.wait_for(b"fan curve", since=m, timeout=6.0):
            ok("fan_buttons", "Write click applies the curve")
        else:
            fail("fan_buttons", "Write click did nothing (no write log)")
        rc = s.quit_expect0()
        if rc != 0:
            fail("fan_buttons", f"q exit {rc!r}")
    finally:
        if s is not None:
            s.close()
        if oldcfg is None:
            os.environ.pop("CTRON_CONFIG", None)
        else:
            os.environ["CTRON_CONFIG"] = oldcfg
        shutil.rmtree(tmpcfg, ignore_errors=True)


def last_chip(buf):
    """Last full "1:temp,pwm" point chip in the byte stream, as a
    (temp, pwm) tuple. Full chips only appear in whole-view repaints
    (initial paint, view switches) — notcurses otherwise emits diffs
    with unchanged cells (the "1:" prefix) omitted."""
    m = re.findall(rb"1:(\d+),(\d+)", buf)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def flow_fan_staging():
    """Regression: a staged fan-curve edit must survive the POWER view
    round-trip — ws_set_view(POWER) runs hw_refresh_live, which used to
    overwrite the in-memory curve from hwmon and silently drop the edit.
    Runs against an isolated CTRON_CONFIG so the real settings.ini and
    the real EC stay untouched (the edit is never written)."""
    tmpcfg = tempfile.mkdtemp(prefix="ctron-tui-")
    oldcfg = os.environ.get("CTRON_CONFIG")
    os.environ["CTRON_CONFIG"] = tmpcfg
    s = None
    try:
        s = Session("fan_staging")
        if not s.wait_render(6.0):
            fail("fan_staging", "no frame rendered")
            return
        t0 = last_chip(s.bytes_since(0))   # initial full-screen paint
        if t0 is None:
            fail("fan_staging", "no point chip found in the frame")
            return
        m = s.mark()
        s.key(b"3")                # focus workspace (FAN view by default)
        s.key(b"l")                # nudge point 1 temp +1 (stages an edit)
        time.sleep(0.5)
        # the chip diff rewrites only the changed temp digits (the "1:"
        # prefix and ",pwm" tail are unchanged), so the bare number is
        # what shows up next to the graph updates
        if f"{t0[0] + 1}".encode() not in s.bytes_since(m):
            fail("fan_staging", f"'l' nudge produced no visible change (expected {t0[0] + 1} in the diff)")
            return
        ok("fan_staging", f"point 1 staged {t0[0]} -> {t0[0] + 1}")
        m2 = s.mark()
        s.key(b"P")                # POWER view: ws_set_view -> hw_refresh_live
        if not s.wait_for(b"Platform profile", since=m2, timeout=15.0):
            fail("fan_staging", "POWER view did not render")
            return
        m3 = s.mark()
        s.key(b"F")                # back to FAN: full interior repaint
        if not s.wait_for(b"FAN CURVE", since=m3, timeout=15.0):
            fail("fan_staging", "did not return to the FAN view")
            return
        time.sleep(0.5)
        t2 = last_chip(s.bytes_since(m3))
        if t2 == (t0[0] + 1, t0[1]):
            ok("fan_staging", "staged edit survived the POWER round-trip")
        else:
            fail("fan_staging", f"staged edit lost: chip reads {t2!r}, staged {(t0[0] + 1, t0[1])}")
        s.key(b"h")                # undo the nudge before quitting
        rc = s.quit_expect0()
        if rc != 0:
            fail("fan_staging", f"q exit {rc!r}")
    finally:
        if s is not None:
            s.close()
        if oldcfg is None:
            os.environ.pop("CTRON_CONFIG", None)
        else:
            os.environ["CTRON_CONFIG"] = oldcfg
        shutil.rmtree(tmpcfg, ignore_errors=True)


def flow_view_hotkeys():
    """Regression for the view hotkeys 5..8: they must switch workspace
    views from ANY focus. The user-reported gap: p/l are taken by the
    fan editor (pwm field / nudge), f/p/l do nothing outside the
    workspace, and after an accidental 'p' the next key is swallowed by
    the typing field. The digits work everywhere and no view binds them."""
    s = Session("view_hotkeys")
    try:
        s.wait_render()
        # default focus is CONTROLS — deliberately no '3' pressed
        m = s.mark()
        s.key(b"6")
        if not s.wait_for(b"Platform profile", since=m, timeout=15.0):
            fail("views", "'6' did not open POWER from the controls focus")
            return
        ok("views", "'6' opens POWER from any focus")
        m = s.mark()
        s.key(b"5")
        if not s.wait_for(b"FAN CURVE", since=m, timeout=15.0):
            fail("views", "'5' did not open FAN")
            return
        ok("views", "'5' opens FAN")
        m = s.mark()
        s.key(b"7")
        if not s.wait_for(b"Kbd brightness", since=m, timeout=15.0):
            fail("views", "'7' did not open LIGHT")
            return
        ok("views", "'7' opens LIGHT")
        m = s.mark()
        s.key(b"8")
        if not s.wait_for(b"GLOBAL", since=m, timeout=15.0):
            fail("views", "'8' did not open HELP")
            return
        ok("views", "'8' opens HELP")
        rc = s.quit_expect0()
        if rc != 0:
            fail("views", f"q exit {rc!r}")
    finally:
        s.close()


def flow_mode_drift():
    """CONTROLS Mode row states: "- → name" before any apply, "name"
    after, "name*" once a tracked field drifts from the apply-time
    snapshot. Isolated CTRON_CONFIG with a one-step mode (profile
    quiet); the REAL profile is saved before and restored after — the
    drift step performs one real (harmless) profile write."""
    try:
        out = subprocess.run(["asusctl", "profile", "get"],
                             capture_output=True, text=True, timeout=10).stdout
    except Exception:
        out = ""
    orig = next((n for n in ("Performance", "Balanced", "Quiet") if n in out),
                None)

    tmpcfg = tempfile.mkdtemp(prefix="ctron-tui-")
    oldcfg = os.environ.get("CTRON_CONFIG")
    os.environ["CTRON_CONFIG"] = tmpcfg
    with open(os.path.join(tmpcfg, "modes.ini"), "w") as f:
        f.write("[drift]\nsteps = profile quiet\n")
    s = None
    try:
        s = Session("mode_drift")
        if not s.wait_render(6.0):
            fail("mode_drift", "no frame rendered")
            return
        m = s.mark()
        s.key(b"2")                # controls focus, Mode row selected
        s.key(b"\r")               # Enter applies the drift mode
        # the log line arrives diff-fragmented (notcurses), so wait for
        # the flash first and then for any fragment of the completion log
        if not s.wait_for(b"applying mode...", since=m, timeout=15.0):
            fail("mode_drift", "Enter did not trigger the mode apply")
            return
        if not s.wait_for(b"drift'", since=m, timeout=15.0):
            fail("mode_drift", "mode apply did not complete")
            return
        ok("mode_drift", "mode applied via the Mode row")
        m2 = s.mark()
        s.key(b"6")                # POWER: change the profile behind it
        if not s.wait_for(b"Platform profile", since=m2, timeout=15.0):
            fail("mode_drift", "POWER did not render")
            return
        s.key(b"l")                # stage the next profile
        s.key(b"w")                # apply it
        if not s.wait_for(b"power apply", since=m2, timeout=15.0):
            fail("mode_drift", "profile apply did not run")
            return
        m3 = s.mark()
        s.key(b"2")                # back to controls: full repaint
        time.sleep(0.5)
        if s.wait_for(b"drift*", since=m3, timeout=8.0):
            ok("mode_drift", "drifted mode shows name*")
        else:
            fail("mode_drift", "no 'drift*' in the Mode row after a change")
        rc = s.quit_expect0()
        if rc != 0:
            fail("mode_drift", f"q exit {rc!r}")
    finally:
        if s is not None:
            s.close()
        if oldcfg is None:
            os.environ.pop("CTRON_CONFIG", None)
        else:
            os.environ["CTRON_CONFIG"] = oldcfg
        shutil.rmtree(tmpcfg, ignore_errors=True)
        if orig:
            subprocess.run([BIN, "--profile", orig], capture_output=True,
                           timeout=30)
            print(f"  (restored profile to {orig})")
        else:
            print("  (could not parse the original profile — not restored)")


def flow_fan_grid():
    """'i' toggles the fan-graph value guides (dotted 10 °C / 25 %
    lines + axis labels). Grid dots ('·') must appear on toggle-on and
    be gone on toggle-off."""
    s = Session("fan_grid")
    try:
        s.wait_render()
        s.key(b"5")                # FAN view from the default focus
        time.sleep(0.4)
        m = s.mark()
        s.key(b"i")
        time.sleep(0.5)
        if b"\xc2\xb7" in s.bytes_since(m):
            ok("fan_grid", "guides drawn on 'i'")
        else:
            fail("fan_grid", "no guide dots after 'i'")
            return
        m = s.mark()
        s.key(b"i")
        time.sleep(0.5)
        if b"\xc2\xb7" in s.bytes_since(m):
            fail("fan_grid", "dots remained after the second 'i'")
        else:
            ok("fan_grid", "guides cleared on the second 'i'")
        rc = s.quit_expect0()
        if rc != 0:
            fail("fan_grid", f"q exit {rc!r}")
    finally:
        s.close()


def flow_snapshots():
    """SNAPSHOTS panel round-trip against an isolated CTRON_CONFIG:
    save (with preview) → rename → note → delete. No hardware writes
    (export only reads hw state; apply is never pressed)."""
    tmpcfg = tempfile.mkdtemp(prefix="ctron-tui-")
    oldcfg = os.environ.get("CTRON_CONFIG")
    os.environ["CTRON_CONFIG"] = tmpcfg
    s = None
    try:
        s = Session("snapshots")
        if not s.wait_render(6.0):
            fail("snapshots", "no frame rendered")
            return
        s.key(b"1")                # focus the snapshots panel
        m = s.mark()
        s.key(b"s")                # save mode: name typing + preview
        if not s.wait_for(b"save:", since=m, timeout=8.0):
            fail("snapshots", "'s' did not open save typing")
            return
        ok("snapshots", "save typing + preview shown")
        s.key(b"t"); s.key(b"s"); s.key(b"1")
        m = s.mark()
        s.key(b"\r")
        if not s.wait_for(b"ts1", since=m, timeout=8.0):
            fail("snapshots", "saved entry did not appear in the list")
            return
        ok("snapshots", "saved ts1 listed")
        s.key(b"r")                # rename: seeded with ts1
        time.sleep(0.2)
        for _ in range(3):
            s.key(b"\x7f")
        s.key(b"t"); s.key(b"s"); s.key(b"2")
        m = s.mark()
        s.key(b"\r")
        if not s.wait_for(b"ts2", since=m, timeout=8.0):
            fail("snapshots", "rename did not produce ts2")
            return
        ok("snapshots", "renamed to ts2")
        s.key(b"c")                # note
        time.sleep(0.2)
        s.key(b"n"); s.key(b"1")
        m = s.mark()
        s.key(b"\r")
        if not s.wait_for(b"n1", since=m, timeout=8.0):
            fail("snapshots", "note not reflected in the summary")
            return
        ok("snapshots", "note shown in summary")
        m = s.mark()
        s.key(b"d")                # delete
        if not s.wait_for(b"(none", since=m, timeout=8.0):
            fail("snapshots", "delete left the list non-empty")
            return
        ok("snapshots", "deleted; list empty again")
        rc = s.quit_expect0()
        if rc != 0:
            fail("snapshots", f"q exit {rc!r}")
    finally:
        if s is not None:
            s.close()
        if oldcfg is None:
            os.environ.pop("CTRON_CONFIG", None)
        else:
            os.environ["CTRON_CONFIG"] = oldcfg
        shutil.rmtree(tmpcfg, ignore_errors=True)


def main():
    if not os.access(BIN, os.X_OK):
        print(f"no executable at {BIN} (run make first)")
        return 2
    print(f"tui_smoke: {BIN} on {ROWS}x{COLS} pty")
    flows = [flow_open_quit, flow_settings_overlay,
             flow_power_stage_apply, flow_corefreq_overlay,
             flow_fan_buttons, flow_fan_staging, flow_view_hotkeys,
             flow_mode_drift, flow_fan_grid, flow_snapshots]
    for f in flows:
        f()
    if failures:
        print(f"\n{len(failures)} failure(s)")
        return 1
    print("\nall tui smoke flows passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

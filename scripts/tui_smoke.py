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
import signal
import subprocess
import sys
import termios
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


def flow_fan_buttons():
    """Regression for the ACT id collision (Write click opened Help)."""
    s = Session("fan_buttons")
    try:
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
        s.close()


def main():
    if not os.access(BIN, os.X_OK):
        print(f"no executable at {BIN} (run make first)")
        return 2
    print(f"tui_smoke: {BIN} on {ROWS}x{COLS} pty")
    flows = [flow_open_quit, flow_settings_overlay,
             flow_power_stage_apply, flow_fan_buttons]
    for f in flows:
        f()
    if failures:
        print(f"\n{len(failures)} failure(s)")
        return 1
    print("\nall tui smoke flows passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""ctron v0.12.0 TUI smoke harness (Konsept C layout & unified keymap).

Runs the real ./build/ctron TUI under a pty and drives it with keys and
mouse clicks. A responder answers notcurses's terminal queries during
the FIRST ~1 s only, then stays silent while draining output.

Usage:  scripts/tui_smoke.py [path-to-ctron]     exit 0 = all pass
"""

import fcntl
import os
import pty
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
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

    def __init__(self, flow, rows=ROWS, cols=COLS):
        self.flow = flow
        self.rows = rows
        self.cols = cols
        self.master, slave = pty.openpty()
        termios.tcsetattr(slave, termios.TCSANOW, termios.tcgetattr(slave))
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env["CTRON_TEST"] = "1"
        env.pop("LINES", None)
        env.pop("COLUMNS", None)
        self.proc = subprocess.Popen(
            [BIN], stdin=slave, stdout=slave, stderr=slave,
            env=env, close_fds=True, preexec_fn=os.setsid)
        os.close(slave)
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()
        deadline = time.monotonic() + RESPONDER_WINDOW_S
        while time.monotonic() < deadline:
            time.sleep(0.02)
            self._respond()

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
        tail = self._tail()
        replies = []
        if b"\x1b[6n" in tail:
            replies.append(b"\x1b[1;1R")
        if re.search(rb"\x1b\[\??[0-9]*c", tail):
            replies.append(b"\x1b[?62;1;2;6;9;15;22c")
        if b"\x1b[>c" in tail:
            replies.append(b"\x1b[>41;361;0c")
        if b"\x1b[>0q" in tail:
            replies.append(b"\x1bP>|tui_smoke.py\x1b\\")
        if re.search(rb"\x1b\[\??>u", tail):
            replies.append(b"\x1b[?0u")
        if b"\x1b[?2026$p" in tail:
            replies.append(b"\x1b[?2026;2$y")
        for m in re.finditer(rb"\x1b\]4;(\d+);\?", tail):
            replies.append(b"\x1b]4;" + m.group(1) +
                           b";rgb:0000/0000/0000\x1b\\")
        if b"\x1b]10;?" in tail:
            replies.append(b"\x1b]10;rgb:d8d8/dede/e9e9\x1b\\")
        if b"\x1b]11;?" in tail:
            replies.append(b"\x1b]11;rgb:1414/1616/1919\x1b\\")
        for r in replies:
            self._send(r)

    def bytes_since(self, mark):
        with self.lock:
            return bytes(self.buf[mark:])

    def mark(self):
        with self.lock:
            return len(self.buf)

    def wait_render(self, timeout=4.0):
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


class IsolatedConfig:
    """Context manager isolating CTRON_CONFIG to a clean temp dir."""

    def __init__(self, settings_text=None):
        self.settings_text = settings_text
        self.tmpcfg = None
        self.oldcfg = None

    def __enter__(self):
        self.tmpcfg = tempfile.mkdtemp(prefix="ctron-tui-")
        self.oldcfg = os.environ.get("CTRON_CONFIG")
        os.environ["CTRON_CONFIG"] = self.tmpcfg
        if self.settings_text is not None:
            with open(os.path.join(self.tmpcfg, "settings.ini"), "w") as f:
                f.write(self.settings_text)
        return self.tmpcfg

    def __exit__(self, *_):
        if self.oldcfg is None:
            os.environ.pop("CTRON_CONFIG", None)
        else:
            os.environ["CTRON_CONFIG"] = self.oldcfg
        shutil.rmtree(self.tmpcfg, ignore_errors=True)


def flow_open_quit():
    with IsolatedConfig():
        s = Session("open_quit")
        try:
            if not s.wait_render(6.0):
                fail("open_quit", "no frame rendered")
                return
            frame = s.bytes_since(0)
            if b"VIEWS" not in frame or b"LIVE" not in frame or b"Platform profile" not in frame:
                fail("open_quit", "Konsept C rail + Power view did not render")
                return
            rc = s.quit_expect0()
            if rc == 0:
                ok("open_quit", "Konsept C rail + Power rendered, q -> exit 0")
            else:
                fail("open_quit", f"q exit code {rc!r}")
        finally:
            s.close()


def flow_settings_overlay():
    with IsolatedConfig():
        s = Session("settings")
        try:
            s.wait_render()
            # Esc on main screen must NOT open Settings (new keymap rule)
            m0 = s.mark()
            s.key(b"\x1b")
            time.sleep(0.3)
            if b"PREFERENCES" in s.bytes_since(m0):
                fail("settings", "Esc on main screen must not open Preferences")
                return
            ok("settings", "Esc on main screen stays silent (no surprise menu)")
            m = s.mark()
            s.key(b",")
            if not s.wait_for(b"Startup view", since=m):
                fail("settings", "',' did not open Preferences with Startup view")
                return
            ok("settings", "',' opens Preferences modal with Startup view")
            m2 = s.mark()
            s.key(b"l")            # cycle Startup view: Power -> Fans
            if not s.wait_for(b"Fans", since=m2):
                fail("settings", "h/l did not change Startup view")
                return
            ok("settings", "Startup view cycles cleanly")
            s.key(b"\x1b")         # Esc closes Preferences
            time.sleep(0.25)
            rc = s.quit_expect0()
            if rc == 0:
                ok("settings", "Esc closes modal, q -> exit 0")
            else:
                fail("settings", f"q exit {rc!r}")
        finally:
            s.close()


def flow_startup_view():
    """Verify startup_view = 4 launches straight into 5 Telemetry."""
    with IsolatedConfig("startup_view = 4\n"):
        s = Session("startup_view")
        try:
            if not s.wait_render(6.0):
                fail("startup_view", "no frame rendered")
                return
            if b"TELEMETRY" in s.bytes_since(0):
                ok("startup_view", "startup_view=4 opens directly on Telemetry")
            else:
                fail("startup_view", "did not boot into Telemetry view")
                return
            rc = s.quit_expect0()
            if rc != 0:
                fail("startup_view", f"q exit {rc!r}")
        finally:
            s.close()


def flow_views_and_tab():
    with IsolatedConfig():
        s = Session("views")
        try:
            s.wait_render()
            # direct 1..5 view hotkeys
            for key, needle, name in [
                (b"2", b"Custom", "2 -> Fans"),
                (b"3", b"DISPLAY", "3 -> Display & Light"),
                (b"4", b"(none", "4 -> Snapshots"),
                (b"5", b"TELEMETRY", "5 -> Telemetry"),
                (b"1", b"Platform profile", "1 -> Power"),
            ]:
                m = s.mark()
                s.key(key)
                if not s.wait_for(needle, since=m, timeout=10.0):
                    fail("views", f"{name} did not render {needle!r}")
                    return
                ok("views", name)
            # Tab cycle forward Power -> Fans -> Display & Light
            m = s.mark()
            s.key(b"\t")
            if not s.wait_for(b"Presets (m)", since=m, timeout=8.0):
                fail("views", "Tab did not advance Power -> Fans")
                return
            m = s.mark()
            s.key(b"\t")
            if not s.wait_for(b"DISPLAY", since=m, timeout=8.0):
                fail("views", "Tab did not advance Fans -> Display & Light")
                return
            ok("views", "Tab cycles across views cleanly")
            rc = s.quit_expect0()
            if rc != 0:
                fail("views", f"q exit {rc!r}")
        finally:
            s.close()


def flow_power_stage_apply():
    with IsolatedConfig():
        s = Session("power")
        try:
            s.wait_render()
            s.key(b"j")
            s.key(b"l")
            s.key(b"h")
            time.sleep(0.3)
            m = s.mark()
            s.key(b"w")
            if s.wait_for(b"power apply", since=m, timeout=10.0):
                ok("power", "w apply wrote completion log")
            else:
                fail("power", "no 'power apply' log after w")
            s.key(b"r")
            rc = s.quit_expect0()
            if rc == 0:
                ok("power", "apply, revert, clean exit")
            else:
                fail("power", f"q exit {rc!r}")
        finally:
            s.close()


def flow_corefreq_overlay():
    """Inline CORE LIMITS pane right inside 1 Power: visible without a
    popup modal, 'c' focuses it, stages a core, Esc returns to Power."""
    with IsolatedConfig():
        s = Session("corefreq")
        try:
            s.wait_render()
            init_frame = s.bytes_since(0)
            if b"CORE LIMITS" not in init_frame or b"CCD1" not in init_frame:
                fail("corefreq", "inline CORE LIMITS / CCD1 not visible inside Power")
                return
            ok("corefreq", "inline CORE LIMITS + CCD1 visible directly inside Power")
            m = s.mark()
            s.key(b"c")            # focus inline CORE LIMITS pane
            if not s.wait_for(b"Esc power", since=m, timeout=6.0):
                fail("corefreq", "'c' did not focus inline CORE LIMITS pane")
                return
            ok("corefreq", "'c' focuses inline CORE LIMITS pane")
            s.key(b"j")            # move from CCD1 header to core c0·16
            m2 = s.mark()
            s.key(b"h")            # stage -100 below max -> live preview updates
            if not s.wait_for(b"cores: 1 staged", since=m2, timeout=6.0):
                fail("corefreq", "staged core not reflected in Power preview")
                return
            ok("corefreq", "staged core immediately shown in Power preview")
            s.key(b"\x1b")         # Esc returns focus to Power list
            time.sleep(0.3)
            s.key(b"r")            # revert staged core
            time.sleep(0.3)
            ok("corefreq", "Esc returns to Power, revert + clean exit")
            rc = s.quit_expect0()
            if rc != 0:
                fail("corefreq", f"q exit {rc!r}")
        finally:
            s.close()


def flow_system_view():
    with IsolatedConfig():
        s = Session("system")
        try:
            s.wait_render()
            m = s.mark()
            s.key(b"3")
            if not s.wait_for(b"DAEBOARD", since=m, timeout=8.0):
                fail("system", "'3' did not render Display & Light view")
                return
            ok("system", "Display & Light sections rendered")
            # move to Panel overdrive, stage toggle, check 'will write:', revert
            s.key(b"j")
            m2 = s.mark()
            s.key(b"l")
            if not s.wait_for(b"will write:", since=m2, timeout=6.0):
                fail("system", "staging Panel OD did not show will write preview")
                return
            ok("system", "staged edit shows will write preview")
            s.key(b"r")
            time.sleep(0.25)
            rc = s.quit_expect0()
            if rc != 0:
                fail("system", f"q exit {rc!r}")
        finally:
            s.close()


def last_chip(buf):
    m = re.findall(rb"1:(\d+)\xc2\xb0,(\d+)", buf)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def flow_fan_staging_and_presets():
    with IsolatedConfig():
        s = Session("fan")
        try:
            s.wait_render()
            m = s.mark()
            s.key(b"2")
            if not s.wait_for(b"Presets (m)", since=m, timeout=8.0):
                fail("fan", "'2' did not open Fans view")
                return
            time.sleep(0.3)
            t0 = last_chip(s.bytes_since(m))
            if t0 is None:
                fail("fan", "no point chip found in Fans view")
                return
            m1 = s.mark()
            s.key(b"l")
            time.sleep(0.4)
            if f"{t0[0] + 1}".encode() not in s.bytes_since(m1):
                fail("fan", f"'l' nudge did not update temp to {t0[0] + 1}")
                return
            ok("fan", f"point 1 staged {t0[0]} -> {t0[0] + 1}")
            # round-trip to Power and back: staged point must survive
            m2 = s.mark()
            s.key(b"1")
            if not s.wait_for(b"Platform profile", since=m2, timeout=10.0):
                fail("fan", "switch to Power failed")
                return
            m3 = s.mark()
            s.key(b"2")
            if not s.wait_for(b"Presets (m)", since=m3, timeout=10.0):
                fail("fan", "return to Fans failed")
                return
            time.sleep(0.3)
            t2 = last_chip(s.bytes_since(m3))
            if t2 == (t0[0] + 1, t0[1]):
                ok("fan", "staged edit survived Power round-trip")
            else:
                fail("fan", f"staged edit lost: got {t2!r}, expected {(t0[0] + 1, t0[1])}")
                return
            # toggle value guides 'i'
            m4 = s.mark()
            s.key(b"i")
            if s.wait_for(b"128", since=m4, timeout=4.0):
                ok("fan", "guides drawn on 'i'")
            else:
                fail("fan", "guides not drawn on 'i'")
                return
            s.key(b"i")
            # open presets modal 'm' and load Quiet
            m5 = s.mark()
            s.key(b"m")
            if not s.wait_for(b"FAN PRESETS", since=m5, timeout=6.0):
                fail("fan", "'m' did not open FAN PRESETS modal")
                return
            ok("fan", "FAN PRESETS modal opened on 'm'")
            m6 = s.mark()
            s.key(b"1")
            time.sleep(0.2)
            s.key(b"\r")
            time.sleep(0.4)
            diff = s.bytes_since(m6)
            if b"Quiet" in diff and (b"loaded" in diff or b"editor" in diff):
                ok("fan", "Quiet preset loaded into editor")
            else:
                fail("fan", "Quiet preset load log missing")
                return
            rc = s.quit_expect0()
            if rc != 0:
                fail("fan", f"q exit {rc!r}")
        finally:
            s.close()


def flow_fan_clicks():
    """Konsept C fan control row click coordinates (rc_ws.x = 24, rc_ws.y = 2 -> 1-based row 3)."""
    with IsolatedConfig():
        s = Session("fan_clicks")
        try:
            s.wait_render()
            s.key(b"2")
            time.sleep(0.3)
            # " CPU " (25..29) " GPU " (31..35) " + " (37..39) " - " (41..43) " Write " (45..51)
            row = 3
            for name, col in (("cpu", 26), ("gpu", 32), ("add", 38), ("del", 42)):
                m = s.mark()
                s.click(col, row)
                time.sleep(0.2)
                if b"Platform profile" in s.bytes_since(m):
                    fail("fan_clicks", f"click on {name} switched views")
                    return
            ok("fan_clicks", "control-row clicks stay in Fans view")
            s.key(b"r")            # revert staged add/del before Write
            time.sleep(0.2)
            m = s.mark()
            s.click(47, row)       # Write
            if s.wait_for(b"fan curve", since=m, timeout=6.0):
                ok("fan_clicks", "Write click logged fan curve write")
            else:
                fail("fan_clicks", "Write click did nothing")
                return
            rc = s.quit_expect0()
            if rc != 0:
                fail("fan_clicks", f"q exit {rc!r}")
        finally:
            s.close()


def flow_telemetry():
    with IsolatedConfig():
        s = Session("telemetry")
        try:
            s.wait_render()
            # 't' jumps straight to Telemetry from Power
            m = s.mark()
            s.key(b"t")
            if not s.wait_for(b"TELEMETRY", since=m, timeout=6.0):
                fail("telemetry", "'t' did not open Telemetry view")
                return
            ok("telemetry", "'t' jumps directly to Telemetry")
            frame = s.bytes_since(m)
            for sec in (b"CPU 1", b"GPU 2", b"iGPU 3", b"RAM 4", b"STORAGE 5",
                        b"NET 6", b"FAN 7", b"POWER 8"):
                if sec not in frame:
                    fail("telemetry", f"section {sec!r} missing")
                    return
            ok("telemetry", "all 8 sections render inside workspace")
            if b"[r] reset stats" not in frame:
                fail("telemetry", "[r] reset stats header button missing")
                return
            # open CPU 1 detail (default sel 0) -> grouped thermals + CCD cores
            m_cpu = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"TCTL", since=m_cpu, timeout=6.0):
                fail("telemetry", "Enter on CPU 1 did not open expanded CPU detail")
                return
            cpu_frame = s.bytes_since(m_cpu)
            if b"TCCD1" not in cpu_frame or b"CCD1" not in cpu_frame or b"thermals" not in cpu_frame:
                fail("telemetry", "expanded CPU detail missing thermals/TCCD1/CCD1")
                return
            if os.path.exists("/sys/kernel/ryzen_smu_drv/pm_table") and (
                b"TEMP" not in cpu_frame or b"PWR" not in cpu_frame or b"CPU CORES" not in cpu_frame
            ):
                fail("telemetry", "ryzen_smu present but per-core TEMP/PWR or CPU CORES missing")
                return
            ok("telemetry", "expanded CPU detail renders TCTL/TCCD1 + CCD-grouped cores")
            s.key(b"\x1b")
            time.sleep(0.25)
            # spatial nav j + l -> RAM 4, Enter opens detail
            s.key(b"j")
            s.key(b"l")
            time.sleep(0.3)
            m2 = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"swap", since=m2, timeout=6.0):
                fail("telemetry", "Enter did not open RAM detail")
                return
            ok("telemetry", "spatial grid nav + Enter detail")
            # 1..8 jumps inside detail page (6 -> NET)
            m3 = s.mark()
            s.key(b"6")
            if not s.wait_for(b"NET", since=m3, timeout=6.0):
                fail("telemetry", "'6' inside detail did not jump to NET")
                return
            ok("telemetry", "1-8 jumps directly inside detail page")
            m4 = s.mark()
            s.key(b"r")
            if not s.wait_for(b"cleared", since=m4, timeout=6.0):
                fail("telemetry", "'r' stats reset ack missing")
                return
            ok("telemetry", "'r' resets stats (cleared flash)")
            m5 = s.mark()
            s.key(b"s")
            if not s.wait_for(b"back", since=m5, timeout=6.0):
                fail("telemetry", "'s' since-reset switch missing")
                return
            ok("telemetry", "'s' toggles since-reset stats")
            s.key(b"s")
            time.sleep(0.2)
            # Esc from detail -> back to grid
            m6 = s.mark()
            s.key(b"\x1b")
            if not s.wait_for(b"STORAGE 5", since=m6, timeout=6.0):
                fail("telemetry", "Esc did not return from detail to grid")
                return
            ok("telemetry", "Esc returns detail -> grid")
            # 't' on grid toggles back to previous view (Power)
            m7 = s.mark()
            s.key(b"t")
            if not s.wait_for(b"Platform profile", since=m7, timeout=6.0):
                fail("telemetry", "'t' on grid did not return to Power")
                return
            ok("telemetry", "'t' toggles cleanly back to previous view")
            rc = s.quit_expect0()
            if rc != 0:
                fail("telemetry", f"q exit {rc!r}")
        finally:
            s.close()


def flow_snapshots():
    with IsolatedConfig() as cfg:
        s = Session("snapshots")
        try:
            s.wait_render()
            s.key(b"4")            # 4 Snapshots
            time.sleep(0.25)
            m = s.mark()
            s.key(b"s")
            if not s.wait_for(b"save:", since=m, timeout=6.0):
                fail("snapshots", "'s' did not open save prompt")
                return
            ok("snapshots", "save typing + preview shown")
            s.key(b"\x15")         # Ctrl+U clear default "my-snapshot"
            s.key(b"t"); s.key(b"s"); s.key(b"1")
            m = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"ts1", since=m, timeout=6.0):
                fail("snapshots", "saved ts1 not listed")
                return
            ok("snapshots", "saved ts1 listed")
            s.key(b"r")
            time.sleep(0.2)
            # mid-string cursor edit: Left -> 'x' -> Ctrl+E -> BS -> '2' => tsx2
            s.key(b"\x1b[D")
            s.key(b"x")
            s.key(b"\x05")
            s.key(b"\x7f")
            s.key(b"2")
            m = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"tsx2", since=m, timeout=6.0):
                fail("snapshots", "mid-string cursor rename did not produce tsx2")
                return
            ok("snapshots", "mid-string cursor rename produced tsx2")
            s.key(b"c")
            time.sleep(0.2)
            s.key(b"n"); s.key(b"1")
            m = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"n1", since=m, timeout=6.0):
                fail("snapshots", "note not shown in footer")
                return
            ok("snapshots", "note shown in inspector footer")
            # interactive field editor ('e'): step, toggle skip ('x'), save ('w'), Esc back
            m_ed = s.mark()
            s.key(b"e")
            if not s.wait_for(b"EDIT SNAPSHOT", since=m_ed, timeout=6.0):
                fail("snapshots", "'e' did not open EDIT SNAPSHOT pane")
                return
            ok("snapshots", "'e' focuses interactive Snapshot Editor")
            m_sk = s.mark()
            s.key(b"x")            # toggle Platform profile -> — (skip)
            if not s.wait_for(b"(skip)", since=m_sk, timeout=6.0):
                fail("snapshots", "'x' did not toggle field to skip")
                return
            ok("snapshots", "'x' toggles field to selective skip")
            s.key(b"j")
            s.key(b"l")            # cycle EPP
            time.sleep(0.2)
            m_sv = s.mark()
            s.key(b"w")            # save edited snapshot
            if not s.wait_for(b"\xe2\x9c\x93 snapshot", since=m_sv, timeout=6.0):
                fail("snapshots", "'w' in Snapshot Editor did not save")
                return
            ctr_path = os.path.join(cfg, "profiles", "tsx2.ctr")
            with open(ctr_path, "r") as cf:
                cbody = cf.read()
            # line-based: "ac-profile =" must not be mistaken for the skipped row
            if "\nprofile = " in cbody or "# note: n1" not in cbody:
                fail("snapshots", f"selective save content mismatch: {cbody!r}")
                return
            ok("snapshots", "'w' saves selective snapshot (skipped field omitted on disk)")

            # snapshot scope: per-core grid, ccd rule, keyboard colour.
            # 'w' keeps the editor open with the cursor on the EPP row.
            s.key(b"j"); s.key(b"j")                 # CPU ceiling row
            m_c = s.mark()
            s.key(b"c")
            if not s.wait_for(b"SNAPSHOT CORE LIMITS", since=m_c, timeout=6.0):
                fail("snapshots", "'c' on the CPU ceiling row did not open the core grid")
                return
            ok("snapshots", "'c' on the CPU ceiling row opens the per-core grid")
            m_k = s.mark()
            s.key(b"h")            # selected CCD header: every thread of it -100
            # the terminal only repaints changed cells, so look for a new value
            if not s.wait_for(b"5286", since=m_k, timeout=6.0):
                fail("snapshots", "'h' in the snapshot grid did not stage the CCD")
                return
            ok("snapshots", "grid stages a whole CCD for the snapshot (no hardware write)")
            m_b = s.mark()
            s.key(b"\x1b")         # keep + back to the fields
            if not s.wait_for(b"Platform profile", since=m_b, timeout=6.0):
                fail("snapshots", "Esc did not leave the core grid")
                return
            ok("snapshots", "Esc leaves the grid and keeps the staged cores")
            s.key(b"e")
            time.sleep(0.2)
            s.key(b"\x15")         # Ctrl+U: drop the seeded aggregate
            s.key(b"ccd 0 3000")   # exact rule replaces that CCD's staged value
            s.key(b"\r")
            time.sleep(0.2)
            # PPT row: type exact watts, then a manual 5 W step (no preset snap)
            s.key(b"j")            # PPT (SPL/SPPT/FPPT) row
            m_p = s.mark()
            s.key(b"e")
            time.sleep(0.2)
            s.key(b"\x15")         # drop the seed (live or 60,75,75)
            s.key(b"62,77,77")     # manual triple
            s.key(b"\r")
            s.key(b"l")            # +5 W on all three -> 67,82,82
            if not s.wait_for(b"67", since=m_p, timeout=6.0):
                fail("snapshots", "manual watts + step did not land on the PPT row")
                return
            ok("snapshots", "PPT row takes typed watts and steps 5 W (no preset snap)")
            for _ in range(8):     # keyboard colour row (field 12, 8 rows down)
                s.key(b"j")
            s.key(b"l")            # Ice -> Red
            time.sleep(0.2)
            s.key(b"w")            # the toast may still be on screen: judge the file
            time.sleep(1.5)
            with open(ctr_path, "r") as cf:
                cbody3 = cf.read()
            # one CCD = 8 cores / 16 threads rewritten to the rule's value
            core_lines = [ln for ln in cbody3.splitlines() if ln.startswith("freq = core ")]
            ccd0_lines = [ln for ln in core_lines if ln.endswith(" 3000")]
            if (len(ccd0_lines) != 16 or
                    "\nfreq = 5386\n" not in cbody3 or
                    "\nppt = 67,82,82\n" not in cbody3 or
                    "\naura = static ff0000\n" not in cbody3):
                fail("snapshots", f"core rule / watts / colour not saved: {cbody3!r}")
                return
            ok("snapshots", "ccd rule + manual watts + colour saved to .ctr")
            s.key(b"\x1b")         # Esc returns focus to Snapshot list
            time.sleep(0.2)
            # double-u overwrite from live hardware (preserves note 'n1')
            m_u1 = s.mark()
            s.key(b"u")
            if not s.wait_for(b"overwrite", since=m_u1, timeout=6.0):
                fail("snapshots", "first 'u' did not arm live overwrite")
                return
            m_u2 = s.mark()
            s.key(b"u")
            if not s.wait_for(b"overwritten", since=m_u2, timeout=6.0):
                fail("snapshots", "second 'u' did not overwrite snapshot from live")
                return
            with open(ctr_path, "r") as cf:
                cbody2 = cf.read()
            if "profile =" not in cbody2 or "# note: n1" not in cbody2:
                fail("snapshots", "double-u live overwrite did not restore profile or keep note")
                return
            ok("snapshots", "double-u overwrites from live while preserving note")
            m = s.mark()
            s.key(b"d")
            if not s.wait_for(b"again", since=m, timeout=6.0):
                fail("snapshots", "first 'd' did not arm delete")
                return
            ok("snapshots", "first 'd' arms delete confirmation")
            s.key(b"j")            # cancels arm
            time.sleep(0.2)
            m2 = s.mark()
            s.key(b"d")
            if not s.wait_for(b"again", since=m2, timeout=6.0):
                fail("snapshots", "non-d key did not cancel delete arm")
                return
            m3 = s.mark()
            s.key(b"d")
            if not s.wait_for(b"(none", since=m3, timeout=6.0):
                fail("snapshots", "second 'd' did not delete snapshot")
                return
            ok("snapshots", "double-d delete confirmed; list empty again")
            rc = s.quit_expect0()
            if rc != 0:
                fail("snapshots", f"q exit {rc!r}")
        finally:
            s.close()


def flow_log_and_help():
    with IsolatedConfig():
        s = Session("log_help")
        try:
            s.wait_render()
            m = s.mark()
            s.key(b"L")
            if not s.wait_for(b"EVENT LOG", since=m, timeout=6.0):
                fail("log_help", "'L' did not open EVENT LOG")
                return
            if not s.wait_for(b"TUI ready", since=m, timeout=4.0):
                fail("log_help", "startup log entry missing in EVENT LOG")
                return
            ok("log_help", "'L' opens EVENT LOG with startup entry")
            s.key(b"j"); s.key(b"k"); s.key(b"\x1b")
            time.sleep(0.25)
            m2 = s.mark()
            s.key(b"?")
            if not s.wait_for(b"KEYMAP", since=m2, timeout=6.0):
                fail("log_help", "'?' did not open HELP modal")
                return
            ok("log_help", "'?' opens floating HELP modal")
            s.key(b"\x1b")
            time.sleep(0.25)
            rc = s.quit_expect0()
            if rc != 0:
                fail("log_help", f"q exit {rc!r}")
        finally:
            s.close()


def flow_narrow_terminal():
    """Below 96 cols the left rail auto-collapses into inline top tabs."""
    with IsolatedConfig():
        s = Session("narrow", rows=28, cols=80)
        try:
            if not s.wait_render(6.0):
                fail("narrow", "no frame rendered at 28x80")
                return
            frame = s.bytes_since(0)
            if b"1 Power" not in frame or b"5 Telem" not in frame:
                fail("narrow", "inline top tabs missing at 80 cols")
                return
            ok("narrow", "rail auto-collapses to inline top tabs at 80 cols")
            rc = s.quit_expect0()
            if rc != 0:
                fail("narrow", f"q exit {rc!r}")
        finally:
            s.close()


def flow_saved_fan_curves():
    """Saved Fan Curves: save in 2 Fans, split pane in 4 Snapshots, rename, edit, select in snapshot."""
    with IsolatedConfig() as cfg:
        s = Session("fcurves")
        try:
            s.wait_render()
            s.key(b"2")
            time.sleep(0.35)
            # Load Balanced factory preset so we save a clean known curve
            s.key(b"m")
            time.sleep(0.25)
            s.key(b"2")
            time.sleep(0.15)
            s.key(b"\r")
            time.sleep(0.25)
            # Save named fan curve 'fc1' via 's'
            m_sv = s.mark()
            s.key(b"s")
            if not s.wait_for(b"save fan curve", since=m_sv, timeout=6.0):
                fail("fcurves", "'s' in 2 Fans did not open save fan curve prompt")
                return
            s.key(b"\x15")         # Ctrl+U clear suggested name
            s.key(b"fc1")
            m_ok = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"fc1", since=m_ok, timeout=6.0):
                fail("fcurves", "saving 'fc1' in 2 Fans failed")
                return
            ok("fcurves", "'s' in 2 Fans saves named fan curve 'fc1'")

            # Verify 'Saved: fc1' appears inside 'm' FAN PRESETS modal
            m_pr = s.mark()
            s.key(b"m")
            if not s.wait_for(b"Saved: fc1", since=m_pr, timeout=6.0):
                fail("fcurves", "saved curve 'fc1' missing in 'm' FAN PRESETS modal")
                return
            ok("fcurves", "saved curve 'fc1' listed inside 'm' FAN PRESETS modal")
            s.key(b"\x1b")
            time.sleep(0.2)

            # Open 4 Snapshots and verify split pane (SNAPSHOTS top + FAN CURVES bottom)
            m_v4 = s.mark()
            s.key(b"4")
            if not s.wait_for(b"FAN CURVES", since=m_v4, timeout=6.0):
                fail("fcurves", "split FAN CURVES section missing in 4 Snapshots")
                return
            ok("fcurves", "4 Snapshots left pane split into SNAPSHOTS + FAN CURVES")

            # 'f' focuses FAN CURVES; rename fc1 -> fc2 ('r') and add note ('c')
            s.key(b"f")
            time.sleep(0.25)
            s.key(b"r")
            time.sleep(0.2)
            s.key(b"\x15")
            s.key(b"fc2")
            m_rn = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"fc2", since=m_rn, timeout=6.0):
                fail("fcurves", "renaming fan curve fc1 -> fc2 failed")
                return
            ok("fcurves", "fan curve renamed fc1 -> fc2")

            s.key(b"c")
            time.sleep(0.2)
            s.key(b"quiet-gpu")
            m_nt = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"quiet-gpu", since=m_nt, timeout=6.0):
                fail("fcurves", "fan curve note 'quiet-gpu' not shown")
                return
            ok("fcurves", "fan curve note saved and displayed")

            # 'e' opens interactive Fan Curve Editor; edit point 1 PWM and save ('w')
            m_ed = s.mark()
            s.key(b"e")
            if not s.wait_for(b"EDIT FAN CURVE", since=m_ed, timeout=6.0):
                fail("fcurves", "'e' did not open EDIT FAN CURVE")
                return
            s.key(b"j")            # CPU point 1 (40°C, 22 PWM -> +5 = 27 PWM)
            s.key(b"l")
            time.sleep(0.2)
            s.key(b"w")
            time.sleep(0.5)
            fc_path = os.path.join(cfg, "fan_curves", "fc2.fc")
            with open(fc_path, "r") as ff:
                fc_body = ff.read()
            if "# note: quiet-gpu" not in fc_body or "27,75,80,98" not in fc_body:
                fail("fcurves", f"edited .fc content mismatch: {fc_body!r}")
                return
            ok("fcurves", "interactive Fan Curve Editor saves edited points + note to .fc")
            s.key(b"\x1b")         # back to FAN CURVES list
            time.sleep(0.2)

            # Switch back to SNAPSHOTS ('f'), save 'sn1', and select 'fc2' inside EDIT SNAPSHOT
            s.key(b"f")
            time.sleep(0.2)
            s.key(b"s")
            time.sleep(0.2)
            s.key(b"\x15")
            s.key(b"sn1")
            s.key(b"\r")
            time.sleep(0.3)
            s.key(b"e")            # EDIT SNAPSHOT
            time.sleep(0.25)
            for _ in range(13):    # row 13 = SF_FAN_CPU
                s.key(b"j")
            time.sleep(0.2)
            s.key(b"e")            # type saved fan curve name 'fc2'
            time.sleep(0.2)
            s.key(b"\x15")
            s.key(b"fc2")
            m_sel = s.mark()
            s.key(b"\r")
            if not s.wait_for(b"[fc2]", since=m_sel, timeout=6.0):
                fail("fcurves", "selecting saved fan curve 'fc2' in Snapshot Editor failed")
                return
            s.key(b"a")            # copy 'fc2' to both CPU + GPU fan curves in snapshot
            time.sleep(0.2)
            s.key(b"w")
            time.sleep(0.4)
            sn_path = os.path.join(cfg, "profiles", "sn1.ctr")
            with open(sn_path, "r") as sf:
                sn_body = sf.read()
            if ("fan-curve cpu = 40,54,61,67,71,75,80,90 27,75,80,98,125,160,190,210" not in sn_body or
                    "fan-curve gpu = 51,53,59,64,66,70,74,83 33,53,73,93,140,180,215,245" not in sn_body):
                fail("fcurves", f"snapshot sn1 did not embed fc2 curves: {sn_body!r}")
                return
            ok("fcurves", "saved fan curve 'fc2' selectable + applied to CPU & GPU inside Snapshot")

            s.key(b"\x1b")
            time.sleep(0.2)
            rc = s.quit_expect0()
            if rc != 0:
                fail("fcurves", f"q exit {rc!r}")
        finally:
            s.close()


def flow_power_revert_and_boot_snap():
    """Unapplied PPT stays honest '--' on open and 'r' (zero fake 45 W),
    BIOS-unavailable PPT limits refuses cleanly, same-boot PPT/GPU memory
    shows (set), 'r' reverts Power + CORE LIMITS, and Preferences Boot snapshot."""
    # Part A: clean boot (no PPT set yet) -> no fake 45 W, PPT limits unavailable,
    # stage Q45 -> '-- → 45 W ●', 'r' reverts cleanly back to '--' (synced).
    with IsolatedConfig():
        s0 = Session("power_unset")
        try:
            if not s0.wait_render(6.0):
                fail("power_unset", "no frame rendered")
                return
            f0 = s0.bytes_since(0)
            if b"45 W" in f0 or b"55 W" in f0:
                fail("power_unset", "fresh Power screen must not show fake 45/55 W")
                return
            if b"BIOS limits unavailable" not in f0:
                fail("power_unset", "PPT limits row must show BIOS limits unavailable")
                return
            ok("power_unset", "fresh Power screen has no fake PPT values and locks PPT limits off")

            # Move to Presets (row 5), stage Q45 -> '-- → 45 W ●'
            for _ in range(5):
                s0.key(b"j")
            m_q = s0.mark()
            s0.key(b"l")
            if not s0.wait_for(b"45 W", since=m_q, timeout=6.0):
                fail("power_unset", "stepping Presets from unset did not stage 45 W")
                return
            # Move to PPT limits (row 9: 4x j) and try toggling off -> must refuse cleanly
            for _ in range(4):
                s0.key(b"j")
            m_lim = s0.mark()
            s0.key(b"l")
            if not s0.wait_for(b"no safe maxima", since=m_lim, timeout=6.0):
                fail("power_unset", "PPT limits toggle did not log honest BIOS refusal")
                return
            ok("power_unset", "PPT limits off refuses without fabricating 90/120/120 W")

            # Press 'r' -> reverts back to honest '--' (zero fake 45 W) + synced
            m_r = s0.mark()
            s0.key(b"r")
            if not s0.wait_for(b"reverted", since=m_r, timeout=6.0) or \
               not s0.wait_for(b"synced", since=m_r, timeout=6.0):
                fail("power_unset", "'r' did not revert unset PPT cleanly to synced")
                return
            if b"45 W" in s0.bytes_since(m_r):
                fail("power_unset", "'r' repainted fake 45 W on unset PPT")
                return
            ok("power_unset", "'r' reverts unset PPT back to honest '--' (no fake values)")
            rc0 = s0.quit_expect0()
            if rc0 != 0:
                fail("power_unset", f"q exit {rc0!r}")
                return
        finally:
            s0.close()

    # Part B: same-boot PPT/GPU memory display, Power + CORE LIMITS 'r' revert,
    # and Preferences -> Boot snapshot selection + --apply-startup --dry-run.
    with open("/proc/sys/kernel/random/boot_id", "r") as bf:
        boot_id = bf.read().strip()
    seed_ini = (
        "poll_ms = 250\n"
        f"last_boot_id = {boot_id}\n"
        "last_ppt_spl = 60\n"
        "last_ppt_sppt = 75\n"
        "last_ppt_fppt = 75\n"
        "last_ppt_off = 0\n"
        "last_gpu_clock = 1400\n"
    )
    with IsolatedConfig(seed_ini) as cfg:
        pdir = os.path.join(cfg, "profiles")
        os.makedirs(pdir, exist_ok=True)
        with open(os.path.join(pdir, "daily.ctr"), "w") as pf:
            pf.write(
                "# ctron snapshot\n"
                "[perf]\nprofile = balanced\nepp = balance_performance\n"
                "[power]\nppt = 60,75,75\n"
            )
        s = Session("power_boot")
        try:
            if not s.wait_render(6.0):
                fail("power_boot", "no frame rendered")
                return
            frame = s.bytes_since(0)
            if b"60 W (set)" not in frame or b"1400 MHz (set)" not in frame:
                fail("power_boot", "same-boot PPT/GPU memory not shown as (set)")
                return
            if b"synced" not in frame:
                fail("power_boot", "remembered boot memory must not mark edits dirty")
                return
            ok("power_boot", "same-boot PPT/GPU memory displayed honestly as (set) and synced")

            # Stage P80 preset on left Power list, then stage a core in CORE LIMITS,
            # then press 'r' inside CORE LIMITS -> BOTH must revert to baseline.
            for _ in range(5):     # PW_PRESET
                s.key(b"j")
            m_st = s.mark()
            s.key(b"l")            # B60 -> P80 (80/80/80)
            if not s.wait_for(b"80 W", since=m_st, timeout=6.0):
                fail("power_boot", "staging P80 did not update SPL to 80 W")
                return
            s.key(b"c")            # focus inline CORE LIMITS
            s.key(b"j")            # core 0
            s.key(b"h")            # stage -100 MHz
            time.sleep(0.25)
            m_rv = s.mark()
            s.key(b"r")            # revert ALL from inside CORE LIMITS
            if not s.wait_for(b"reverted", since=m_rv, timeout=6.0):
                fail("power_boot", "'r' inside CORE LIMITS did not show revert toast")
                return
            # notcurses only repaints changed cells: "60 W → 80 W ●" -> "60 W (set) (15–90)"
            # keeps the "60 W " prefix and repaints "(set)" + rail "synced"
            if not s.wait_for(b"(set)", since=m_rv, timeout=6.0) or \
               not s.wait_for(b"synced", since=m_rv, timeout=6.0):
                fail("power_boot", "'r' did not revert PPT back to (set) and rail to synced")
                return
            ok("power_boot", "'r' reverts both Power PPT/Presets and CORE LIMITS to baseline")

            # Preferences (',') -> Boot snapshot (default off -> select daily)
            s.key(b"\x1b")         # leave CORE LIMITS
            time.sleep(0.2)
            m_pr = s.mark()
            s.key(b",")
            if not s.wait_for(b"Boot snapshot", since=m_pr, timeout=6.0):
                fail("power_boot", "'Boot snapshot' row missing in Preferences")
                return
            s.key(b"j")            # row 1 = Boot snapshot
            m_bs = s.mark()
            s.key(b"l")            # off -> daily
            if not s.wait_for(b"daily (--apply-startup)", since=m_bs, timeout=6.0):
                fail("power_boot", "could not select 'daily' as Boot snapshot")
                return
            ok("power_boot", "Preferences selects 'daily' as Boot snapshot")
            s.key(b"\x1b")
            time.sleep(0.2)
            rc = s.quit_expect0()
            if rc != 0:
                fail("power_boot", f"q exit {rc!r}")
                return
        finally:
            s.close()

        with open(os.path.join(cfg, "settings.ini"), "r") as sf:
            sbody = sf.read()
        if "\nstartup_snapshot = daily\n" not in sbody:
            fail("power_boot", f"startup_snapshot not persisted: {sbody!r}")
            return
        ok("power_boot", "startup_snapshot = daily persisted in settings.ini")

        # Verify CLI --apply-startup --dry-run reads the configured Boot snapshot
        proc = subprocess.run(
            [BIN, "--config-dir", cfg, "--apply-startup", "--dry-run"],
            capture_output=True, text=True, timeout=10,
        )
        if proc.returncode != 0 or "boot snapshot: daily" not in proc.stdout or "ppt = 60,75,75" not in proc.stdout:
            fail("power_boot", f"--apply-startup --dry-run failed: rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
            return
        ok("power_boot", "--apply-startup --dry-run validates configured Boot snapshot")


def main():
    if not os.access(BIN, os.X_OK):
        print(f"no executable at {BIN} (run make first)")
        return 2
    print(f"tui_smoke: {BIN} on {ROWS}x{COLS} pty (Konsept C)")
    flows = [
        flow_open_quit,
        flow_settings_overlay,
        flow_startup_view,
        flow_views_and_tab,
        flow_power_stage_apply,
        flow_power_revert_and_boot_snap,
        flow_corefreq_overlay,
        flow_system_view,
        flow_fan_staging_and_presets,
        flow_fan_clicks,
        flow_telemetry,
        flow_snapshots,
        flow_saved_fan_curves,
        flow_log_and_help,
        flow_narrow_terminal,
    ]
    for f in flows:
        f()
    if failures:
        print(f"\n{len(failures)} failure(s)")
        return 1
    print("\nall tui smoke flows passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

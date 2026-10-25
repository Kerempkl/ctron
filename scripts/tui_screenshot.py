#!/usr/bin/env python3
"""Capture ctron TUI screenshots (e.g. for the README's docs/*.png).

Runs the real TUI on a pty (Session adapted from scripts/tui_smoke.py),
replays the captured ANSI stream through a small terminal emulator and
renders the final screen grid to PNG with PIL. Screenshots run against
an isolated CTRON_CONFIG, so the real settings.ini is never touched.

Run from the repo root (BIN is ./build/ctron):

  python3 scripts/tui_screenshot.py docs/tui-fan.png 160 50
  python3 scripts/tui_screenshot.py docs/tui-power.png 160 50 6
  python3 scripts/tui_screenshot.py out.png 160 50 '5;sleep;m'

The optional 4th arg is a key script: ';'-separated steps, each either
'sleep' or a key string sent to the TUI.
"""

import os
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
import fcntl

from PIL import Image, ImageDraw, ImageFont

BIN = "./build/ctron"

# ---------------------------------------------------------------- session

class Session:
    def __init__(self, cols, rows):
        self.master, slave = pty.openpty()
        termios.tcsetattr(slave, termios.TCSANOW, termios.tcgetattr(slave))
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env["COLORTERM"] = "truecolor"
        env["CTRON_TEST"] = "1"
        env.pop("LINES", None)
        env.pop("COLUMNS", None)
        self.proc = subprocess.Popen(
            [BIN], stdin=slave, stdout=slave, stderr=slave,
            env=env, close_fds=True, preexec_fn=os.setsid)
        os.close(slave)
        self.buf = bytearray()
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()
        deadline = time.monotonic() + 1.0
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
            return bytes(self.buf[-512:])

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
            replies.append(b"\x1b[>41;371;0c")
        if b"\x1b[>0q" in tail:
            replies.append(b"\x1bP>|xterm(380)\x1b\\")
        if re.search(rb"\x1b\[\??[0-9]*u", tail):
            replies.append(b"\x1b[?0u")
        if b"\x1b[?2026$p" in tail:
            replies.append(b"\x1b[?2026;2$y")
        for m in re.finditer(rb"\x1b\]4;(\d+);\?", tail):
            replies.append(b"\x1b]4;" + m.group(1) + b";" +
                           palette_reply(int(m.group(1))) + b"\x1b\\")
        if b"\x1b]10;?" in tail:
            replies.append(b"\x1b]10;rgb:eeee/eeee/eeee\x1b\\")
        if b"\x1b]11;?" in tail:
            replies.append(b"\x1b]11;rgb:1a1a/1a1a/1a1a\x1b\\")
        for r in replies:
            self._send(r)

    def key(self, k):
        self._send(k.encode() if isinstance(k, str) else k)

    def quiet(self, window=0.45, timeout=8.0):
        """Wait until output stays silent for `window` seconds."""
        end = time.monotonic() + timeout
        last = -1
        stable_since = None
        while time.monotonic() < end:
            time.sleep(0.05)
            with self.lock:
                cur = len(self.buf)
            if cur != last:
                last = cur
                stable_since = time.monotonic()
            elif stable_since is not None and \
                    time.monotonic() - stable_since >= window:
                return True
        return False

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except Exception:
                self.proc.kill()
        try:
            os.close(self.master)
        except OSError:
            pass

# ------------------------------------------------------------- palette

def xterm_palette():
    pal = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
           (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
           (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
           (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
    levels = [0, 95, 135, 175, 215, 255]
    for i in range(216):
        r, g, b = i // 36, (i // 6) % 6, i % 6
        pal.append((levels[r], levels[g], levels[b]))
    for i in range(24):
        v = 8 + 10 * i
        pal.append((v, v, v))
    return pal

PALETTE = xterm_palette()

def palette_reply(idx):
    if 0 <= idx < 256:
        r, g, b = PALETTE[idx]
        return ("rgb:%02x%02x/%02x%02x/%02x%02x" %
                (r >> 4, r & 15, g >> 4, g & 15, b >> 4, b & 15)).encode()
    return b"rgb:0000/0000/0000"

# ------------------------------------------------------------- emulator

DEFAULT_FG = (238, 238, 238)
DEFAULT_BG = (26, 26, 26)

class Screen:
    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.reset()

    def reset(self):
        self.chars = [[" "] * self.cols for _ in range(self.rows)]
        self.fg = [[DEFAULT_FG] * self.cols for _ in range(self.rows)]
        self.bg = [[DEFAULT_BG] * self.cols for _ in range(self.rows)]
        self.st = [[0] * self.cols for _ in range(self.rows)]  # bit flags
        self.x = self.y = 0
        self.cur_fg = DEFAULT_FG
        self.cur_bg = DEFAULT_BG
        self.cur_st = 0
        self.saved = (0, 0)
        self.alt = False

    # flags
    BOLD, ITAL, UNDER, REV = 1, 2, 4, 8

    def _put(self, ch):
        if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
            self.chars[self.y][self.x] = ch
            self.fg[self.y][self.x] = self.cur_fg
            self.bg[self.y][self.x] = self.cur_bg
            self.st[self.y][self.x] = self.cur_st
        self.x += 1
        if self.x >= self.cols:
            self.x = self.cols - 1

    def _erase(self, y, x0, x1):
        for x in range(x0, min(x1, self.cols)):
            self.chars[y][x] = " "
            self.fg[y][x] = self.cur_fg
            self.bg[y][x] = self.cur_bg
            self.st[y][x] = self.cur_st

    def _erase_rows(self, y0, y1):
        for y in range(y0, min(y1, self.rows)):
            self._erase(y, 0, self.cols)

    def _scroll_up(self):
        self.chars.pop(0); self.chars.append([" "] * self.cols)
        self.fg.pop(0); self.fg.append([DEFAULT_FG] * self.cols)
        self.bg.pop(0); self.bg.append([DEFAULT_BG] * self.cols)
        self.st.pop(0); self.st.append([0] * self.cols)

    def feed(self, data):
        i, n = 0, len(data)
        while i < n:
            b = data[i]
            if b == 0x1b:
                i = self._esc(data, i)
            elif b == 0x0d:
                self.x = 0; i += 1
            elif b == 0x0a:
                self.y += 1
                if self.y >= self.rows:
                    self.y = self.rows - 1
                    self._scroll_up()
                i += 1
            elif b == 0x08:
                self.x = max(0, self.x - 1); i += 1
            elif b == 0x09:
                self.x = min(self.cols - 1, (self.x // 8 + 1) * 8); i += 1
            elif b < 0x20 or b == 0x7f:
                i += 1
            else:
                # decode utf-8 char
                ln = 1
                if b >= 0xf0: ln = 4
                elif b >= 0xe0: ln = 3
                elif b >= 0xc0: ln = 2
                try:
                    ch = data[i:i + ln].decode("utf-8")
                except UnicodeDecodeError:
                    ch = "?"
                self._put(ch)
                i += ln
        return i

    def _esc(self, data, i):
        n = len(data)
        if i + 1 >= n:
            return n
        c = data[i + 1]
        if c == ord("["):
            j = i + 2
            start = j
            while j < n and 0x20 <= data[j] <= 0x3f:
                j += 1
            if j >= n:
                return n
            final = data[j]
            seq = data[start:j]
            self._csi(seq, chr(final))
            return j + 1
        if c == ord("]"):                       # OSC ... BEL/ST
            j = i + 2
            while j < n:
                if data[j] == 0x07:
                    return j + 1
                if data[j] == 0x1b and j + 1 < n and data[j + 1] == ord("\\"):
                    return j + 2
                j += 1
            return n
        if c in (ord("P"), ord("X"), ord("^"), ord("_")):   # DCS/SOS/PM/APC
            j = i + 2
            while j + 1 < n:
                if data[j] == 0x1b and data[j + 1] == ord("\\"):
                    return j + 2
                j += 1
            return n
        if c == ord("7"):
            self.saved = (self.x, self.y); return i + 2
        if c == ord("8"):
            self.x, self.y = self.saved; return i + 2
        if c == ord("c"):
            self.reset(); return i + 2
        if c in (ord("("), ord(")"), ord("#")):
            return min(i + 3, n)                # charset designation
        return i + 2                            # ESC = > etc: ignore

    def _csi(self, seq, final):
        seqbody = seq
        if seq[:1] in (b"?", b">", b"=", b"<", b" "):
            priv = bytes([seq[0]])
            seqbody = seq[1:]
        else:
            priv = b""
        try:
            params = [int(p) if p else 0
                      for p in seqbody.replace(b":", b";").split(b";")]
        except ValueError:
            params = [0]

        def p(idx, default=1):
            return params[idx] if idx < len(params) and params[idx] else default

        if final == "H" or final == "f":
            self.y = min(self.rows - 1, max(0, p(0) - 1))
            self.x = min(self.cols - 1, max(0, p(1) - 1))
        elif final == "A":
            self.y = max(0, self.y - p(0))
        elif final == "B":
            self.y = min(self.rows - 1, self.y + p(0))
        elif final == "C":
            self.x = min(self.cols - 1, self.x + p(0))
        elif final == "D":
            self.x = max(0, self.x - p(0))
        elif final == "E":
            self.y = min(self.rows - 1, self.y + p(0)); self.x = 0
        elif final == "F":
            self.y = max(0, self.y - p(0)); self.x = 0
        elif final == "G" or final == "`":
            self.x = min(self.cols - 1, max(0, p(0) - 1))
        elif final == "d":
            self.y = min(self.rows - 1, max(0, p(0) - 1))
        elif final == "J":
            m = params[0] if params else 0
            if m == 0:
                self._erase(self.y, self.x, self.cols)
                self._erase_rows(self.y + 1, self.rows)
            elif m == 1:
                self._erase_rows(0, self.y)
                self._erase(self.y, 0, self.x + 1)
            else:
                self._erase_rows(0, self.rows)
        elif final == "K":
            m = params[0] if params else 0
            if m == 0:
                self._erase(self.y, self.x, self.cols)
            elif m == 1:
                self._erase(self.y, 0, self.x + 1)
            else:
                self._erase(self.y, 0, self.cols)
        elif final == "L":                       # insert lines
            count = p(0)
            for _ in range(count):
                self.chars.insert(self.y, [" "] * self.cols); self.chars.pop()
                self.fg.insert(self.y, [DEFAULT_FG] * self.cols); self.fg.pop()
                self.bg.insert(self.y, [DEFAULT_BG] * self.cols); self.bg.pop()
                self.st.insert(self.y, [0] * self.cols); self.st.pop()
        elif final == "M":                       # delete lines
            count = p(0)
            for _ in range(count):
                self.chars.pop(self.y); self.chars.append([" "] * self.cols)
                self.fg.pop(self.y); self.fg.append([DEFAULT_FG] * self.cols)
                self.bg.pop(self.y); self.bg.append([DEFAULT_BG] * self.cols)
                self.st.pop(self.y); self.st.append([0] * self.cols)
        elif final == "@":                       # insert chars
            count = p(0)
            row = self.chars[self.y]
            for _ in range(count):
                row.insert(self.x, " "); row.pop()
        elif final == "P":                       # delete chars
            count = p(0)
            row = self.chars[self.y]
            for _ in range(count):
                row.pop(self.x); row.append(" ")
        elif final == "m":
            self._sgr(params)
        elif final in "hl":
            if priv == b"?" and 1049 in params:
                self.reset()
                self.alt = (final == "h")
        # anything else (r, s, u, ...) ignored

    def _sgr(self, params):
        i = 0
        if not params:
            params = [0]
        while i < len(params):
            v = params[i]
            if v == 0:
                self.cur_fg = DEFAULT_FG; self.cur_bg = DEFAULT_BG
                self.cur_st = 0
            elif v == 1:
                self.cur_st |= self.BOLD
            elif v == 3:
                self.cur_st |= self.ITAL
            elif v == 4 or v == 21:
                self.cur_st |= self.UNDER
            elif v == 7:
                self.cur_st |= self.REV
            elif v == 22:
                self.cur_st &= ~self.BOLD
            elif v == 23:
                self.cur_st &= ~self.ITAL
            elif v == 24:
                self.cur_st &= ~self.UNDER
            elif v == 27:
                self.cur_st &= ~self.REV
            elif v == 39:
                self.cur_fg = DEFAULT_FG
            elif v == 49:
                self.cur_bg = DEFAULT_BG
            elif v in (38, 48):
                col = None
                if i + 1 < len(params):
                    if params[i + 1] == 5 and i + 2 < len(params):
                        idx = params[i + 2]
                        col = PALETTE[idx] if 0 <= idx < 256 else None
                        i += 2
                    elif params[i + 1] == 2 and i + 4 < len(params):
                        col = tuple(params[i + 2:i + 5])
                        i += 4
                if col:
                    if v == 38:
                        self.cur_fg = col
                    else:
                        self.cur_bg = col
            elif 90 <= v <= 97:
                self.cur_fg = PALETTE[v - 90 + 8]
            elif 100 <= v <= 107:
                self.cur_bg = PALETTE[v - 100 + 8]
            i += 1

# ------------------------------------------------------------- render

FONTS = "/usr/share/fonts/TTF/DejaVuSansMono.ttf"

def render(screen, path, fontsize=19):
    font = ImageFont.truetype(FONTS, fontsize)
    fontb = ImageFont.truetype(
        "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf", fontsize)
    fonti = ImageFont.truetype(
        "/usr/share/fonts/TTF/DejaVuSansMono-Oblique.ttf", fontsize)
    fontbi = ImageFont.truetype(
        "/usr/share/fonts/TTF/DejaVuSansMono-BoldOblique.ttf", fontsize)
    ascent, descent = font.getmetrics()
    cw = int(font.getlength("M"))
    ch = ascent + descent + 1
    pad = 26
    W = screen.cols * cw + pad * 2
    H = screen.rows * ch + pad * 2
    img = Image.new("RGB", (W, H), (13, 13, 13))
    d = ImageDraw.Draw(img)
    # screen body background
    d.rectangle([pad - 6, pad - 6, W - pad + 6, H - pad + 6],
                fill=DEFAULT_BG, outline=(45, 45, 45))
    for y in range(screen.rows):
        for x in range(screen.cols):
            ch_ = screen.chars[y][x]
            st = screen.st[y][x]
            fg, bg = screen.fg[y][x], screen.bg[y][x]
            if st & Screen.REV:
                fg, bg = bg, fg
            px, py = pad + x * cw, pad + y * ch
            if bg != DEFAULT_BG:
                d.rectangle([px, py, px + cw - 1, py + ch - 1], fill=bg)
            if ch_ != " ":
                f = font
                if st & Screen.BOLD and st & Screen.ITAL:
                    f = fontbi
                elif st & Screen.BOLD:
                    f = fontb
                elif st & Screen.ITAL:
                    f = fonti
                d.text((px, py), ch_, font=f, fill=fg)
            if st & Screen.UNDER:
                d.line([px, py + ascent + 2, px + cw - 1, py + ascent + 2],
                       fill=fg)
    img.save(path)
    print(f"saved {path} ({W}x{H}, cell {cw}x{ch})")

# ------------------------------------------------------------- main

def main():
    out = sys.argv[1]
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 160
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else 50
    script = sys.argv[4] if len(sys.argv) > 4 else ""

    tmpcfg = tempfile.mkdtemp(prefix="ctron-shot-")
    os.environ["CTRON_CONFIG"] = tmpcfg
    s = Session(cols, rows)
    try:
        s.quiet(window=0.5, timeout=15.0)
        time.sleep(2.5)          # let live telemetry settle in
        if script:
            for step in script.split(";"):
                if step == "sleep":
                    time.sleep(1.5)
                else:
                    s.key(step)
                    s.quiet(window=0.4, timeout=8.0)
        with s.lock:
            data = bytes(s.buf)
    finally:
        s.close()
    sc = Screen(cols, rows)
    sc.feed(data)
    render(sc, out)

if __name__ == "__main__":
    main()

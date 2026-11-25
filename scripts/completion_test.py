#!/usr/bin/env python3
"""Regression test for the shell completions (bash, zsh, fish).

    make comptest            # every shell that is installed
    python3 scripts/completion_test.py [shell ...]

Each shell is driven the way a user's Tab key reaches it: bash through
its completion function, fish through `complete --do-complete`, zsh
through a real interactive shell in a pty. The completions run against
an isolated, seeded config directory (never the user's own) and every
case states the exact candidate set expected, so a change in the CLI or
in one of the three files shows up as a failure here.

The flag list is taken from `build/ctron --help`, so a flag added to
the binary without touching the completions fails the test.
"""
import os
import pty
import re
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
import time
import fcntl

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COMP = os.path.join(ROOT, "completions")
BIN = os.path.join(ROOT, "build", "ctron")

MODES = {"turbo", "dup", "a-very-long-mode-name-o"}
SNAPS = {"30w-silent", "quiet/office"}
CURVES = {"silent", "gaming/boost"}
EFFECTS = {"static", "breathe", "rainbow-cycle", "rainbow-wave", "pulse",
           "comet", "flash", "stars", "rain", "highlight", "laser", "ripple"}
COLORS = {"Ice", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "White"}
SUBS = {"mode", "snapshot", "profile", "fancurve", "fan-curve"}
ENTRY_SUBS = {"list", "apply", "diff", "export", "delete"}

# (command line, expectation): an exact candidate set, ONLY_FLAGS (the
# result may contain nothing but documented flags — bash offers further
# flags on an empty word, the other two offer nothing there) or None for
# "nothing at all". A trailing space puts the cursor on a new, empty word.
ONLY_FLAGS = "only-flags"
CASES = [
    ("ctron --profile ", {"quiet", "balanced", "performance"}),
    ("ctron --profile=b", {"balanced"}),
    ("ctron --ac-profile ", {"off", "quiet", "balanced", "performance"}),
    ("ctron --battery-profile ", {"off", "quiet", "balanced", "performance"}),
    ("ctron --epp b", {"balance_power", "balance_performance"}),
    ("ctron --freq ", {"core", "ccd"}),
    ("ctron --hz ", {"max"}),
    ("ctron --ppt ", {"Q45", "B60", "P80", "45,55,55", "60,75,75",
                      "80,80,80", "off", "on"}),
    ("ctron --ppt=o", {"off", "on"}),
    ("ctron --gpu-clock ", {"reset"}),
    ("ctron --panel-od ", {"on", "off"}),
    ("ctron --cpu-boost ", {"on", "off"}),
    ("ctron --kbd ", {"off", "low", "med", "high"}),
    ("ctron --aura ", EFFECTS),
    ("ctron --aura static ", COLORS),
    ("ctron --aura ripple ", COLORS),
    ("ctron --mode ", MODES),
    ("ctron --mode d", {"dup"}),
    ("ctron --fan ", {"stock", "silent", "cool", "full", "on", "off",
                      "cpu", "gpu"} | CURVES),
    ("ctron --fan c", {"cool", "cpu"}),
    ("ctron --fan=g", {"gpu", "gaming/boost"}),
    ("ctron --fan cpu ", {"on", "off"}),
    ("ctron --fan gpu ", {"on", "off"}),
    ("ctron --fan-curve ", {"cpu", "gpu"}),
    # a value that is a bare word: more flags are fine, subcommands are not
    ("ctron --fan stock ", ONLY_FLAGS),
    ("ctron --fan-curve cpu ", ONLY_FLAGS),
    ("ctron --mode turbo ", ONLY_FLAGS),
    ("ctron --profile quiet ", ONLY_FLAGS),
    ("ctron mode ", {"list", "show", "add", "delete"}),
    ("ctron mode s", {"show"}),
    ("ctron --dry-run mode ", {"list", "show", "add", "delete"}),
    ("ctron --config-dir /tmp mode ", {"list", "show", "add", "delete"}),
    ("ctron mode show ", MODES),
    ("ctron mode delete a", {"a-very-long-mode-name-o"}),
    ("ctron mode list ", None),
    ("ctron mode add new ", None),
    ("ctron mode show --", {"--json", "--config-dir"}),
    ("ctron snapshot ", ENTRY_SUBS),
    ("ctron snapshot a", {"apply"}),
    ("ctron snapshot apply ", SNAPS),
    ("ctron --json snapshot apply ", SNAPS),
    ("ctron snapshot apply --", {"--dry-run", "--json", "--config-dir"}),
    ("ctron snapshot apply --d", {"--dry-run"}),
    ("ctron snapshot delete ", SNAPS),
    ("ctron snapshot delete --", {"--json", "--config-dir"}),
    ("ctron snapshot diff q", {"quiet/office"}),
    ("ctron snapshot diff quiet/", {"quiet/office"}),
    ("ctron snapshot export ", SNAPS),
    ("ctron profile ", ENTRY_SUBS),
    ("ctron profile apply 3", {"30w-silent"}),
    ("ctron fancurve ", ENTRY_SUBS),
    ("ctron fancurve apply ", CURVES),
    ("ctron fancurve apply --", {"--dry-run", "--json", "--config-dir"}),
    ("ctron fancurve delete ", CURVES),
    ("ctron fancurve delete --", {"--json", "--config-dir"}),
    ("ctron fancurve export ", CURVES),
    ("ctron fancurve list ", None),
    ("ctron fan-curve diff g", {"gaming/boost"}),
]

# the flag list comes from the binary itself, not from a copy here
def help_flags():
    out = subprocess.run([BIN, "--help"], capture_output=True, text=True,
                         check=True).stdout
    return set(re.findall(r"--[a-z][a-z0-9-]*", out))


def seed_config(parent):
    cfg = os.path.join(parent, "cfg")
    for sub in ("profiles/quiet", "fan_curves/gaming"):
        os.makedirs(os.path.join(cfg, sub), exist_ok=True)
    # mirrors what ctron itself lists: a section needs a `steps =` line,
    # repeated sections count once, names are cut at 23 characters
    with open(os.path.join(cfg, "modes.ini"), "w") as f:
        f.write("[turbo]\nsteps = profile performance, ppt P80\n"
                "# [commented]\n; steps = nothing\n"
                "[no-steps]\nname = nothing here\n"
                "[dup]\nSTEPS = profile quiet\n[dup]\nsteps = profile balanced\n"
                "[a-very-long-mode-name-over-23-chars]\nsteps = profile quiet\n")
    for name in ("profiles/30w-silent.ctr", "profiles/quiet/office.ctr",
                 "profiles/notasnapshot.txt", "fan_curves/silent.fc",
                 "fan_curves/gaming/boost.fc"):
        with open(os.path.join(cfg, name), "w") as f:
            f.write("x\n")
    return cfg


# ---- bash -------------------------------------------------------------------
def run_bash(cfg, cases):
    harness = r'''
source "$1"
_ctron_case() {
    COMP_WORDS=("$@")
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    COMPREPLY=()
    _ctron >/dev/null 2>&1
    printf '%s\n' "${COMPREPLY[@]}"
}
'''
    script = [harness]
    quoted = []
    for line, _ in cases:
        words, trailing = split_line(line)
        args = " ".join("'" + w + "'" for w in ["ctron"] + words + ([""] if trailing or not words else []))
        script.append("_ctron_case %s; printf '\\x00\\n'\n" % args)
        quoted.append(args)
    proc = subprocess.run(["bash", "--noprofile", "--norc", "-c", "".join(script),
                           "bash", os.path.join(COMP, "ctron.bash")],
                          capture_output=True, text=True, env=env_for(cfg))
    return [set(chunk.split()) for chunk in proc.stdout.split("\x00\n")]


# ---- fish -------------------------------------------------------------------
def run_fish(cfg, cases):
    script = ["source " + sh_quote(os.path.join(COMP, "ctron.fish"))]
    for line, _ in cases:
        script.append("complete --do-complete=%s; printf '\\x00\\n'"
                      % sh_quote(line))
    proc = subprocess.run(["fish", "-c", "; ".join(script)], capture_output=True,
                          text=True, env=env_for(cfg))
    # "candidate\tdescription"; --flag=value comes back with its prefix
    out = [{re.sub(r"^--[a-z-]+=", "", c.split("\t")[0])
            for c in chunk.split("\n") if c}
           for chunk in proc.stdout.split("\x00\n")]
    return out


# ---- zsh (a real interactive shell in a pty) --------------------------------
PROMPT = "@@ctron-prompt@@ "


class Zsh:
    def __init__(self, cfg):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = env_for(cfg)
            env["PS1"] = PROMPT
            env["COLUMNS"] = "400"
            env["LINES"] = "50"
            os.environ.clear()
            os.environ.update(env)
            os.execvp("zsh", ["zsh", "-f", "-i"])
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 50, 400, 0, 0))
        self.wait()
        dump = os.path.join(os.path.dirname(cfg), "zcompdump")
        # fpath first: compinit only registers the functions it can see
        self.send("fpath=(%s $fpath); autoload -Uz compinit; compinit -u -d %s; "
                  "zstyle ':completion:*' verbose no; "
                  "zstyle ':completion:*' menu no; "
                  "unsetopt listambiguous; setopt autolist; LISTMAX=1000; "
                  # Ctrl-G prints the edit buffer behind a marker (only way to
                  # read a unique completion reliably: zle redraws the changed
                  # part of the line only)
                  "ctron-dump() { zle -I; print -r -- \"@@buffer@@$BUFFER\"; "
                  "BUFFER=; zle reset-prompt }; "
                  "zle -N ctron-dump; bindkey '^G' ctron-dump\n"
                  % (sh_quote(COMP), sh_quote(dump)))
        self.wait()

    def send(self, text):
        os.write(self.fd, text.encode())

    def wait(self, idle=0.35, limit=8.0):
        out = b""
        quiet = 0.0
        start = time.time()
        while quiet < idle and time.time() - start < limit:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                out += chunk
                quiet = 0.0
            else:
                quiet += 0.05
        return out.decode("utf-8", "replace")

    def candidates(self, line):
        typed = line.split()
        self.send("\x15" + line + "\t")   # ^U kills the line, then Tab
        before, _, _ = self.wait().rpartition(PROMPT)
        # several candidates: a beep (with the common prefix inserted after
        # it), a blank line, the list, then the redrawn prompt
        m = re.search(r"\x07[^\r\n]*\r\r\n(.*?)\r\n?$", before, re.S)
        if m and m.group(1).strip():
            # one candidate per column: "word" or "word[description]"
            return {w.split("[")[0] for w in m.group(1).split()}
        # at most one candidate: zsh rewrites only the changed part of the
        # line, so ask the shell to print the whole buffer instead
        self.send("\x07")
        out = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", self.wait())
        m = re.search(r"@@buffer@@([^\r\n]*)", out)
        buf = m.group(1).split() if m else []
        if len(buf) > len(typed):
            word = buf[-1]                     # a new word was added
        elif buf and typed and buf[-1] != typed[-1]:
            word = buf[-1]                     # the current word grew
        else:
            return set()
        return {re.sub(r"^--[a-z-]+=", "", word)}

    def close(self):
        try:
            self.send("\x15exit\n")
            self.wait(0.2, 2.0)
        except OSError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass


def run_zsh(cfg, cases):
    z = Zsh(cfg)
    got = []
    try:
        for line, _ in cases:
            got.append(z.candidates(line))
    finally:
        z.close()
    return got


# ---- plumbing ---------------------------------------------------------------
def split_line(line):
    words = line.split()[1:]            # drop the leading "ctron"
    return words, line.endswith(" ")


def env_for(cfg):
    env = {k: v for k, v in os.environ.items()
           if k in ("PATH", "LANG", "LC_ALL", "TERM", "TMPDIR")}
    env["CTRON_CONFIG"] = cfg
    return env


def sh_quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def check(shell, cases, got, label, known_flags):
    """Compare each case; returns the number of failures."""
    bad = 0
    for (line, want), have in zip(cases, got):
        if want is None:
            ok = not have
        elif want == ONLY_FLAGS:
            ok = have <= known_flags
        elif isinstance(want, str):
            ok = set(want.split()).issubset(have)
        else:
            ok = have == want
        if not ok:
            bad += 1
            print("  FAIL %s %s %s" % (shell, label, "[" + line + "]"))
            print("       got  %s" % " ".join(sorted(have)))
            if want is None:
                print("       want nothing")
            elif want == ONLY_FLAGS:
                print("       want nothing but documented flags")
            elif isinstance(want, str):
                print("       want at least %s" % want)
            else:
                print("       want %s" % " ".join(sorted(want)))
    return bad


def main():
    shells = sys.argv[1:] or ["bash", "zsh", "fish"]
    # `--` lists the long flags, `-` the short ones; every flag the
    # binary documents has to be in there
    documented = help_flags() | {"--tui"}   # real, documented in the man page
    short = {"-h", "-V", "-s", "-w", "-t"}
    offered = [("ctron --", " ".join(sorted(f for f in documented if f.startswith("--")))),
               ("ctron -", " ".join(sorted(short))),
               ("ctron ", " ".join(sorted(SUBS)))]
    flags = documented | short
    if not os.path.exists(BIN):
        sys.exit("completion_test: build ctron first (make)")
    failed = 0
    runners = {"bash": run_bash, "zsh": run_zsh, "fish": run_fish}
    with tempfile.TemporaryDirectory(prefix="ctron-comptest-") as tmp:
        cfg = seed_config(tmp)
        for shell in shells:
            if shutil.which(shell) is None:
                print("%-4s skipped (not installed)" % shell)
                continue
            bad = check(shell, CASES, runners[shell](cfg, CASES), "case", flags)
            bad += check(shell, offered, runners[shell](cfg, offered), "offers", flags)
            failed += bad
            print("%-4s %d/%d checks" % (shell, len(CASES) + len(offered) - bad,
                                          len(CASES) + len(offered)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

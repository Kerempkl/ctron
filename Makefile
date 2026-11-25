# GNU make required ($(shell) below). An external CFLAGS/LDLIBS (distro
# packaging, Nix stdenv, -O0 debugging) is honoured; the flags ctron
# cannot build without are appended after it. pkg-config absence falls
# back to the plain library names. LTO=1 opts into link-time
# optimization (off by default so debug/packaging builds are
# untouched); `make install` strips the installed binary only —
# build/ctron keeps its symbols (STRIP=0 keeps them in the installed
# binary too, for packagers that split and strip debug info themselves).
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -std=c11 -Isrc -D_GNU_SOURCE -pthread
CFLAGS += $(shell pkg-config --cflags notcurses 2>/dev/null)
LDLIBS ?=
LDLIBS += $(shell pkg-config --libs notcurses 2>/dev/null \
           || echo -lnotcurses -lnotcurses-core)
LDLTO =
ifeq ($(LTO),1)
CFLAGS += -flto=auto
LDLTO = -flto=auto
endif

# HARDEN=1 opts into explicit exploit mitigations (strong stack canary +
# fortified libc calls). Distro CFLAGS usually add these already; Nix
# stdenv and a plain `make` do not — the flag makes them deterministic
# for release/packaging builds without touching the default path.
ifeq ($(HARDEN),1)
CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=2
endif

# The version is written once, in the root VERSION file, and handed to
# the compiler here — not through CFLAGS, so a `make CFLAGS=...` override
# cannot drop it. Every object depends on the file for a plain rebuild
# after a bump. `make check-version` verifies the copies that cannot
# read it (README badge, flake.nix, PKGBUILD, CHANGELOG).
VERSION := $(strip $(shell cat VERSION 2>/dev/null))
ifeq ($(VERSION),)
$(error VERSION file missing or empty in the repository root)
endif

SRC = src/main.c src/util.c src/hw.c src/control.c src/daeboard.c src/fan.c src/cmds.c \
      src/modes.c src/profile.c src/settings.c src/telem.c src/tinput.c \
      src/ui/ui.c src/ui/panel_profiles.c \
      src/ui/panel_workspace.c src/ui/panel_telem_full.c \
      src/ui/panel_settings.c \
      src/ui/editor_fan.c src/ui/editor_daeboard.c src/ui/editor_corefreq.c \
      src/display/display.c src/display/display_kde.c src/display/display_hypr.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = build/ctron

TEST_SRC = tests/test_core.c
TEST_OBJ = util hw control daeboard fan cmds modes profile settings telem tinput \
           display/display display/display_kde display/display_hypr
TEST_LIBOBJ = $(addprefix build/,$(addsuffix .o,$(TEST_OBJ)))
TEST_BIN = build/test_core

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(LDLTO) $(OBJ) -o $@ $(LDLIBS)

HEADERS = $(wildcard src/*.h src/ui/*.h src/display/*.h)

build/%.o: src/%.c $(HEADERS) VERSION
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DCTRON_VERSION='"$(VERSION)"' -c $< -o $@

test: $(TEST_BIN) build/test_daeboard
	$(TEST_BIN)
	./build/test_daeboard

tuitest: $(TARGET)
	python3 scripts/tui_smoke.py $(TARGET)

check-version:
	@sh scripts/check-version.sh

# The README flag block is a verbatim copy of `ctron --help`.
check-help: $(TARGET)
	@sh scripts/check-help.sh

# Shell completion regression test: drives bash, zsh and fish the way Tab
# reaches them (the shells that are installed), against a throwaway
# config directory. Optional — it needs the shells, never hardware.
comptest: $(TARGET)
	python3 scripts/completion_test.py $(COMPT_SHELLS)

build/test_daeboard: tests/test_daeboard.c src/daeboard.c src/util.c src/daeboard.h src/util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_daeboard.c src/daeboard.c src/util.c

$(TEST_BIN): $(TEST_SRC) $(TEST_LIBOBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_SRC) $(TEST_LIBOBJ)

# Install layout. The default is the historical user-local one (the
# binary, the ctron-toggle helper, the man page and the bash/zsh/fish
# completions under ~/.local); distro packaging passes the standard
# variables instead, e.g.
#   make install PREFIX=/usr DESTDIR=$PWD/pkg    (staged system tree)
#   make install PREFIX=/usr STRIP=0             (packager strips itself)
PREFIX  ?= $(HOME)/.local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man
DATADIR ?= $(PREFIX)/share

STRIP ?= 1
ifeq ($(STRIP),0)
INSTBIN = install -m 755
else
INSTBIN = install -m 755 -s
endif

MAN1 = $(MANDIR)/man1/ctron.1
COMP_BASH = $(DATADIR)/bash-completion/completions/ctron
COMP_ZSH = $(DATADIR)/zsh/site-functions/_ctron
COMP_FISH = $(DATADIR)/fish/vendor_completions.d/ctron.fish

# ctron.desktop is deliberately not installed here: a user-local install
# would overwrite a hand-edited ~/.local/share/applications/ctron.desktop
# (the launcher ctron-toggle is bound to). Packages install it from
# contrib/ instead.
install: $(TARGET) man/ctron.1 completions/ctron.bash completions/_ctron completions/ctron.fish
	install -d $(DESTDIR)$(BINDIR)
	$(INSTBIN) $(TARGET) $(DESTDIR)$(BINDIR)/ctron
	install -m 755 scripts/ctron-toggle.sh $(DESTDIR)$(BINDIR)/ctron-toggle
	install -d $(DESTDIR)$(MANDIR)/man1 \
	           $(DESTDIR)$(DATADIR)/bash-completion/completions \
	           $(DESTDIR)$(DATADIR)/zsh/site-functions \
	           $(DESTDIR)$(DATADIR)/fish/vendor_completions.d
	install -m 644 man/ctron.1 $(DESTDIR)$(MAN1)
	install -m 644 completions/ctron.bash $(DESTDIR)$(COMP_BASH)
	install -m 644 completions/_ctron $(DESTDIR)$(COMP_ZSH)
	install -m 644 completions/ctron.fish $(DESTDIR)$(COMP_FISH)
	@echo "installed ctron + ctron-toggle to $(DESTDIR)$(BINDIR)"
	@echo "          man page + bash/zsh/fish completions under $(DESTDIR)$(DATADIR)"
	@if [ -z "$(DESTDIR)" ] && [ "$(BINDIR)" = "$(HOME)/.local/bin" ]; then \
		echo 'note: zsh users need `fpath=(~/.local/share/zsh/site-functions $$fpath)` before compinit'; \
	fi
	@case ":$$PATH:" in *":$(BINDIR):"*) ;; *) \
		echo "note: $(BINDIR) is not in your PATH";; esac

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ctron $(DESTDIR)$(BINDIR)/ctron-toggle \
	      $(DESTDIR)$(MAN1) $(DESTDIR)$(COMP_BASH) $(DESTDIR)$(COMP_ZSH) \
	      $(DESTDIR)$(COMP_FISH)

clean:
	rm -rf build

.PHONY: all test tuitest comptest check-version check-help install uninstall clean

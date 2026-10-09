# GNU make required ($(shell) below). An external CFLAGS/LDLIBS (distro
# packaging, Nix stdenv, -O0 debugging) is honoured; the flags ctron
# cannot build without are appended after it. pkg-config absence falls
# back to the plain library names.
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -std=c11 -Isrc -D_GNU_SOURCE -pthread
CFLAGS += $(shell pkg-config --cflags notcurses 2>/dev/null)
LDLIBS ?=
LDLIBS += $(shell pkg-config --libs notcurses 2>/dev/null \
           || echo -lnotcurses -lnotcurses-core)

SRC = src/main.c src/util.c src/hw.c src/control.c src/daeboard.c src/fan.c src/cmds.c \
      src/modes.c src/profile.c src/settings.c \
      src/ui/ui.c src/ui/panel_profiles.c src/ui/panel_controls.c \
      src/ui/panel_workspace.c src/ui/panel_telemetry.c src/ui/panel_settings.c \
      src/ui/editor_fan.c src/ui/editor_daeboard.c src/ui/editor_corefreq.c \
      src/display/display.c src/display/display_kde.c src/display/display_hypr.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = build/ctron

TEST_SRC = tests/test_core.c
TEST_OBJ = util hw control daeboard fan cmds modes profile settings \
           display/display display/display_kde display/display_hypr
TEST_LIBOBJ = $(addprefix build/,$(addsuffix .o,$(TEST_OBJ)))
TEST_BIN = build/test_core

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

HEADERS = $(wildcard src/*.h src/ui/*.h src/display/*.h)

build/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN) build/test_daeboard
	$(TEST_BIN)
	./build/test_daeboard

tuitest: $(TARGET)
	python3 scripts/tui_smoke.py $(TARGET)

build/test_daeboard: tests/test_daeboard.c src/daeboard.c src/util.c src/daeboard.h src/util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_daeboard.c src/daeboard.c src/util.c

$(TEST_BIN): $(TEST_SRC) $(TEST_LIBOBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(TEST_LIBOBJ)

install: $(TARGET)
	mkdir -p $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/ctron
	@echo "installed to $(HOME)/.local/bin/ctron"
	@case ":$$PATH:" in *":$(HOME)/.local/bin:"*) ;; *) \
		echo "note: $(HOME)/.local/bin is not in your PATH";; esac

clean:
	rm -rf build

.PHONY: all test tuitest install clean

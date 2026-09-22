CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Isrc -D_GNU_SOURCE \
          $(shell pkg-config --cflags notcurses 2>/dev/null)
LDLIBS ?= $(shell pkg-config --libs notcurses 2>/dev/null || echo -lnotcurses)

SRC = src/main.c src/util.c src/hw.c src/control.c src/fan.c src/cmds.c \
      src/modes.c src/profile.c src/settings.c \
      src/ui/ui.c src/ui/panel_profiles.c src/ui/panel_controls.c \
      src/ui/panel_workspace.c src/ui/panel_telemetry.c src/ui/panel_settings.c \
      src/ui/editor_fan.c \
      src/display/display.c src/display/display_kde.c src/display/display_hypr.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = build/ctron

TEST_SRC = tests/test_core.c
TEST_OBJ = util hw control fan cmds modes profile settings \
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

test: $(TEST_BIN)
	$(TEST_BIN)

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

.PHONY: all test install clean

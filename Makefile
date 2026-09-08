CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -D_GNU_SOURCE $(shell pkg-config --cflags notcurses 2>/dev/null)
LDFLAGS ?= $(shell pkg-config --libs notcurses 2>/dev/null || echo "-lnotcurses -lnotcurses-core")

SRC = src/main.c src/hardware.c src/settings.c src/profile.c src/ui.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = build/ctron

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/*.o $(TARGET) $(TEST_BIN)

install: $(TARGET)
	mkdir -p $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/ctron
	chmod 755 scripts/daetron-spawn.sh
	ln -sf $(HOME)/.local/bin/ctron $(HOME)/.local/bin/vhelper
	ln -sf $(HOME)/.local/bin/ctron $(HOME)/.local/bin/tufhelper
	mkdir -p $(HOME)/.local/share/applications
	install -m 644 ctron.desktop $(HOME)/.local/share/applications/ctron.desktop
	ln -sf $(HOME)/.local/share/applications/ctron.desktop $(HOME)/.local/share/applications/vhelper.desktop

TEST = tests/test_persist.c
TEST_BIN = build/test_persist

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST) src/hardware.c src/settings.c src/profile.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(TEST) src/hardware.c src/settings.c src/profile.c

.PHONY: all clean install test

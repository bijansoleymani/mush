# Magic Mushroom - SDL2 reimplementation
#
#   make        build ./mush
#   make run    build and run (loads the original .VGA/.LVL assets from .)
#   make clean
#
# Requires SDL2:  macOS -> `brew install sdl2`,  Debian -> `apt install libsdl2-dev`

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
CFLAGS  += $(shell pkg-config --cflags sdl2)
LDLIBS  += $(shell pkg-config --libs sdl2) -lm

SRC := src/main.c src/game.c src/render.c src/pcx.c src/assets.c
BIN := mush

$(BIN): $(SRC) src/mush.h src/font.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: run clean

# Magic Mushroom - SDL2 reimplementation
#
#   make        build ./mush
#   make run    build and run (loads the original .VGA/.LVL assets from .)
#   make web    build the WebAssembly version into web/ (needs emscripten)
#   make serve  build web/ and serve it at http://localhost:8000
#   make clean
#
# Requires SDL2:  macOS -> `brew install sdl2`,  Debian -> `apt install libsdl2-dev`
# For `make web`: macOS -> `brew install emscripten`, or install the emsdk

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

# --- WebAssembly build: emcc compiles the same sources against its own SDL2
# port and packs the original .VGA/.LVL assets into web/index.data, where
# fopen() reads them from the in-memory filesystem unchanged.
# No ALLOW_MEMORY_GROWTH: a growable heap is backed by a resizable
# ArrayBuffer, which Chrome's WebGL rejects in texSubImage2D (the call SDL
# uses to upload the framebuffer every frame). The game fits in the
# default 16 MB with room to spare. ---
EMCC   ?= emcc
ASSETS := INTRO.VGA ZONESLCT.VGA FOREST.VGA FOREST.LVL \
          OASIS.VGA OASIS.LVL INFERNO.VGA INFERNO.LVL

web/index.html: $(SRC) src/mush.h src/font.h src/shell.html $(ASSETS)
	mkdir -p web
	$(EMCC) -std=c11 -O2 -Wall -Wextra -s USE_SDL=2 \
	    --shell-file src/shell.html \
	    $(foreach f,$(ASSETS),--preload-file $(f)) \
	    -o $@ $(SRC)

web: web/index.html

serve: web
	python3 -m http.server 8000 -d web

# --- Headless WASI build for standalone runtimes (wasmtime, etc.): the game
# minus SDL. src/headless.c drives the simulation with scripted input and
# dumps the rendered framebuffer as a PPM through the WASI filesystem.
# Emscripten's STANDALONE_WASM libc stubs out fopen, so this links against
# the real wasi-libc instead (emscripten's bundled clang + wasm-ld do the
# compiling). -nodefaultlibs -lc: Homebrew ships no compiler-rt builtins for
# wasm32-wasi, and this code needs none.
WASI_CC      ?= $(shell brew --prefix emscripten)/libexec/llvm/bin/clang
WASI_SYSROOT ?= $(shell brew --prefix wasi-libc)/share/wasi-sysroot
WASISRC := src/headless.c src/game.c src/render.c src/pcx.c src/assets.c

mush-wasi.wasm: $(WASISRC) src/mush.h src/font.h
	$(WASI_CC) --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT) \
	    -std=c11 -O2 -Wall -Wextra -nodefaultlibs -lc -o $@ $(WASISRC)

wasi: mush-wasi.wasm

run-wasi: mush-wasi.wasm
	wasmtime run --dir . mush-wasi.wasm . 0 350 frame.ppm

clean:
	rm -f $(BIN) mush-wasi.wasm frame.ppm
	rm -rf web

.PHONY: run web serve wasi run-wasi clean

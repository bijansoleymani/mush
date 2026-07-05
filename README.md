# Magic Mushroom — C / SDL2 reconstruction

A playable reimplementation of **Magic Mushroom**, the 1994 DOS shareware
platform game by Andre Lavoipierre, rebuilt in clean C from the original
16-bit binaries and data files in this folder.

> **What this is (and isn't).** The original was compiled with Turbo-C; its
> real source is unrecoverable. This is *not* a decompilation. Instead, the
> **data file formats were reverse-engineered** from the shipped assets
> (byte-for-byte verified), and the game was rebuilt around them in readable,
> modern C. The remake loads the **original, unmodified asset files**
> (`FOREST.VGA`, `FOREST.LVL`, `INTRO.VGA`, …) at runtime.

## Build & play

```sh
brew install sdl2      # macOS   (Debian/Ubuntu: apt install libsdl2-dev)
make
./mush                 # run from this folder so it finds the .VGA/.LVL files
# or: ./mush /path/to/assets
```

**Controls** (exactly as printed on the original title screen):

| Key | Action |
|-----|--------|
| `F1` / `F2` / `F3` | choose zone (Heart of the Forest / Oasis Death / Inferno) |
| any key | dismiss the title screen |
| **Left Shift** | move left (accelerates the longer it's held) |
| **Right Shift** | move right |
| **Alt** | jump |
| `Esc` | back to zone select / quit |

Collect **5 gems** per level while avoiding spikes, hazard fields, grey
toadstools, and the bottom of the screen. Clear all levels in a zone to win it.

## Reverse-engineered file formats

Everything below was recovered by inspecting the original bytes; every size
checks out exactly against all three zones. See the header comment in
[`src/mush.h`](src/mush.h) for the authoritative summary.

### `.LVL` — level maps
A flat array of levels, each **160 bytes** = a **16 × 10** grid of one-byte
tile codes (row-major). `filesize / 160` gives the level count, which matches
the level counts the original launcher passed on the command line
(`FOREST`=24, `OASIS`=13, `INFERNO`=13 — 50 total, as the manual claims).

Tile code `V` is **one-based** — it draws tilesheet bitmap `V − 1`:
* `0` → empty sky (air)
* `1 .. ntiles` → draw tilesheet bitmap `V − 1`
* `> ntiles` → an **entity marker** (a roaming enemy, or — where dense — a
  static hazard field such as Oasis's "death" tiles)

Some tile indices carry gameplay meaning (shared across zones): tile `0`
(mushroom) marks the **player start**, `1` = a toadstool/skull **enemy**,
`2` = a **gem** to collect, `4` = deadly **spikes**. So a level's mushroom,
gems and enemies are placed right in the map, not spawned randomly.

### `.VGA` — tilesheets
A flat array of tiles, each **400 bytes** = **20 × 20** pixels, one 8-bit
palette index per pixel, no header. `filesize / 400` gives the tile count
(FOREST 20, OASIS 19, INFERNO 17). The sheet layout is shared across zones:
tile `0` = the player mushroom, `1` = skull, `2` = gem, `4` = spikes.

### `.VGA` menu screens (`INTRO.VGA`, `ZONESLCT.VGA`)
Standard **256-colour RLE PCX** images (despite the `.VGA` extension). Each
carries its own palette and is shown with it.

### The gameplay palette (generated in code)
The zones share **one 256-colour palette** that is **not stored anywhere** — not
in `MM.EXE` (in any byte order or bit depth) and not in the tilesheets. The
original builds it at runtime and writes it to the VGA DAC, so the remake
**reproduces that generator exactly** (`game_palette_load` in
[`src/render.c`](src/render.c), recovered from `FUN_1000_01e2`):

* indices **16..231** are a **6×6×6 colour cube** — red and blue step through
  `{0,12,24,36,48,60}` while green is `trunc(inner × 0.8)` → `{0,9,19,28,38,48}`
  (the `0.8` is an IEEE double the original `FMUL`s against);
* index **1** and **232..255** are the blue `(0,15,30)`; **2..4** are the
  animated water-blue; **0** is the transparency key; **5..15** keep the default
  VGA palette.

DAC values are 6-bit, scaled to 8-bit for a modern display. This is the true
palette (e.g. index 196 = pure red for the mushroom cap, 231 = its white spots),
so all three zones — including Oasis's green trees and Inferno's red rock — are
colour-exact.

## Recovered mechanics (from decompiling MM.EXE)

The gameplay is **not guessed** — it was recovered by disassembling and
decompiling the original 16-bit DOS binary (capstone, with Borland's
floating-point-emulator `INT 0x34–0x3B` rewritten back to 8087 opcodes; then
Ghidra's decompiler via pyghidra). The physics lives in `FUN_1000_01e2`; all
constants below are the originals and sit at the top of
[`src/game.c`](src/game.c) / [`src/mush.h`](src/mush.h).

Motion is 16-bit **fixed-point: 64 units = 1 pixel**, stepped at the VGA
vsync rate (**~70 Hz**, mode 13h). Recovered values:

| Mechanic | Original value |
|---|---|
| Horizontal accel (shift held) | ±4 / frame, capped at ±240 (3.75 px/f) |
| Friction (grounded, no input) | 8 / frame toward 0 |
| Jump velocity (ALT) | −200; releasing ALT while rising adds +12/frame (variable height) |
| Gravity / terminal | +4 / frame, max 200 |
| Solid tiles | only level value 4 or 16 (grass-topped tiles); one-way, vertical-only collision |
| Death | falling (pixel-y > 175) or enemy contact — **spikes are not deadly**; no lives, infinite respawns |
| Gems | one shown at a time at a random gem-cell; collect → next random; 5 to clear |
| Enemies | speed by marker (0x10–0x40), wall-bounce, gravity +1 (term 0x40), fall→respawn at top |
| Controls | LEFT/RIGHT shift + ALT read via `bioskey(2)` BIOS flags; ESC quits |

**Faithful (from the original data / binary):** all artwork, level layouts, the
50 levels, zone names, and now the palette too — its 6×6×6-cube generator was
recovered from the code and reproduced exactly (see above), so no part of the
game is guessed.

## Source layout

| File | Role |
|------|------|
| [`src/mush.h`](src/mush.h) | types, constants, and the file-format spec |
| [`src/pcx.c`](src/pcx.c) | 256-colour RLE PCX loader (menu screens + master palette) |
| [`src/assets.c`](src/assets.c) | `.VGA` / `.LVL` loaders |
| [`src/render.c`](src/render.c) | software framebuffer: tiles, sprites, 8×8 text |
| [`src/game.c`](src/game.c) | level parsing, physics, enemies, gems, rules |
| [`src/main.c`](src/main.c) | SDL2 window, input, and the state machine |
| [`src/render.c`](src/render.c) → `game_palette_load` | reproduces the code's 256-colour palette generator |
| [`src/font.h`](src/font.h) | generated 8×8 bitmap font (HUD/messages) |

## Credits
Original game © 1994 Andre Lavoipierre. This reconstruction is an
educational, interoperability-focused rebuild of a freely-distributable
shareware release.

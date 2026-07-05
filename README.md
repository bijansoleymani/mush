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

Tile code meaning:
* `0` → empty sky (air)
* `1 .. ntiles-1` → draw that tilesheet bitmap
* `>= ntiles` → an **entity marker** (grey toadstool, or — where dense — a
  static hazard field such as Oasis's "death" tiles)

### `.VGA` — tilesheets
A flat array of tiles, each **400 bytes** = **20 × 20** pixels, one 8-bit
palette index per pixel, no header. `filesize / 400` gives the tile count
(FOREST 20, OASIS 19, INFERNO 17). The sheet layout is shared across zones:
tile `0` = the player mushroom, `1` = skull, `2` = gem, `4` = spikes.

### `.VGA` menu screens (`INTRO.VGA`, `ZONESLCT.VGA`)
Standard **256-colour RLE PCX** images (despite the `.VGA` extension).
`INTRO.VGA`'s palette is the one left on the VGA DAC when the title fades into
gameplay, so it doubles as the **master game palette**. Palette index **253**
(magenta) is the transparency colour key used by every sprite and tile.

## What's faithful vs. reconstructed

**Faithful (from the originals):** all artwork, palettes, level layouts, the
50 levels across 3 zones, zone names, the gem count, and the control scheme.

**Reconstructed (design choices, since the collision/physics tables live only
in the original machine code):**
* **Tile solidity** — the rule here keeps every level traversable: code `4` is
  deadly spikes, code `0` is air, every other drawn tile is a solid platform,
  and entity markers become toadstools (sparse) or hazard fields (dense).
* **Physics** — acceleration-based movement (the manual notes the controls are
  "dynamic — the longer you hold, the faster you move"), gravity, and jump feel
  are tuned to play well, not measured from the binary. Constants are all at the
  top of [`src/game.c`](src/game.c) if you want to tweak them.
* **Gems** — placed at random air cells (the manual says gems are "distributed
  randomly"), refilled as you collect, 5 to clear a level.

## Source layout

| File | Role |
|------|------|
| [`src/mush.h`](src/mush.h) | types, constants, and the file-format spec |
| [`src/pcx.c`](src/pcx.c) | 256-colour RLE PCX loader (menu screens + master palette) |
| [`src/assets.c`](src/assets.c) | `.VGA` / `.LVL` loaders |
| [`src/render.c`](src/render.c) | software framebuffer: tiles, sprites, 8×8 text |
| [`src/game.c`](src/game.c) | level parsing, physics, enemies, gems, rules |
| [`src/main.c`](src/main.c) | SDL2 window, input, and the state machine |
| [`src/font.h`](src/font.h) | generated 8×8 bitmap font (HUD/messages) |

## Credits
Original game © 1994 Andre Lavoipierre. This reconstruction is an
educational, interoperability-focused rebuild of a freely-distributable
shareware release.

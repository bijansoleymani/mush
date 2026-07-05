/*
 * Magic Mushroom  -  a clean-C / SDL2 reimplementation
 * ----------------------------------------------------
 * Reconstructed from the 1994 DOS shareware release by Andre Lavoipierre.
 *
 * This is NOT the original source (that is unrecoverable from a compiled
 * Turbo-C binary).  It is a faithful, readable reimplementation whose data
 * formats were reverse-engineered from the original asset files, which the
 * game still loads verbatim at runtime.
 *
 * ============================ FILE FORMATS ============================
 *
 * All formats below were recovered by inspecting the original bytes; every
 * size and field checks out exactly against FOREST/OASIS/INFERNO.
 *
 *  .LVL  (level maps)          e.g. FOREST.LVL (3840 = 24*160 bytes)
 *      A concatenation of levels, each exactly LEVEL_BYTES (160) bytes:
 *      a COLS x ROWS = 16 x 10 grid of one-byte tile codes, row-major.
 *      Level count = filesize / 160  (matches the launcher arg 24/13/13).
 *      Tile code meaning:
 *          0                 -> empty sky (air, not drawn)
 *          1 .. ntiles-1     -> draw tilesheet bitmap of that index
 *          >= ntiles         -> an "entity marker" (enemy or hazard); the
 *                               cell itself is air.  See zonedef in game.c.
 *
 *  .VGA  (tilesheets)          e.g. FOREST.VGA (8000 = 20*400 bytes)
 *      A concatenation of tiles, each TILE_BYTES (400) = 20x20 pixels,
 *      one 8-bit palette index per pixel, row-major, no header.
 *      Tile count = filesize / 400.
 *      Shared layout across zones: tile 0 = mushroom (the player sprite),
 *      tile 1 = skull, tile 2 = gem, tile 4 = spikes (deadly).
 *
 *  .VGA screens (INTRO.VGA, ZONESLCT.VGA)
 *      Standard 256-colour, RLE-compressed PCX images (despite the .VGA
 *      extension).  INTRO.VGA's palette is the one active during play, so
 *      it doubles as the game's master palette.  Palette index 253 is the
 *      magenta transparency key used by all sprites/tiles.
 *
 * =====================================================================
 */
#ifndef MUSH_H
#define MUSH_H

#include <stdint.h>
#include <stdbool.h>

/* ---- screen / tile geometry (VGA mode 13h, 20px tiles) ---- */
#define SCREEN_W      320
#define SCREEN_H      200
#define TILE          20
#define COLS          16
#define ROWS          10
#define LEVEL_BYTES   (COLS * ROWS)   /* 160 */
#define TILE_BYTES    (TILE * TILE)   /* 400 */
#define PAL_TRANSPARENT 253           /* magenta colour key */

/* ---- a 256-colour palette loaded from a PCX ---- */
typedef struct {
    uint8_t r[256], g[256], b[256];
    uint32_t argb[256];               /* pre-baked 0xFFRRGGBB for fast blits */
} Palette;

/* ---- a decoded PCX image (full-screen menu art) ---- */
typedef struct {
    int w, h;
    uint8_t *pix;                     /* w*h palette indices */
    Palette pal;
} Pcx;

/* ---- a zone's graphics + level data ---- */
typedef struct {
    uint8_t *tiles;                   /* ntiles * TILE_BYTES */
    int ntiles;
    uint8_t *levels;                  /* nlevels * LEVEL_BYTES */
    int nlevels;
} Zone;

/* ---- 32bpp software framebuffer we draw everything into ---- */
typedef struct {
    uint32_t px[SCREEN_W * SCREEN_H];
} Frame;

/* -------- assets.c : load original asset files -------- */
bool pcx_load(const char *path, Pcx *out);       /* pcx.c */
void pcx_free(Pcx *p);
bool zone_load(const char *dir, const char *vga, const char *lvl, Zone *out);
void zone_free(Zone *z);

/* -------- render.c : draw into a Frame using a Palette -------- */
void fb_clear(Frame *f, uint32_t argb);
void fb_blit_pcx(Frame *f, const Pcx *img);      /* full-screen, centred */
void fb_blit_tile(Frame *f, const Zone *z, const Palette *pal,
                  int tileidx, int px, int py);  /* 20x20, keys out 253 */
void fb_blit_tile_tinted(Frame *f, const Zone *z, const Palette *pal,
                         int tileidx, int px, int py, uint32_t tint);
void fb_draw_level(Frame *f, const Zone *z, const Palette *pal, int level);
void fb_text(Frame *f, int x, int y, const char *s, uint32_t argb);
void fb_text_center(Frame *f, int y, const char *s, uint32_t argb);
void fb_rect(Frame *f, int x, int y, int w, int h, uint32_t argb);

/* -------- game.c : one zone's play session -------- */
typedef enum { CELL_AIR, CELL_SOLID, CELL_DEADLY } CellKind;

typedef struct {
    float x, y, vx, vy;
    int   dir;                        /* -1 left, +1 right (patrol) */
    bool  alive;
} Enemy;

typedef struct { float x, y; bool active; } Gem;

#define MAX_ENEMIES 32
#define MAX_GEMS    8
#define GEMS_TO_WIN 5
#define GEMS_ONSCREEN 3

typedef struct {
    const Zone *zone;
    const Palette *pal;
    int   level;
    /* player */
    float px, py, pvx, pvy;
    bool  on_ground;
    int   lives;
    int   gems_collected;             /* toward GEMS_TO_WIN */
    Gem   gems[MAX_GEMS];
    Enemy enemies[MAX_ENEMIES];
    int   nenemies;
    /* per-cell classification for the current level */
    CellKind cell[ROWS][COLS];
    unsigned rng;
} Game;

void game_start_level(Game *g, int level);
void game_refill_gems(Game *g);
void game_respawn(Game *g);
void game_init(Game *g, const Zone *z, const Palette *pal);
/* advance one 60Hz tick; input flags are held-key states. returns event. */
typedef enum { EV_NONE, EV_DIED, EV_GAMEOVER, EV_LEVEL_CLEAR, EV_ZONE_CLEAR } GameEvent;
GameEvent game_tick(Game *g, bool left, bool right, bool jump);
void game_render(Game *g, Frame *f);

#endif /* MUSH_H */

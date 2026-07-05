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
 *      Tile code V is ONE-BASED: it draws tilesheet bitmap (V - 1).
 *          0            -> empty sky (air, not drawn)
 *          1 .. ntiles  -> draw tilesheet bitmap (V - 1)
 *          > ntiles     -> an "entity marker" (roaming enemy or hazard field)
 *      Certain tile indices carry gameplay meaning (shared across zones):
 *          tile 0 = player-start (mushroom), tile 1 = toadstool/skull enemy,
 *          tile 2 = gem pickup, tile 4 = spikes (deadly).
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
 *      extension).  Each carries its own palette and is shown with it.
 *
 *  Gameplay palette
 *      The zones share one 256-colour palette that MM.EXE builds in code -
 *      no palette table is stored in the binary or the raw tilesheets.  It
 *      was reconstructed from the reference screenshot (see palette.h) and
 *      is exact for every colour that appears there.  Sprite/tile pixels
 *      with palette index 0 are transparent (the colour key).
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
#define PAL_TRANSPARENT 0             /* index 0 is the sprite/tile colour key */

/* level byte V is one-based: it maps to tilesheet bitmap (V-1); V==0 is air */
#define LVL_TILE(v)   ((int)(v) - 1)
/* tile indices with gameplay meaning (shared tilesheet layout across zones) */
#define TILE_PLAYER   0               /* mushroom: marks the player start */
#define TILE_ENEMY    1               /* grey toadstool / skull */
#define TILE_GEM      2               /* collectible gem */
#define TILE_SPIKES   4               /* deadly spikes */

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
void game_palette_load(Palette *out);            /* fill from reconstructed table */
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
/* ============================================================
 * Physics recovered from MM.EXE (see README "Recovered mechanics").
 * All motion is 16-bit fixed-point: FP (64) units == 1 pixel.
 * The original is vsync-locked to VGA mode 13h == ~70 Hz.
 * ============================================================ */
#define FP            64              /* fixed-point units per pixel */
#define TICK_HZ       70              /* vsync-locked frame rate      */

#define P_ACCEL       4               /* horizontal accel per frame while held */
#define P_VXMAX       240             /* horizontal speed cap (3.75 px/frame)   */
#define P_FRICT       8               /* friction per frame (grounded, no input) */
#define P_JUMP        (-200)          /* initial jump velocity                  */
#define P_VARJUMP     12              /* added to vy each frame ALT released while rising */
#define P_GRAV        4               /* gravity per frame                      */
#define P_VYMAX       200             /* terminal fall velocity                 */
#define E_GRAV        1               /* enemy gravity per frame                */
#define E_VYMAX       0x40            /* enemy terminal fall velocity           */

#define WORLD_MINX    0
#define WORLD_MAXX    0x4ac0          /* px clamp (== 299 px)                    */
#define WORLD_MINY    0x100           /* py clamp top (== 4 px)                  */
#define WORLD_MAXY    0x2cc0          /* py clamp bottom (== 179 px)             */
#define FALL_DEATH_PX 175             /* pixel-y past which the player drowns    */
#define GEMS_TO_WIN   5

typedef struct {
    int  x, y, vx, vy, speed;         /* fixed-point; matches the original 5-word struct */
    bool alive;
} Enemy;

typedef struct { int col, row; } GemPos;   /* a candidate gem cell */

#define MAX_ENEMIES 64
#define MAX_GEMS    64

typedef struct {
    const Zone *zone;
    const Palette *pal;
    int   level;
    /* player (fixed-point) */
    int   px, py, pvx, pvy;
    int   start_col, start_row;       /* from the value-1 (tile-0) marker */
    bool  on_ground, jumping;
    /* gems: one shown at a time at a random candidate cell */
    int   gems_collected;
    int   gems_needed;                /* min(GEMS_TO_WIN, ngempos) */
    GemPos gempos[MAX_GEMS];
    int   ngempos;
    int   cur_gem;                    /* index into gempos, -1 = none */
    Enemy enemies[MAX_ENEMIES];
    int   nenemies;
    int   spawn_side;                 /* alternates enemy respawn side */
    bool  solid[ROWS][COLS];          /* solid iff tile index 3 or 15   */
    unsigned rng;
} Game;

void game_start_level(Game *g, int level);
void game_respawn(Game *g);
void game_init(Game *g, const Zone *z, const Palette *pal);
/* advance one ~70Hz tick; input flags are held-key states. returns event. */
typedef enum { EV_NONE, EV_DIED, EV_GAMEOVER, EV_LEVEL_CLEAR, EV_ZONE_CLEAR } GameEvent;
GameEvent game_tick(Game *g, bool left, bool right, bool jump);
void game_render(Game *g, Frame *f);

#endif /* MUSH_H */

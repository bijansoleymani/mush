/* Magic Mushroom game logic — ported from the decompiled MM.EXE.
 *
 * The mechanics and every constant below were recovered by decompiling the
 * original 16-bit DOS binary (Ghidra; the physics lives in FUN_1000_01e2).
 * Motion is 16-bit fixed-point: FP (64) units == 1 pixel, updated at the
 * VGA vsync rate (~70 Hz).  See README "Recovered mechanics" and mush.h.
 *
 * Level codes are one-based (tile = value-1).  Gameplay meaning of a cell:
 *   value 1  -> player start          value 4 or 16 -> solid floor
 *   value 2  -> toadstool enemy       value 3       -> gem candidate cell
 *   value 0x15/0x16/0x17 -> enemy markers (differing speeds)
 * Collision is vertical-only (land on solid tops; one-way platforms);
 * horizontal motion is bounded only by the screen edges.  Spikes are NOT
 * deadly in the demo — death comes only from falling or touching an enemy,
 * and there are no lives (infinite respawns on the same level).
 */
#include "mush.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TILE_FP  (TILE * FP)          /* 1280 == one tile in fixed-point (0x500) */
#define PLAYER   TILE                 /* player sprite/box is 20x20 */

/* deterministic per-level RNG (same idea as the original's rand()) */
static unsigned rnd(Game *g) { g->rng = g->rng * 1103515245u + 12345u; return (g->rng >> 16) & 0x7FFF; }

static uint8_t mapval(const Game *g, int c, int r)   /* raw one-based level byte */
{
    return g->zone->levels[g->level * LEVEL_BYTES + r * COLS + c];
}

static bool solid_cell(const Game *g, int c, int r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return false;
    return g->solid[r][c];
}

/* Collision footprint (from FUN_1000_1ab5): the floor is probed over a
 * COLL_W-pixel horizontal span starting `xoff` pixels in from the box's left
 * edge — so a box can overhang an edge until this span clears the platform. */
#define COLL_W  10

static bool solid_span(const Game *g, int px_pixel, int row, int xoff)
{
    for (int k = xoff; k < xoff + COLL_W; k++)
        if (solid_cell(g, (px_pixel + k) / TILE, row)) return true;
    return false;
}

/* One-way floor collision, matching FUN_1000_1ab5: look 0..3 px below the
 * feet for a solid tile and clamp *downward* velocity so the box settles
 * exactly on the tile top over a frame or two.  It NEVER moves the box up, so
 * stepping off a ledge just falls (no snap-back onto the platform). */
static void land(const Game *g, int x_fp, int y_fp, int *vy, int h_px, int xoff,
                 bool *on_ground, bool *jumping)
{
    *on_ground = false;
    if (*vy < 0) return;                          /* rising: platforms are one-way */
    int px   = x_fp >> 6;
    int feet = (y_fp >> 6) + h_px;                /* feet pixel (bottom edge)      */
    int dist = (TILE - feet % TILE) % TILE;       /* px down to next tile boundary */
    if (dist > 3) return;                         /* boundary >3px away: keep falling (matches the 0..3 lookahead) */
    int row  = (feet + dist) / TILE;              /* tile row at/below the boundary */
    if (!solid_span(g, px, row, xoff)) return;
    if (*vy > dist * FP) *vy = dist * FP;         /* don't overshoot the tile top  */
    if (dist == 0) { *vy = 0; *on_ground = true; if (jumping) *jumping = false; }
}

/* ---------------- level construction ---------------- */

static void set_current_gem(Game *g)
{
    if (g->ngempos <= 0) { g->cur_gem = -1; return; }
    int n, tries = 0;
    do { n = rnd(g) % g->ngempos; } while (n == g->cur_gem && g->ngempos > 1 && ++tries < 64);
    g->cur_gem = n;
}

/* (re)build the static + entity state from the level bytes: solidity, enemy
 * spawns (at their original cells), gem candidate cells and the start cell.
 * The original re-scans the map on every (re)start, so death puts the enemies
 * back exactly where they began. */
static void parse_level(Game *g)
{
    g->nenemies = 0; g->ngempos = 0; g->spawn_side = 0;
    int start_c = -1, start_r = -1;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint8_t v = mapval(g, c, r);
            g->solid[r][c] = (v == 4 || v == 0x10);   /* grass-brick tiles (3 & 15) */

            if (v == 1) { start_c = c; start_r = r; }  /* player start */
            else if (v == 3) {                         /* gem candidate cell */
                if (g->ngempos < MAX_GEMS) {
                    g->gempos[g->ngempos].col = c;
                    g->gempos[g->ngempos].row = r; g->ngempos++;
                }
            } else if (r != 0) {                       /* enemies never in top row */
                int speed = 0;
                if (v == 2)          speed = 0x20;     /* toadstool */
                else if (v == 0x15)  speed = 0x40;
                else if (v == 0x16)  speed = 0x30;
                else if (v == 0x17)  speed = 0x10;
                if (speed && g->nenemies < MAX_ENEMIES) {
                    Enemy *e = &g->enemies[g->nenemies++];
                    e->x = c * TILE_FP; e->y = r * TILE_FP;
                    e->vx = speed; e->vy = 0; e->speed = speed; e->alive = true;
                }
            }
        }
    if (start_c < 0) { start_c = 1; start_r = 1; }      /* fallback */
    g->start_col = start_c; g->start_row = start_r;
}

static void reset_player(Game *g)
{
    g->px = g->start_col * TILE_FP; g->py = g->start_row * TILE_FP;
    g->pvx = g->pvy = 0; g->on_ground = false; g->jumping = false;
    g->dying = false; g->death_step = 0;
    g->gems_collected = 0;
    g->cur_gem = -1; set_current_gem(g);
}

void game_start_level(Game *g, int level)
{
    g->level = level;
    g->rng = (unsigned)(level + 1) * 2654435761u;
    parse_level(g);
    g->gems_needed = GEMS_TO_WIN;                       /* gems respawn until 5 */
    reset_player(g);
}

void game_init(Game *g, const Zone *z, const Palette *pal)
{
    memset(g, 0, sizeof(*g));
    g->zone = z; g->pal = pal;
    game_start_level(g, 0);
}

void game_respawn(Game *g)   /* death: rebuild enemies at spawn, restart level */
{
    parse_level(g);          /* enemies back at their original spots */
    reset_player(g);         /* rng keeps running, so the next gem differs */
}

/* Advance the death dissolve: eat ~40 random pixels out of the 28x28 box each
 * call (matching the ~1600 total the original scatters); returns 1 when done. */
int game_death_step(Game *g)
{
    for (int k = 0; k < 40; k++) {
        int dx = rnd(g) % DISSOLVE, dy = rnd(g) % DISSOLVE;
        g->dissolve[dy * DISSOLVE + dx] = 1;
    }
    return ++g->death_step >= 40;
}

static GameEvent die(Game *g, int ppx, int ppy)
{
    g->dying = true; g->death_px = ppx; g->death_py = ppy; g->death_step = 0;
    memset(g->dissolve, 0, sizeof g->dissolve);
    return EV_DIED;
}

/* ---------------- per-tick simulation (one ~70Hz frame) ---------------- */

static void update_enemy(Game *g, Enemy *e)
{
    if (!e->alive) return;
    if (e->x < 0x40)       e->vx =  e->speed;          /* bounce off walls */
    if (e->x > WORLD_MAXX) e->vx = -e->speed;
    if (e->y < 0x140)      e->vy = 0;

    if (e->y > 0x2bc0) {                               /* fell off -> respawn at top */
        e->y = 0x140; e->vy = 0;
        if (g->spawn_side == 0) { e->x = e->speed;   e->vx =  e->speed; }
        else                    { e->x = WORLD_MAXX; e->vx = -e->speed; }
        g->spawn_side ^= 1;
    }
    /* gravity + one-way floor, then move */
    e->vy += E_GRAV;
    bool og, dummy = false;
    land(g, e->x, e->y, &e->vy, PLAYER, 2, &og, &dummy);
    if (e->vy > E_VYMAX) e->vy = E_VYMAX;
    e->x += e->vx; e->y += e->vy;
}

GameEvent game_tick(Game *g, bool left, bool right, bool jump)
{
    /* --- horizontal: dynamic accel (LEFT/RIGHT shift), friction on ground --- */
    if (left  && g->pvx > -P_VXMAX) g->pvx -= P_ACCEL;
    if (right && g->pvx <  P_VXMAX) g->pvx += P_ACCEL;
    if (!left && !right && g->on_ground) {
        if (g->pvx < 0) { g->pvx += P_FRICT; if (g->pvx > 0) g->pvx = 0; }
        if (g->pvx > 0) { g->pvx -= P_FRICT; if (g->pvx < 0) g->pvx = 0; }
    }

    /* --- jump (ALT) with variable height --- */
    if (!g->jumping && g->pvy < 1 && jump) { g->jumping = true; g->pvy = P_JUMP; }
    if (!jump && g->pvy < 0) g->pvy += P_VARJUMP;

    /* --- gravity + landing --- */
    g->pvy += P_GRAV; if (g->pvy > P_VYMAX) g->pvy = P_VYMAX;
    land(g, g->px, g->py, &g->pvy, PLAYER, 4, &g->on_ground, &g->jumping);

    /* --- integrate + screen bounds --- */
    g->px += g->pvx; g->py += g->pvy;
    if (g->px < WORLD_MINX) { g->px = WORLD_MINX; g->pvx = 0; }
    if (g->px > WORLD_MAXX) { g->px = WORLD_MAXX; g->pvx = 0; }
    if (g->py < WORLD_MINY) { g->py = WORLD_MINY; g->pvy = 0; }
    if (g->py > WORLD_MAXY) { g->py = WORLD_MAXY; g->pvy = 0; }

    int ppx = g->px >> 6, ppy = g->py >> 6;

    /* --- death: fell off the bottom --- */
    if (ppy > FALL_DEATH_PX) return die(g, ppx, ppy);

    /* --- gem: one shown at a time; 5 collected clears the level --- */
    if (g->cur_gem >= 0) {
        int gx = g->gempos[g->cur_gem].col * TILE;
        int gy = g->gempos[g->cur_gem].row * TILE;
        if (gx < ppx + PLAYER && ppx < gx + PLAYER &&
            gy < ppy + PLAYER && ppy < gy + PLAYER) {
            g->gems_collected++;
            if (g->gems_collected >= g->gems_needed)
                return (g->level + 1 >= g->zone->nlevels) ? EV_ZONE_CLEAR : EV_LEVEL_CLEAR;
            set_current_gem(g);
        }
    }

    /* --- enemies: move, then kill the player on ~18x15 overlap --- */
    for (int i = 0; i < g->nenemies; i++) {
        Enemy *e = &g->enemies[i];
        update_enemy(g, e);
        if (!e->alive) continue;
        int ex = e->x >> 6, ey = e->y >> 6;
        if (ex < ppx + 0x12 && ppx < ex + 0x12 &&
            ey < ppy + 0x0f && ppy < ey + 0x14) return die(g, ppx, ppy);
    }
    return EV_NONE;
}

/* ---------------- rendering ---------------- */

void game_render(Game *g, Frame *f)
{
    fb_clear(f, 0xFF000000u);
    fb_draw_level(f, g->zone, g->pal, g->level);

    /* the single active gem (tile 2) */
    if (g->cur_gem >= 0)
        fb_blit_tile(f, g->zone, g->pal, TILE_GEM,
                     g->gempos[g->cur_gem].col * TILE, g->gempos[g->cur_gem].row * TILE);

    /* enemies: grey toadstool / skull sprite (tile 1) */
    for (int i = 0; i < g->nenemies; i++)
        if (g->enemies[i].alive)
            fb_blit_tile(f, g->zone, g->pal, TILE_ENEMY,
                         g->enemies[i].x >> 6, g->enemies[i].y >> 6);

    /* player: red-and-white mushroom (tile 0); dissolves away on death */
    if (g->dying)
        fb_blit_tile_dissolve(f, g->zone, g->pal, TILE_PLAYER,
                              g->death_px, g->death_py, g->dissolve, DISSOLVE, 4, 4);
    else
        fb_blit_tile(f, g->zone, g->pal, TILE_PLAYER, g->px >> 6, g->py >> 6);

    /* HUD */
    fb_rect(f, 0, 0, SCREEN_W, 9, 0xC0000000u);
    char buf[32];
    snprintf(buf, sizeof buf, "GEMS %d/%d", g->gems_collected, g->gems_needed);
    fb_text(f, 2, 1, buf, 0xFFFFE000u);
    snprintf(buf, sizeof buf, "LEVEL %d", g->level + 1);
    fb_text_center(f, 1, buf, 0xFFFFFFFFu);
}

/* Magic Mushroom game logic: level setup, mushroom physics, toadstool
 * enemies, gems, collision and win/lose rules.
 *
 * Design notes (what is faithful vs. reconstructed):
 *  - Tile semantics are reconstructed, since the original collision tables
 *    live only in MM.EXE's machine code.  The rule used here is simple and
 *    keeps every level traversable:
 *        code 0            -> air
 *        code 4            -> spikes (deadly)   [tile 4 is spikes in all zones]
 *        code 1..ntiles-1  -> solid platform    [everything you see, you can stand on]
 *        code >= ntiles    -> entity marker: if that marker is dense in the
 *                             level it is a static deadly hazard field
 *                             (e.g. OASIS's "death" tiles); if sparse it is a
 *                             patrolling grey toadstool.
 *  - Controls, palette, sprites, level layouts and gem count are all original.
 */
#include "mush.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- tuning (60 Hz fixed step, pixel units) ---- */
#define PW 14                 /* player collision box */
#define PH 18
#define EW 16                 /* enemy collision box  */
#define EH 16
#define GRAVITY      0.30f
#define MAX_FALL     6.0f
#define MOVE_ACCEL   0.35f    /* "dynamic" accel: the longer held, the faster */
#define MOVE_MAX     3.2f
#define GND_FRICTION 0.70f
#define AIR_FRICTION 0.92f
#define JUMP_VEL     (-5.7f)
#define ENEMY_SPEED  0.8f
#define HAZARD_MIN   6        /* >= this many identical markers => hazard field */
#define GEM_R        7.0f

/* deterministic per-level RNG so gem layouts are stable */
static unsigned rnd(Game *g) { g->rng = g->rng * 1103515245u + 12345u; return (g->rng >> 16) & 0x7FFF; }

static uint8_t mapcode(const Game *g, int c, int r)
{
    return g->zone->levels[g->level * LEVEL_BYTES + r * COLS + c];
}

static CellKind cell_at(const Game *g, int c, int r)
{
    if (c < 0 || c >= COLS) return CELL_SOLID;   /* side walls keep you in */
    if (r < 0)  return CELL_AIR;                 /* open sky above */
    if (r >= ROWS) return CELL_AIR;              /* below floor = fall out */
    return g->cell[r][c];
}

static bool box_solid(const Game *g, float x, float y, int w, int h)
{
    int c0 = (int)floorf(x / TILE), c1 = (int)floorf((x + w - 1) / TILE);
    int r0 = (int)floorf(y / TILE), r1 = (int)floorf((y + h - 1) / TILE);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            if (cell_at(g, c, r) == CELL_SOLID) return true;
    return false;
}

static bool point_deadly(const Game *g, float x, float y)
{
    return cell_at(g, (int)floorf(x / TILE), (int)floorf(y / TILE)) == CELL_DEADLY;
}

/* Pick a sensible player start: a standable air cell (solid directly below,
 * never above spikes) with at least two cells of headroom, so we never spawn
 * inside a cramped one-tile pocket.  Prefer low and left; relax if needed. */
static void find_start(const Game *g, int *out_c, int *out_r)
{
    for (int need = 2; need >= 0; need--) {
        int best_c = -1, best_r = -1;
        for (int r = ROWS - 1; r >= 0; r--)
            for (int c = 0; c < COLS; c++) {
                if (g->cell[r][c] != CELL_AIR) continue;
                if (cell_at(g, c, r + 1) != CELL_SOLID) continue;
                int head = 0;
                for (int h = 1; h <= need; h++)
                    if (cell_at(g, c, r - h) == CELL_AIR) head++;
                if (head < need) continue;
                best_c = c; best_r = r; goto done;    /* lowest row, left-most */
            }
    done:
        if (best_c >= 0) { *out_c = best_c; *out_r = best_r; return; }
    }
    *out_c = 1; *out_r = 1;                            /* last resort */
}

/* ---------------- level construction ---------------- */

void game_start_level(Game *g, int level)
{
    const int NT = g->zone->ntiles;
    g->level = level;
    g->rng = (unsigned)(level + 1) * 2654435761u;
    g->nenemies = 0; g->ngems = 0; g->gems_collected = 0;
    int start_c = -1, start_r = -1;

    /* count each entity-marker value to tell dense hazard fields from the
     * sparse markers that spawn a single roaming enemy */
    int markcount[256] = {0};
    for (int i = 0; i < LEVEL_BYTES; i++) {
        uint8_t v = g->zone->levels[level * LEVEL_BYTES + i];
        if (v && LVL_TILE(v) >= NT) markcount[v]++;
    }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint8_t v = mapcode(g, c, r);
            if (v == 0) { g->cell[r][c] = CELL_AIR; continue; }
            int t = LVL_TILE(v);

            if (t >= NT) {                            /* entity marker */
                if (markcount[v] >= HAZARD_MIN) {
                    g->cell[r][c] = CELL_DEADLY;      /* static hazard field */
                } else {
                    g->cell[r][c] = CELL_AIR;
                    if (g->nenemies < MAX_ENEMIES) {
                        Enemy *e = &g->enemies[g->nenemies++];
                        e->x = c * TILE + (TILE - EW) / 2.0f;
                        e->y = r * TILE + (TILE - EH);
                        e->vx = e->vy = 0;
                        e->dir = (rnd(g) & 1) ? 1 : -1; e->alive = true;
                    }
                }
            } else if (t == TILE_PLAYER) {            /* player start marker */
                g->cell[r][c] = CELL_AIR; start_c = c; start_r = r;
            } else if (t == TILE_ENEMY) {             /* toadstool/skull */
                g->cell[r][c] = CELL_AIR;
                if (g->nenemies < MAX_ENEMIES) {
                    Enemy *e = &g->enemies[g->nenemies++];
                    e->x = c * TILE + (TILE - EW) / 2.0f;
                    e->y = r * TILE + (TILE - EH);
                    e->vx = e->vy = 0;
                    e->dir = (rnd(g) & 1) ? 1 : -1; e->alive = true;
                }
            } else if (t == TILE_GEM) {               /* collectible gem */
                g->cell[r][c] = CELL_AIR;
                if (g->ngems < MAX_GEMS) {
                    g->gems[g->ngems].x = c * TILE + TILE / 2.0f;
                    g->gems[g->ngems].y = r * TILE + TILE / 2.0f;
                    g->gems[g->ngems].active = true; g->ngems++;
                }
            } else if (t == TILE_SPIKES) {            /* spikes */
                g->cell[r][c] = CELL_DEADLY;
            } else {                                  /* solid terrain */
                g->cell[r][c] = CELL_SOLID;
            }
        }

    g->gems_needed = g->ngems < GEMS_TO_WIN ? g->ngems : GEMS_TO_WIN;
    if (g->gems_needed < 1) g->gems_needed = 1;       /* avoid instant clear */

    /* player start: the tile-0 marker, else a safe fallback */
    if (start_c < 0) find_start(g, &start_c, &start_r);
    g->start_x = start_c * TILE + (TILE - PW) / 2.0f;
    g->start_y = start_r * TILE + (TILE - PH);
    g->px = g->start_x; g->py = g->start_y;
    g->pvx = g->pvy = 0; g->on_ground = true;
}

void game_init(Game *g, const Zone *z, const Palette *pal)
{
    memset(g, 0, sizeof(*g));
    g->zone = z; g->pal = pal;
    g->lives = 3;
    game_start_level(g, 0);
}

void game_respawn(Game *g)   /* after a death, same level, keep gem progress */
{
    g->px = g->start_x; g->py = g->start_y;
    g->pvx = g->pvy = 0; g->on_ground = true;
}

/* ---------------- per-tick simulation ---------------- */

static void move_axis(Game *g, float dx, float dy)
{
    /* X in sub-pixel steps */
    int n = (int)ceilf(fabsf(dx)); if (n < 1) n = 1;
    float s = dx / n;
    for (int i = 0; i < n; i++) {
        if (box_solid(g, g->px + s, g->py, PW, PH)) { g->pvx = 0; break; }
        g->px += s;
    }
    /* Y in sub-pixel steps */
    n = (int)ceilf(fabsf(dy)); if (n < 1) n = 1;
    s = dy / n;
    for (int i = 0; i < n; i++) {
        if (box_solid(g, g->px, g->py + s, PW, PH)) {
            if (dy > 0) g->on_ground = true;
            g->pvy = 0; break;
        }
        g->py += s;
    }
}

static void update_enemy(Game *g, Enemy *e)
{
    if (!e->alive) return;
    /* gravity */
    e->vy += GRAVITY; if (e->vy > MAX_FALL) e->vy = MAX_FALL;
    int n = (int)ceilf(fabsf(e->vy)); if (n < 1) n = 1;
    float s = e->vy / n;
    for (int i = 0; i < n; i++) {
        if (box_solid(g, e->x, e->y + s, EW, EH)) { e->vy = 0; break; }
        e->y += s;
    }
    /* patrol: reverse at a wall or the edge of a platform */
    float nx = e->x + e->dir * ENEMY_SPEED;
    bool wall = box_solid(g, nx, e->y, EW, EH);
    float footx = (e->dir > 0) ? nx + EW : nx;
    bool ledge = cell_at(g, (int)floorf(footx / TILE),
                            (int)floorf((e->y + EH + 1) / TILE)) != CELL_SOLID;
    if (wall || ledge) e->dir = -e->dir;
    else e->x = nx;
}

GameEvent game_tick(Game *g, bool left, bool right, bool jump)
{
    /* --- horizontal: dynamic acceleration while a direction is held --- */
    if (left  && !right) g->pvx -= MOVE_ACCEL;
    if (right && !left)  g->pvx += MOVE_ACCEL;
    if (!(left ^ right)) g->pvx *= (g->on_ground ? GND_FRICTION : AIR_FRICTION);
    if (g->pvx >  MOVE_MAX) g->pvx =  MOVE_MAX;
    if (g->pvx < -MOVE_MAX) g->pvx = -MOVE_MAX;

    /* --- jump --- */
    if (jump && g->on_ground) { g->pvy = JUMP_VEL; g->on_ground = false; }

    /* --- gravity --- */
    g->pvy += GRAVITY; if (g->pvy > MAX_FALL) g->pvy = MAX_FALL;

    move_axis(g, g->pvx, g->pvy);
    g->on_ground = box_solid(g, g->px, g->py + 1.0f, PW, PH);

    /* --- death: fell off the bottom --- */
    if (g->py > SCREEN_H + 4) return EV_DIED;

    /* --- death: touched spikes / hazard (sample feet + belly) --- */
    float cx = g->px + PW / 2.0f;
    if (point_deadly(g, cx, g->py + PH - 2) ||
        point_deadly(g, cx, g->py + PH / 2.0f)) return EV_DIED;

    /* --- enemies --- */
    for (int i = 0; i < g->nenemies; i++) {
        Enemy *e = &g->enemies[i];
        update_enemy(g, e);
        if (e->alive &&
            g->px < e->x + EW && g->px + PW > e->x &&
            g->py < e->y + EH && g->py + PH > e->y) return EV_DIED;
    }

    /* --- gems (placed in the level; collect gems_needed of them) --- */
    for (int i = 0; i < g->ngems; i++) {
        if (!g->gems[i].active) continue;
        float dx = g->gems[i].x - cx, dy = g->gems[i].y - (g->py + PH / 2.0f);
        if (dx*dx + dy*dy < (GEM_R + PW/2)*(GEM_R + PW/2)) {
            g->gems[i].active = false;
            g->gems_collected++;
            if (g->gems_collected >= g->gems_needed) {
                return (g->level + 1 >= g->zone->nlevels) ? EV_ZONE_CLEAR
                                                          : EV_LEVEL_CLEAR;
            }
        }
    }
    return EV_NONE;
}

/* ---------------- rendering ---------------- */

void game_render(Game *g, Frame *f)
{
    fb_clear(f, 0xFF000000u);
    fb_draw_level(f, g->zone, g->pal, g->level);

    /* entity-marker hazard fields (no tile bitmap) get a faint red wash */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint8_t v = mapcode(g, c, r);
            if (g->cell[r][c] == CELL_DEADLY && v && LVL_TILE(v) >= g->zone->ntiles)
                fb_rect(f, c * TILE, r * TILE, TILE, TILE, 0xFF901010u);
        }

    /* gems (tile 2) still to be collected */
    for (int i = 0; i < g->ngems; i++)
        if (g->gems[i].active)
            fb_blit_tile(f, g->zone, g->pal, TILE_GEM,
                         (int)g->gems[i].x - TILE/2, (int)g->gems[i].y - TILE/2);

    /* enemies: the grey toadstool / skull sprite (tile 1) */
    for (int i = 0; i < g->nenemies; i++)
        if (g->enemies[i].alive)
            fb_blit_tile(f, g->zone, g->pal, TILE_ENEMY,
                         (int)g->enemies[i].x - (TILE-EW)/2,
                         (int)g->enemies[i].y - (TILE-EH));

    /* player: red-and-white mushroom (tile 0) */
    fb_blit_tile(f, g->zone, g->pal, TILE_PLAYER,
                 (int)g->px - (TILE-PW)/2, (int)g->py - (TILE-PH));

    /* HUD */
    fb_rect(f, 0, 0, SCREEN_W, 9, 0xC0000000u);
    char buf[32];
    snprintf(buf, sizeof buf, "GEMS %d/%d", g->gems_collected, g->gems_needed);
    fb_text(f, 2, 1, buf, 0xFFFFE000u);
    snprintf(buf, sizeof buf, "LEVEL %d", g->level + 1);
    fb_text_center(f, 1, buf, 0xFFFFFFFFu);
    snprintf(buf, sizeof buf, "LIVES %d", g->lives);
    fb_text(f, SCREEN_W - 8*8 - 2, 1, buf, 0xFF80FF80u);
}

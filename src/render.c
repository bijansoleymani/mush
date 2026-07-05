/* Software framebuffer rendering: everything is drawn into a 320x200
 * 32bpp Frame, exactly like writing to VGA mode-13h memory, then the
 * main loop uploads it to an SDL texture and scales it up. */
#include "mush.h"
#include "font.h"
#include <string.h>

/* Reproduces MM.EXE's runtime palette generator (in FUN_1000_01e2), rather
 * than a table inferred from screenshots.  The zones share one 256-colour
 * palette built as a 6x6x6 colour cube at DAC indices 16..231: red and blue
 * step through {0,12,24,36,48,60} while green is trunc(inner*0.8), giving
 * {0,9,19,28,38,48} (the 0.8 is an IEEE double the original FMULs by).  The
 * game then sets index 1 and 232..255 to the blue (0,15,30); indices 2..4 are
 * the animated water-blue (shown at its base shade here); index 0 is the
 * transparent black; 5..15 keep the default VGA 16-colour palette.  DAC values
 * are 6-bit and scaled to 8-bit for a modern display.  Menus keep their PCX
 * palettes. */
void game_palette_load(Palette *out)
{
    static const int STEP[6] = { 0, 12, 24, 36, 48, 60 };
    static const uint8_t VGA16[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };
    #define SET6(i, R, G, B) do {                                      \
        uint8_t r_ = (uint8_t)(((R) * 255 + 31) / 63);                 \
        uint8_t g_ = (uint8_t)(((G) * 255 + 31) / 63);                 \
        uint8_t b_ = (uint8_t)(((B) * 255 + 31) / 63);                 \
        out->r[i] = r_; out->g[i] = g_; out->b[i] = b_;               \
        out->argb[i] = 0xFF000000u | (r_ << 16) | (g_ << 8) | b_;      \
    } while (0)

    for (int i = 0; i < 16; i++) SET6(i, VGA16[i][0], VGA16[i][1], VGA16[i][2]);

    int idx = 16;                                    /* the 6x6x6 cube */
    for (int r = 0; r < 6; r++)
        for (int b = 0; b < 6; b++)
            for (int c = 0; c < 6; c++) {
                int g = (int)(STEP[c] * 0.8);        /* trunc -> 0,9,19,28,38,48 */
                SET6(idx, STEP[r], g, STEP[b]); idx++;
            }

    SET6(1, 0, 15, 30);
    for (int i = 2;   i < 5;   i++) SET6(i, 0, 15, 30);   /* animated water base */
    for (int i = 232; i < 256; i++) SET6(i, 0, 15, 30);
    #undef SET6
}

/* Cycle the shimmering water/gem colours, reproducing the palette animation in
 * FUN_1000_01e2: a bright shade (0,30,50) walks through two groups of DAC
 * indices — {232,240,247} and {2,3,4} — one step every 16 frames (a 48-frame
 * loop); the rest sit at the dim base (0,15,30). */
void game_palette_animate(Palette *pal, unsigned tick)
{
    static const int g1[3] = { 232, 240, 247 };
    static const int g2[3] = {   2,   3,   4 };
    int phase = (tick / 16) % 3;
    for (int i = 0; i < 3; i++) {
        int G = (i == phase) ? 30 : 15;                  /* bright vs dim (6-bit) */
        int B = (i == phase) ? 50 : 30;
        uint8_t g_ = (uint8_t)((G * 255 + 31) / 63);
        uint8_t b_ = (uint8_t)((B * 255 + 31) / 63);
        uint32_t c = 0xFF000000u | (g_ << 8) | b_;       /* red channel is 0 */
        for (int k = 0; k < 2; k++) {
            int idx = k ? g2[i] : g1[i];
            pal->r[idx] = 0; pal->g[idx] = g_; pal->b[idx] = b_; pal->argb[idx] = c;
        }
    }
}

void fb_clear(Frame *f, uint32_t argb)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) f->px[i] = argb;
}

void fb_rect(Frame *f, int x, int y, int w, int h, uint32_t argb)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SCREEN_H) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SCREEN_W) continue;
            f->px[yy * SCREEN_W + xx] = argb;
        }
    }
}

void fb_blit_pcx(Frame *f, const Pcx *img)
{
    int ox = (SCREEN_W - img->w) / 2;
    int oy = (SCREEN_H - img->h) / 2;
    fb_clear(f, 0xFF000000u);
    for (int y = 0; y < img->h; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < img->w; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= SCREEN_W) continue;
            f->px[dy * SCREEN_W + dx] = img->pal.argb[img->pix[y * img->w + x]];
        }
    }
}

/* blend two 0xAARRGGBB colours 50/50 (used for tinting enemies grey) */
static uint32_t mix(uint32_t a, uint32_t b)
{
    return 0xFF000000u |
           ((((a >> 16 & 0xFF) + (b >> 16 & 0xFF)) >> 1) << 16) |
           ((((a >>  8 & 0xFF) + (b >>  8 & 0xFF)) >> 1) <<  8) |
           ((((a       & 0xFF) + (b       & 0xFF)) >> 1));
}

static void blit_tile(Frame *f, const Zone *z, const Palette *pal,
                      int idx, int px, int py, bool tinted, uint32_t tint)
{
    if (idx < 0 || idx >= z->ntiles) return;
    const uint8_t *t = z->tiles + (size_t)idx * TILE_BYTES;
    for (int y = 0; y < TILE; y++) {
        int dy = py + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < TILE; x++) {
            uint8_t c = t[y * TILE + x];
            if (c == PAL_TRANSPARENT) continue;         /* colour key */
            int dx = px + x;
            if (dx < 0 || dx >= SCREEN_W) continue;
            uint32_t col = pal->argb[c];
            if (tinted) col = mix(col, tint);
            f->px[dy * SCREEN_W + dx] = col;
        }
    }
}

void fb_blit_tile(Frame *f, const Zone *z, const Palette *pal,
                  int idx, int px, int py)
{ blit_tile(f, z, pal, idx, px, py, false, 0); }

void fb_blit_tile_tinted(Frame *f, const Zone *z, const Palette *pal,
                         int idx, int px, int py, uint32_t tint)
{ blit_tile(f, z, pal, idx, px, py, true, tint); }

/* Like fb_blit_tile but skip any pixel whose slot in `mask` (a mw-wide grid,
 * the sprite sitting at offset ox,oy inside it) is set — used for the death
 * dissolve, where skipped pixels reveal the level drawn behind the mushroom. */
void fb_blit_tile_dissolve(Frame *f, const Zone *z, const Palette *pal,
                           int idx, int px, int py,
                           const uint8_t *mask, int mw, int ox, int oy)
{
    if (idx < 0 || idx >= z->ntiles) return;
    const uint8_t *t = z->tiles + (size_t)idx * TILE_BYTES;
    for (int y = 0; y < TILE; y++) {
        int dy = py + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < TILE; x++) {
            uint8_t c = t[y * TILE + x];
            if (c == PAL_TRANSPARENT) continue;
            if (mask[(y + oy) * mw + (x + ox)]) continue;   /* dissolved away */
            int dx = px + x;
            if (dx < 0 || dx >= SCREEN_W) continue;
            f->px[dy * SCREEN_W + dx] = pal->argb[c];
        }
    }
}

void fb_draw_level(Frame *f, const Zone *z, const Palette *pal, int level)
{
    const uint8_t *map = z->levels + (size_t)level * LEVEL_BYTES;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint8_t v = map[r * COLS + c];
            if (v == 0) continue;                       /* air */
            int t = LVL_TILE(v);
            if (t >= z->ntiles) continue;               /* entity marker */
            if (t == TILE_PLAYER || t == TILE_ENEMY || t == TILE_GEM)
                continue;                               /* drawn as live sprites */
            fb_blit_tile(f, z, pal, t, c * TILE, r * TILE);
        }
}

/* ---- 8x8 bitmap text ---- */
void fb_text(Frame *f, int x, int y, const char *s, uint32_t argb)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;           /* fold to uppercase */
        if (ch >= FONT_LO && ch <= FONT_HI) {
            const unsigned char *g = FONT8X8[ch - FONT_LO];
            for (int ry = 0; ry < 8; ry++)
                for (int rx = 0; rx < 8; rx++)
                    if (g[ry] & (0x80 >> rx))
                        fb_rect(f, x + rx, y + ry, 1, 1, argb);
        }
        x += 8;
    }
}

void fb_text_center(Frame *f, int y, const char *s, uint32_t argb)
{
    int len = 0; for (const char *p = s; *p; p++) len++;
    fb_text(f, (SCREEN_W - len * 8) / 2, y, s, argb);
}

/* Software framebuffer rendering: everything is drawn into a 320x200
 * 32bpp Frame, exactly like writing to VGA mode-13h memory, then the
 * main loop uploads it to an SDL texture and scales it up. */
#include "mush.h"
#include "font.h"
#include <string.h>

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

void fb_draw_level(Frame *f, const Zone *z, const Palette *pal, int level)
{
    const uint8_t *map = z->levels + (size_t)level * LEVEL_BYTES;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint8_t v = map[r * COLS + c];
            if (v == 0 || v >= z->ntiles) continue;     /* air / entity marker */
            fb_blit_tile(f, z, pal, v, c * TILE, r * TILE);
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

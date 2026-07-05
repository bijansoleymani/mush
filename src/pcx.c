/* PCX loader: 256-colour, 8-bit, RLE (ZSoft PCX v5, VGA palette at tail).
 * Handles exactly the two menu screens shipped with Magic Mushroom
 * (INTRO.VGA, ZONESLCT.VGA), which are PCX despite the .VGA extension. */
#include "mush.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n);
    if (buf && fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (len) *len = n;
    return buf;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool pcx_load(const char *path, Pcx *out)
{
    long len;
    uint8_t *d = read_file(path, &len);
    if (!d || len < 128 + 769) { free(d); return false; }
    if (d[0] != 0x0A) { free(d); return false; }      /* PCX magic */

    int xmin = rd16(d + 4),  ymin = rd16(d + 6);
    int xmax = rd16(d + 8),  ymax = rd16(d + 10);
    int w = xmax - xmin + 1, h = ymax - ymin + 1;
    int nplanes = d[65];
    int bpr = rd16(d + 66);                            /* bytes per scanline/plane */
    if (w <= 0 || h <= 0 || nplanes != 1) { free(d); return false; }

    out->w = w; out->h = h;
    out->pix = malloc((size_t)w * h);
    if (!out->pix) { free(d); return false; }

    /* --- RLE decode into scanline buffers, then take first w bytes/row --- */
    const uint8_t *body = d + 128;
    const uint8_t *end  = d + (len - 769);             /* palette marker precedes tail */
    size_t total = (size_t)bpr * h;
    uint8_t *raw = malloc(total);
    if (!raw) { free(out->pix); free(d); return false; }
    size_t o = 0;
    while (o < total && body < end) {
        uint8_t b = *body++;
        if ((b & 0xC0) == 0xC0) {                      /* run: low 6 bits = count */
            int cnt = b & 0x3F;
            uint8_t v = (body < end) ? *body++ : 0;
            while (cnt-- && o < total) raw[o++] = v;
        } else {
            raw[o++] = b;
        }
    }
    for (int y = 0; y < h; y++)
        memcpy(out->pix + (size_t)y * w, raw + (size_t)y * bpr, w);
    free(raw);

    /* --- 256-colour palette: last 768 bytes, preceded by 0x0C marker --- */
    const uint8_t *pal = d + (len - 768);
    for (int i = 0; i < 256; i++) {
        uint8_t r = pal[i*3], g = pal[i*3+1], b = pal[i*3+2];
        out->pal.r[i] = r; out->pal.g[i] = g; out->pal.b[i] = b;
        out->pal.argb[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    free(d);
    return true;
}

void pcx_free(Pcx *p)
{
    if (p && p->pix) { free(p->pix); p->pix = NULL; }
}

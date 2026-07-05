/* Loaders for the reverse-engineered .VGA tilesheets and .LVL level maps.
 * See the format notes at the top of mush.h. */
#include "mush.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? n : 1);
    if (buf && fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (len) *len = n;
    return buf;
}

static char *join(const char *dir, const char *name, char *out, size_t n)
{
    if (dir && *dir) snprintf(out, n, "%s/%s", dir, name);
    else             snprintf(out, n, "%s", name);
    return out;
}

bool zone_load(const char *dir, const char *vga, const char *lvl, Zone *out)
{
    char path[512];
    long vlen = 0, llen = 0;
    memset(out, 0, sizeof(*out));

    out->tiles = slurp(join(dir, vga, path, sizeof path), &vlen);
    if (!out->tiles || vlen % TILE_BYTES != 0) {
        fprintf(stderr, "bad tilesheet %s (%ld bytes)\n", path, vlen);
        zone_free(out);
        return false;
    }
    out->ntiles = (int)(vlen / TILE_BYTES);

    out->levels = slurp(join(dir, lvl, path, sizeof path), &llen);
    if (!out->levels || llen % LEVEL_BYTES != 0 || llen == 0) {
        fprintf(stderr, "bad level file %s (%ld bytes)\n", path, llen);
        zone_free(out);
        return false;
    }
    out->nlevels = (int)(llen / LEVEL_BYTES);
    return true;
}

void zone_free(Zone *z)
{
    if (!z) return;
    free(z->tiles);  z->tiles  = NULL;
    free(z->levels); z->levels = NULL;
    z->ntiles = z->nlevels = 0;
}

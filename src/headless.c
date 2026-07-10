/* Headless driver - the whole game minus SDL. Runs the simulation with
 * scripted input and dumps the final rendered frame as a PPM, using nothing
 * but stdio, so it compiles to standalone WASI (wasmtime) as well as native.
 *
 *   mush-wasi [dir] [zone 0-2] [ticks] [out.ppm]
 *   e.g.  wasmtime run --dir . mush-wasi.wasm . 0 350 frame.ppm
 *
 * Input script: hold RIGHT the whole time, tap jump every 50 ticks. Deaths
 * fast-forward the dissolve and respawn, level clears advance, matching the
 * state machine in main.c.
 */
#include "mush.h"
#include <stdio.h>
#include <stdlib.h>

static const struct { const char *name, *vga, *lvl; } ZONES[3] = {
    { "HEART OF THE FOREST", "FOREST.VGA",  "FOREST.LVL"  },
    { "OASIS DEATH",         "OASIS.VGA",   "OASIS.LVL"   },
    { "INFERNO",             "INFERNO.VGA", "INFERNO.LVL" },
};

static int write_ppm(const char *path, const Frame *f)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    fprintf(fp, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t p = f->px[i];
        uint8_t rgb[3] = { p >> 16, p >> 8, p };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    int zi        = (argc > 2) ? atoi(argv[2]) : 0;
    int ticks     = (argc > 3) ? atoi(argv[3]) : 350;
    const char *out = (argc > 4) ? argv[4] : "frame.ppm";
    if (zi < 0 || zi > 2) zi = 0;
    if (ticks < 1) ticks = 1;

    Zone zone;
    if (!zone_load(dir, ZONES[zi].vga, ZONES[zi].lvl, &zone)) {
        fprintf(stderr, "cannot load zone %d assets from %s\n", zi, dir);
        return 1;
    }
    Palette pal; game_palette_load(&pal);
    Game game;   game_init(&game, &zone, &pal);
    printf("zone %d \"%s\": %d tiles, %d levels\n",
           zi, ZONES[zi].name, zone.ntiles, zone.nlevels);

    Frame *fb = malloc(sizeof(Frame));
    unsigned anim = 0;
    int deaths = 0, clears = 0;
    for (int t = 0; t < ticks; t++) {
        anim++;
        bool jump = (t % 50) < 12;             /* variable-height tap */
        GameEvent e = game_tick(&game, false, true, jump);
        if (e == EV_DIED) {
            deaths++;
            while (!game_death_step(&game)) {}  /* fast-forward the dissolve */
            game_respawn(&game);
        } else if (e == EV_LEVEL_CLEAR) {
            clears++;
            game_start_level(&game, game.level + 1);
        } else if (e == EV_ZONE_CLEAR) {
            clears++;
            printf("tick %4d: ZONE CLEARED\n", t);
            break;
        }
        if (t % 70 == 0)
            printf("tick %4d: level %2d  player (%3d,%3d)px  gems %d/%d\n",
                   t, game.level + 1, game.px / FP, game.py / FP,
                   game.gems_collected, game.gems_needed);
    }
    printf("simulated %d ticks: %d deaths, %d level clears\n",
           ticks, deaths, clears);

    game_palette_animate(&pal, anim);
    game_render(&game, fb);
    if (!write_ppm(out, fb)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", out, SCREEN_W, SCREEN_H);

    free(fb);
    zone_free(&zone);
    return 0;
}

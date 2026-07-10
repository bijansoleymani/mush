/* Magic Mushroom (SDL2 reimplementation) - entry point, window, state machine.
 *
 * Flow, mirroring the original GO.EXE -> MM.EXE handoff:
 *   ZONE SELECT (ZONESLCT.VGA, F1/F2/F3)  ->  TITLE (INTRO.VGA, any key)  ->
 *   PLAY.  Menus render with each PCX's own palette; gameplay uses the shared
 *   reconstructed palette (game_palette_load in render.c).
 *
 * Controls (from the title screen):  ALT = jump,  LEFT SHIFT = left,
 *   RIGHT SHIFT = right,  ESC = quit.
 */
#include "mush.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define SCALE 3

typedef enum { S_ZONESELECT, S_TITLE, S_PLAY, S_DIED, S_MSG } State;
typedef enum { A_NONE, A_NEXTLEVEL, A_ZONESELECT, A_RESPAWN } After;

static const struct { const char *name, *vga, *lvl; } ZONES[3] = {
    { "HEART OF THE FOREST", "FOREST.VGA",  "FOREST.LVL"  },
    { "OASIS DEATH",         "OASIS.VGA",   "OASIS.LVL"   },
    { "INFERNO",             "INFERNO.VGA", "INFERNO.LVL" },
};

/* All loop state lives here so the browser build can drive one frame at a
 * time from emscripten_set_main_loop (the browser owns the loop and a
 * blocking while() would hang the page). */
static struct {
    const char *dir;
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex;
    Pcx intro, zoneslct;
    Palette master;
    Zone zone; bool have_zone;
    Game game;
    Frame *fb;
    State state; After after;
    int timer;
    unsigned anim;                     /* water-shimmer frame counter */
    char msg[64];
    bool running;
    Uint32 prev; double acc, dt;
} A;

static void frame(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) A.running = false;
        if (ev.type == SDL_KEYDOWN) {
            SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_ESCAPE) {
                if (A.state == S_PLAY || A.state == S_DIED || A.state == S_MSG)
                    { A.state = S_ZONESELECT; }        /* back to menu */
#ifndef __EMSCRIPTEN__
                else A.running = false;    /* in a browser there is no "quit" */
#endif
            } else if (A.state == S_ZONESELECT) {
                /* F1-F3 as in the original launcher; 1-3 too, since browsers
                 * and laptop keyboards often reserve the F-keys */
                int z = (k == SDLK_F1 || k == SDLK_1) ? 0 :
                        (k == SDLK_F2 || k == SDLK_2) ? 1 :
                        (k == SDLK_F3 || k == SDLK_3) ? 2 : -1;
                if (z >= 0) {
                    if (A.have_zone) zone_free(&A.zone);
                    if (zone_load(A.dir, ZONES[z].vga, ZONES[z].lvl, &A.zone)) {
                        A.have_zone = true;
                        game_init(&A.game, &A.zone, &A.master);
                        A.state = S_TITLE;
                    }
                }
            } else if (A.state == S_TITLE) {
                A.state = S_PLAY;                       /* any key starts */
            }
        }
    }

    /* fixed ~70 Hz simulation (matches the original's vsync-locked rate) */
    Uint32 now = SDL_GetTicks();
    A.acc += now - A.prev; A.prev = now;
    if (A.acc > 100) A.acc = 100;
    while (A.acc >= A.dt) {
        A.acc -= A.dt;
        if (A.state == S_PLAY || A.state == S_MSG) A.anim++;  /* water freezes on death */
        if (A.state == S_PLAY) {
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            /* original keys (shifts + ALT) plus arrows/space, which behave
             * better in browsers (ALT and SHIFT combos trigger UI shortcuts) */
            bool left  = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_LEFT];
            bool right = ks[SDL_SCANCODE_RSHIFT] || ks[SDL_SCANCODE_RIGHT];
            bool jump  = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT] ||
                         ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_UP];
            GameEvent e = game_tick(&A.game, left, right, jump);
            if (e == EV_DIED) {           /* dissolve the mushroom, then respawn */
                A.state = S_DIED;
            } else if (e == EV_LEVEL_CLEAR) {
                strcpy(A.msg, "LEVEL CLEAR!"); A.after = A_NEXTLEVEL;
                A.state = S_MSG; A.timer = 60;
            } else if (e == EV_ZONE_CLEAR) {
                strcpy(A.msg, "ZONE COMPLETE!"); A.after = A_ZONESELECT;
                A.state = S_MSG; A.timer = 120;
            }
        } else if (A.state == S_DIED) {
            if (game_death_step(&A.game)) { game_respawn(&A.game); A.state = S_PLAY; }
        } else if (A.state == S_MSG) {
            if (--A.timer <= 0) {
                if (A.after == A_NEXTLEVEL) {
                    game_start_level(&A.game, A.game.level + 1); A.state = S_PLAY;
                } else if (A.after == A_ZONESELECT) {
                    A.state = S_ZONESELECT;
                }
            }
        }
    }

    /* draw */
    if (A.state == S_ZONESELECT) {
        fb_blit_pcx(A.fb, &A.zoneslct);
    } else if (A.state == S_TITLE) {
        fb_blit_pcx(A.fb, &A.intro);
        fb_text_center(A.fb, SCREEN_H - 12, "PRESS ANY KEY", 0xFFFFFFFFu);
    } else {
        game_palette_animate(&A.master, A.anim);   /* shimmer the water */
        game_render(&A.game, A.fb);    /* draws the dissolving mushroom while dying */
        if (A.state == S_MSG) {
            fb_rect(A.fb, 0, SCREEN_H/2 - 10, SCREEN_W, 20, 0xD0000000u);
            fb_text_center(A.fb, SCREEN_H/2 - 4, A.msg, 0xFFFFE000u);
        }
    }

    SDL_UpdateTexture(A.tex, NULL, A.fb->px, SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(A.ren);
    SDL_RenderCopy(A.ren, A.tex, NULL, NULL);
    SDL_RenderPresent(A.ren);
}

int main(int argc, char **argv)
{
    A.dir = (argc > 1) ? argv[1] : ".";   /* where the .VGA/.LVL live */
    /* Simulation rate. The original is vsync-locked to VGA mode 13h (~70 Hz),
     * and every speed is per-frame, so the whole game scales with this. Pass a
     * second arg to match a slower reference (e.g. `mush . 60`). */
    int fps = (argc > 2) ? atoi(argv[2]) : TICK_HZ;
    if (fps < 20 || fps > 240) fps = TICK_HZ;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    A.win = SDL_CreateWindow("Magic Mushroom",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * SCALE, SCREEN_H * SCALE, SDL_WINDOW_RESIZABLE);
#ifdef __EMSCRIPTEN__
    /* no PRESENTVSYNC: requestAnimationFrame already paces the loop, and
     * SDL's vsync setup tears down the emscripten main-loop scheduling */
    A.ren = SDL_CreateRenderer(A.win, -1, 0);
#else
    A.ren = SDL_CreateRenderer(A.win, -1, SDL_RENDERER_PRESENTVSYNC);
#endif
    SDL_RenderSetLogicalSize(A.ren, SCREEN_W, SCREEN_H);
    A.tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    /* menu screens + master (gameplay) palette */
    char path[512];
    snprintf(path, sizeof path, "%s/INTRO.VGA", A.dir);
    if (!pcx_load(path, &A.intro)) { fprintf(stderr, "missing %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/ZONESLCT.VGA", A.dir);
    if (!pcx_load(path, &A.zoneslct)) { fprintf(stderr, "missing %s\n", path); return 1; }
    game_palette_load(&A.master);      /* reconstructed gameplay palette */

    A.fb = malloc(sizeof(Frame));
    A.state = S_ZONESELECT;
    A.after = A_NONE;
    A.running = true;
    A.prev = SDL_GetTicks();
    A.acc = 0.0; A.dt = 1000.0 / fps;

#ifdef __EMSCRIPTEN__
    /* 0 fps = one frame per requestAnimationFrame; the accumulator above
     * keeps the simulation at `fps` regardless of the display rate */
    emscripten_set_main_loop(frame, 0, 1);   /* never returns */
#else
    while (A.running) frame();
#endif

    free(A.fb);
    if (A.have_zone) zone_free(&A.zone);
    pcx_free(&A.intro); pcx_free(&A.zoneslct);
    SDL_DestroyTexture(A.tex);
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return 0;
}

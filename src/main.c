/* Magic Mushroom (SDL2 reimplementation) - entry point, window, state machine.
 *
 * Flow, mirroring the original GO.EXE -> MM.EXE handoff:
 *   ZONE SELECT (ZONESLCT.VGA, F1/F2/F3)  ->  TITLE (INTRO.VGA, any key)  ->
 *   PLAY.  Menus render with each PCX's own palette; gameplay uses the shared
 *   reconstructed palette (palette.h).
 *
 * Controls (from the title screen):  ALT = jump,  LEFT SHIFT = left,
 *   RIGHT SHIFT = right,  ESC = quit.
 */
#include "mush.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALE 3

typedef enum { S_ZONESELECT, S_TITLE, S_PLAY, S_DIED, S_MSG } State;
typedef enum { A_NONE, A_NEXTLEVEL, A_ZONESELECT, A_RESPAWN } After;

static const struct { const char *name, *vga, *lvl; } ZONES[3] = {
    { "HEART OF THE FOREST", "FOREST.VGA",  "FOREST.LVL"  },
    { "OASIS DEATH",         "OASIS.VGA",   "OASIS.LVL"   },
    { "INFERNO",             "INFERNO.VGA", "INFERNO.LVL" },
};

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";   /* where the .VGA/.LVL live */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Magic Mushroom",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * SCALE, SCREEN_H * SCALE, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    /* menu screens + master (gameplay) palette */
    Pcx intro, zoneslct;
    char path[512];
    snprintf(path, sizeof path, "%s/INTRO.VGA", dir);
    if (!pcx_load(path, &intro)) { fprintf(stderr, "missing %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/ZONESLCT.VGA", dir);
    if (!pcx_load(path, &zoneslct)) { fprintf(stderr, "missing %s\n", path); return 1; }
    Palette master; game_palette_load(&master);   /* reconstructed gameplay palette */

    Zone zone; bool have_zone = false;
    Game game;
    Frame *fb = malloc(sizeof(Frame));

    State state = S_ZONESELECT;
    After after = A_NONE;
    int  timer = 0;
    char msg[64] = {0};

    bool running = true;
    Uint32 prev = SDL_GetTicks();
    double acc = 0.0; const double DT = 1000.0 / 60.0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    if (state == S_PLAY || state == S_DIED || state == S_MSG)
                        { state = S_ZONESELECT; }      /* back to menu */
                    else running = false;
                } else if (state == S_ZONESELECT) {
                    int z = (k == SDLK_F1) ? 0 : (k == SDLK_F2) ? 1 :
                            (k == SDLK_F3) ? 2 : -1;
                    if (z >= 0) {
                        if (have_zone) zone_free(&zone);
                        if (zone_load(dir, ZONES[z].vga, ZONES[z].lvl, &zone)) {
                            have_zone = true;
                            game_init(&game, &zone, &master);
                            state = S_TITLE;
                        }
                    }
                } else if (state == S_TITLE) {
                    state = S_PLAY;                     /* any key starts */
                }
            }
        }

        /* fixed 60 Hz simulation */
        Uint32 now = SDL_GetTicks();
        acc += now - prev; prev = now;
        if (acc > 100) acc = 100;
        while (acc >= DT) {
            acc -= DT;
            if (state == S_PLAY) {
                const Uint8 *ks = SDL_GetKeyboardState(NULL);
                bool left  = ks[SDL_SCANCODE_LSHIFT];
                bool right = ks[SDL_SCANCODE_RSHIFT];
                bool jump  = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];
                GameEvent e = game_tick(&game, left, right, jump);
                if (e == EV_DIED) {
                    game.lives--;
                    if (game.lives <= 0) {
                        strcpy(msg, "GAME OVER"); after = A_ZONESELECT;
                        state = S_MSG; timer = 90;
                    } else { state = S_DIED; timer = 45; }
                } else if (e == EV_LEVEL_CLEAR) {
                    strcpy(msg, "LEVEL CLEAR!"); after = A_NEXTLEVEL;
                    state = S_MSG; timer = 60;
                } else if (e == EV_ZONE_CLEAR) {
                    strcpy(msg, "ZONE COMPLETE!"); after = A_ZONESELECT;
                    state = S_MSG; timer = 120;
                }
            } else if (state == S_DIED || state == S_MSG) {
                if (--timer <= 0) {
                    if (state == S_DIED) { game_respawn(&game); state = S_PLAY; }
                    else if (after == A_NEXTLEVEL) {
                        game_start_level(&game, game.level + 1); state = S_PLAY;
                    } else if (after == A_ZONESELECT) {
                        state = S_ZONESELECT;
                    }
                }
            }
        }

        /* draw */
        if (state == S_ZONESELECT) {
            fb_blit_pcx(fb, &zoneslct);
        } else if (state == S_TITLE) {
            fb_blit_pcx(fb, &intro);
            fb_text_center(fb, SCREEN_H - 12, "PRESS ANY KEY", 0xFFFFFFFFu);
        } else {
            game_render(&game, fb);
            if (state == S_DIED) {
                for (int i = 0; i < SCREEN_W*SCREEN_H; i++)     /* red flash */
                    fb->px[i] = 0xFF000000u | ((fb->px[i] & 0xFF0000) | 0x300000);
                fb_text_center(fb, SCREEN_H/2 - 4, "OUCH!", 0xFFFF4040u);
            } else if (state == S_MSG) {
                fb_rect(fb, 0, SCREEN_H/2 - 10, SCREEN_W, 20, 0xD0000000u);
                fb_text_center(fb, SCREEN_H/2 - 4, msg, 0xFFFFE000u);
            }
        }

        SDL_UpdateTexture(tex, NULL, fb->px, SCREEN_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    free(fb);
    if (have_zone) zone_free(&zone);
    pcx_free(&intro); pcx_free(&zoneslct);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

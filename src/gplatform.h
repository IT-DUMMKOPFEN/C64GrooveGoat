//
// C64 GrooveGoat - SDL2 platform layer (window, input, frame timing)
// Replaces the SDL 1.2 based BME window/keyboard/mouse modules of GoatTracker.
//

#ifndef GPLATFORM_H
#define GPLATFORM_H

#include <SDL.h>
#include "gkeys.h"

#define MAX_KEYS 512

#define MOUSEB_LEFT 1
#define MOUSEB_RIGHT 2
#define MOUSEB_MIDDLE 4

int plat_init(const char *title, int fbwidth, int fbheight);
void plat_shutdown(void);
void win_checkmessages(void);
int win_getspeed(int framerate);
void plat_togglefullscreen(void);
void plat_settitle(const char *title);
void plat_seticon(SDL_Surface *icon);
void plat_mapmouse(int wx, int wy, int *fx, int *fy);
int plat_getscale(void);

extern SDL_Window *plat_window;
extern SDL_Renderer *plat_renderer;
extern int plat_dstx, plat_dsty, plat_dstw, plat_dsth;
extern int plat_fbw, plat_fbh;
extern int win_quitted;
extern int win_fullscreen;
extern int win_redraw;
extern int win_resized;
extern unsigned char win_keytable[MAX_KEYS];
extern unsigned char win_keystate[MAX_KEYS];
extern int win_asciikey;
extern int win_symkey;
extern unsigned win_mousebuttons;
extern int win_mousewheel;
extern int win_mousex;
extern int win_mousey;
extern int win_textpending;
extern char win_dropfile[1024];

#endif

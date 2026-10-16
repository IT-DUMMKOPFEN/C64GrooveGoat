//
// C64 GrooveGoat - SDL2 platform layer (window, input, frame timing)
//
// This replaces the SDL 1.2 "BME" window, keyboard and mouse modules that
// GoatTracker 2 used. Key codes are reported with the old SDL 1.2 numbering
// (see gkeys.h) so the editor logic did not need to change.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gplatform.h"

SDL_Window *plat_window = NULL;
SDL_Renderer *plat_renderer = NULL;

int win_quitted = 0;
int win_fullscreen = 0;
int win_redraw = 1;
int win_resized = 0;
unsigned char win_keytable[MAX_KEYS];
unsigned char win_keystate[MAX_KEYS];
int win_asciikey = 0;
int win_symkey = 0;
unsigned win_mousebuttons = 0;
int win_mousewheel = 0;
int win_mousex = 0;
int win_mousey = 0;
int win_textpending = 0;
char win_dropfile[1024] = {0};

// Destination rectangle of the framebuffer inside the window (output pixels),
// maintained by the renderer in gconsole.c
int plat_dstx = 0, plat_dsty = 0, plat_dstw = 1, plat_dsth = 1;
int plat_fbw = 1, plat_fbh = 1;
static int plat_basew = 1, plat_baseh = 1;

static Uint32 lasttime = 0;
static int framecounter = 0;

static int mapscancode(SDL_Scancode sc)
{
  if ((sc >= SDL_SCANCODE_A) && (sc <= SDL_SCANCODE_Z)) return 'a' + (sc - SDL_SCANCODE_A);
  if ((sc >= SDL_SCANCODE_1) && (sc <= SDL_SCANCODE_9)) return '1' + (sc - SDL_SCANCODE_1);
  if ((sc >= SDL_SCANCODE_F1) && (sc <= SDL_SCANCODE_F12)) return KEY_F1 + (sc - SDL_SCANCODE_F1);
  if ((sc >= SDL_SCANCODE_KP_1) && (sc <= SDL_SCANCODE_KP_9)) return KEY_KP1 + (sc - SDL_SCANCODE_KP_1);

  switch (sc)
  {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN: return KEY_ENTER;
    case SDL_SCANCODE_RETURN2: return KEY_ENTER;
    case SDL_SCANCODE_ESCAPE: return KEY_ESC;
    case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
    case SDL_SCANCODE_TAB: return KEY_TAB;
    case SDL_SCANCODE_SPACE: return KEY_SPACE;
    case SDL_SCANCODE_MINUS: return KEY_MINUS;
    case SDL_SCANCODE_EQUALS: return KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET: return KEY_BRACKETL;
    case SDL_SCANCODE_RIGHTBRACKET: return KEY_BRACKETR;
    case SDL_SCANCODE_BACKSLASH: return KEY_BACKSLASH;
    case SDL_SCANCODE_NONUSHASH: return KEY_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON: return KEY_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE: return KEY_APOST1;
    case SDL_SCANCODE_GRAVE: return KEY_APOST2;
    case SDL_SCANCODE_COMMA: return KEY_COMMA;
    case SDL_SCANCODE_PERIOD: return KEY_PERIOD;
    case SDL_SCANCODE_SLASH: return KEY_SLASH;
    case SDL_SCANCODE_CAPSLOCK: return KEY_CAPSLOCK;
    case SDL_SCANCODE_SCROLLLOCK: return KEY_SCROLLLOCK;
    case SDL_SCANCODE_PAUSE: return KEY_PAUSE;
    case SDL_SCANCODE_INSERT: return KEY_INS;
    case SDL_SCANCODE_HOME: return KEY_HOME;
    case SDL_SCANCODE_PAGEUP: return KEY_PGUP;
    case SDL_SCANCODE_DELETE: return KEY_DEL;
    case SDL_SCANCODE_END: return KEY_END;
    case SDL_SCANCODE_PAGEDOWN: return KEY_PGDN;
    case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
    case SDL_SCANCODE_LEFT: return KEY_LEFT;
    case SDL_SCANCODE_DOWN: return KEY_DOWN;
    case SDL_SCANCODE_UP: return KEY_UP;
    case SDL_SCANCODE_NUMLOCKCLEAR: return KEY_NUMLOCK;
    case SDL_SCANCODE_KP_DIVIDE: return KEY_KPDIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return KEY_KPMULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return KEY_KPMINUS;
    case SDL_SCANCODE_KP_PLUS: return KEY_KPPLUS;
    case SDL_SCANCODE_KP_ENTER: return KEY_KPENTER;
    case SDL_SCANCODE_KP_0: return KEY_KP0;
    case SDL_SCANCODE_KP_PERIOD: return KEY_KPPERIOD;
    case SDL_SCANCODE_KP_EQUALS: return KEY_KPEQUALS;
    case SDL_SCANCODE_NONUSBACKSLASH: return KEY_ISO;
    case SDL_SCANCODE_APPLICATION: return KEY_MENU;
    case SDL_SCANCODE_LCTRL: return KEY_CTRL;
    case SDL_SCANCODE_RCTRL: return KEY_RIGHTCTRL;
    case SDL_SCANCODE_LSHIFT: return KEY_LEFTSHIFT;
    case SDL_SCANCODE_RSHIFT: return KEY_RIGHTSHIFT;
    case SDL_SCANCODE_LALT: return KEY_ALT;
    case SDL_SCANCODE_RALT: return KEY_RIGHTALT;
    case SDL_SCANCODE_LGUI: return KEY_LEFTMETA;
    case SDL_SCANCODE_RGUI: return KEY_RIGHTMETA;
    default: return 0;
  }
}

// Map layout-dependent keycodes of letters (used for command shortcuts like
// CTRL/CMD+Z) to the same numbering
static int mapkeysym(SDL_Keycode sym)
{
  if ((sym >= 'a') && (sym <= 'z')) return sym;
  if ((sym >= '0') && (sym <= '9')) return sym;
  return 0;
}

int plat_init(const char *title, int fbwidth, int fbheight)
{
  SDL_Rect usable;
  int ww = fbwidth;
  int wh = fbheight;

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#ifdef SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK
  SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
  {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 0;
  }
  atexit(SDL_Quit);

  plat_fbw = fbwidth;
  plat_fbh = fbheight;
  plat_basew = fbwidth;
  plat_baseh = fbheight;

  // Fit the initial window into the usable desktop area
  if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
  {
    int maxw = usable.w * 92 / 100;
    int maxh = usable.h * 92 / 100;
    if ((ww > maxw) || (wh > maxh))
    {
      double sx = (double)maxw / ww;
      double sy = (double)maxh / wh;
      double s = sx < sy ? sx : sy;
      ww = (int)(ww * s);
      wh = (int)(wh * s);
    }
  }

  plat_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (win_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
  if (!plat_window)
  {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 0;
  }
  SDL_SetWindowMinimumSize(plat_window, fbwidth / 3, fbheight / 3);

  plat_renderer = SDL_CreateRenderer(plat_window, -1, SDL_RENDERER_ACCELERATED);
  if (!plat_renderer)
    plat_renderer = SDL_CreateRenderer(plat_window, -1, SDL_RENDERER_SOFTWARE);
  if (!plat_renderer)
  {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return 0;
  }

  SDL_StartTextInput();
  SDL_ShowCursor(SDL_ENABLE);
  memset(win_keytable, 0, sizeof win_keytable);
  memset(win_keystate, 0, sizeof win_keystate);
  lasttime = SDL_GetTicks();
  return 1;
}

void plat_shutdown(void)
{
  if (plat_renderer) SDL_DestroyRenderer(plat_renderer);
  if (plat_window) SDL_DestroyWindow(plat_window);
  plat_renderer = NULL;
  plat_window = NULL;
}

void plat_settitle(const char *title)
{
  if (plat_window) SDL_SetWindowTitle(plat_window, title);
}

void plat_seticon(SDL_Surface *icon)
{
  if ((plat_window) && (icon)) SDL_SetWindowIcon(plat_window, icon);
}

void plat_togglefullscreen(void)
{
  if (!plat_window) return;
  win_fullscreen ^= 1;
  SDL_SetWindowFullscreen(plat_window, win_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  win_redraw = 1;
  win_resized = 1;
}

// Window coordinates (points) -> framebuffer pixel coordinates
void plat_mapmouse(int wx, int wy, int *fx, int *fy)
{
  int winw = 1, winh = 1, outw = 1, outh = 1;
  double px, py;

  if ((!plat_window) || (!plat_renderer))
  {
    *fx = wx;
    *fy = wy;
    return;
  }
  SDL_GetWindowSize(plat_window, &winw, &winh);
  SDL_GetRendererOutputSize(plat_renderer, &outw, &outh);
  if (winw < 1) winw = 1;
  if (winh < 1) winh = 1;
  px = (double)wx * outw / winw;
  py = (double)wy * outh / winh;
  px = (px - plat_dstx) * plat_fbw / (plat_dstw > 0 ? plat_dstw : 1);
  py = (py - plat_dsty) * plat_fbh / (plat_dsth > 0 ? plat_dsth : 1);
  if (px < 0) px = 0;
  if (py < 0) py = 0;
  if (px > plat_fbw - 1) px = plat_fbw - 1;
  if (py > plat_fbh - 1) py = plat_fbh - 1;
  *fx = (int)px;
  *fy = (int)py;
}

// Suggested rendering scale for crisp text (1 = normal, 2 = HiDPI)
int plat_getscale(void)
{
  int outw = 0, outh = 0;
  if (getenv("GG_SCALE")) return atoi(getenv("GG_SCALE")) >= 2 ? 2 : 1;
  if (!plat_renderer) return 1;
  SDL_GetRendererOutputSize(plat_renderer, &outw, &outh);
  // Use the sharp double size font when the output has at least 1.5 times
  // the pixels of the normal size screen (Retina / HiDPI or big windows)
  if ((outw >= plat_basew * 3 / 2) && (outh >= plat_baseh * 3 / 2)) return 2;
  return 1;
}

void win_checkmessages(void)
{
  SDL_Event event;

  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
      case SDL_QUIT:
      win_quitted = 1;
      break;

      case SDL_WINDOWEVENT:
      switch (event.window.event)
      {
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        win_resized = 1;
        win_redraw = 1;
        break;

        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_MAXIMIZED:
        win_redraw = 1;
        break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
        // Forget held modifiers, otherwise e.g. ALT+TAB leaves ALT stuck
        memset(win_keystate, 0, sizeof win_keystate);
        win_mousebuttons = 0;
        break;
      }
      break;

      case SDL_MOUSEMOTION:
      plat_mapmouse(event.motion.x, event.motion.y, &win_mousex, &win_mousey);
      break;

      case SDL_MOUSEBUTTONDOWN:
      plat_mapmouse(event.button.x, event.button.y, &win_mousex, &win_mousey);
      switch (event.button.button)
      {
        case SDL_BUTTON_LEFT:
        // Mac users without a right button: CTRL+click acts as right click
        if (win_keystate[KEY_CTRL] || win_keystate[KEY_RIGHTCTRL])
          win_mousebuttons |= MOUSEB_RIGHT;
        else
          win_mousebuttons |= MOUSEB_LEFT;
        break;
        case SDL_BUTTON_MIDDLE: win_mousebuttons |= MOUSEB_MIDDLE; break;
        case SDL_BUTTON_RIGHT: win_mousebuttons |= MOUSEB_RIGHT; break;
      }
      break;

      case SDL_MOUSEBUTTONUP:
      plat_mapmouse(event.button.x, event.button.y, &win_mousex, &win_mousey);
      switch (event.button.button)
      {
        case SDL_BUTTON_LEFT: win_mousebuttons &= ~(MOUSEB_LEFT|MOUSEB_RIGHT); break;
        case SDL_BUTTON_MIDDLE: win_mousebuttons &= ~MOUSEB_MIDDLE; break;
        case SDL_BUTTON_RIGHT: win_mousebuttons &= ~MOUSEB_RIGHT; break;
      }
      break;

      case SDL_MOUSEWHEEL:
      {
        int dy = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
        win_mousewheel += dy;
      }
      break;

      case SDL_KEYDOWN:
      {
        int code = mapscancode(event.key.keysym.scancode);
        int sym = mapkeysym(event.key.keysym.sym);
        if ((code > 0) && (code < MAX_KEYS))
        {
          win_keytable[code] = 1;
          win_keystate[code] = 1;
        }
        win_symkey = sym;

        // Control characters are not delivered as text input
        switch (code)
        {
          case KEY_ENTER:
          case KEY_KPENTER:
          win_asciikey = 13;
          break;
          case KEY_BACKSPACE:
          win_asciikey = 8;
          break;
          case KEY_TAB:
          win_asciikey = 9;
          break;
          case KEY_ESC:
          win_asciikey = 27;
          break;
          case KEY_DEL:
          win_asciikey = 127;
          break;
        }

        // ALT+ENTER toggles fullscreen
        if ((code == KEY_ENTER) && (event.key.keysym.mod & KMOD_ALT))
        {
          win_keytable[code] = 0;
          win_asciikey = 0;
          plat_togglefullscreen();
        }
      }
      break;

      case SDL_KEYUP:
      {
        int code = mapscancode(event.key.keysym.scancode);
        if ((code > 0) && (code < MAX_KEYS))
          win_keystate[code] = 0;
      }
      break;

      case SDL_TEXTINPUT:
      {
        unsigned char c = (unsigned char)event.text.text[0];
        // Accept plain ASCII and Latin-1 letters encoded as two byte UTF-8
        if ((c >= 32) && (c < 127)) win_asciikey = c;
        else if ((c == 0xc2) || (c == 0xc3))
        {
          unsigned char c2 = (unsigned char)event.text.text[1];
          win_asciikey = ((c & 0x03) << 6) | (c2 & 0x3f);
        }
      }
      break;

      case SDL_DROPFILE:
      if (event.drop.file)
      {
        strncpy(win_dropfile, event.drop.file, sizeof win_dropfile - 1);
        win_dropfile[sizeof win_dropfile - 1] = 0;
        SDL_free(event.drop.file);
      }
      break;
    }
  }
}

// Wait until at least one frame (at the given framerate) has passed,
// processing window messages meanwhile. Returns number of frames passed.
int win_getspeed(int framerate)
{
  int frametime = 10000 / framerate;
  int frames = 0;

  while (!frames)
  {
    Uint32 now;
    win_checkmessages();
    now = SDL_GetTicks();
    framecounter += (int)(now - lasttime) * 10;
    lasttime = now;
    frames = framecounter / frametime;
    framecounter -= frames * frametime;
    if (frames > 10)
    {
      frames = 10;
      framecounter = 0;
    }
    if (!frames) SDL_Delay((frametime - framecounter) / 10 + 1);
  }
  return frames;
}

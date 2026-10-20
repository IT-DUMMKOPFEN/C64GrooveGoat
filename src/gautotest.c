//
// C64 GrooveGoat - scripted UI test driver
//
// GG_SCRIPT contains space separated tokens that are turned into SDL input
// events, one token every few frames, while the normal program runs:
//
//   a-z 0-9 F1-F12 SPACE RETURN ESC TAB UP DOWN LEFT RIGHT BKSP DEL INS
//   HOME END PGUP PGDN KPDIV KPMUL KPPLUS KPMINUS GRAVE MINUS PLUS COMMA
//   PERIOD SLASH        keys, prefix S+ (shift), C+ (ctrl) or A+ (alt)
//   click:X,Y           left click on text cell X,Y (rclick: right click)
//   move:X,Y            move the mouse pointer to text cell X,Y
//   dclick:X,Y          double click
//   wheel:X,Y,N         mouse wheel N steps over cell X,Y
//   wait:N              wait N frames
//   type:TEXT           type text (underscore = space)
//   shot:FILE.bmp       screenshot of the current screen
//   quit                exit immediately
//

#include "goattrk2.h"

static char *script = NULL;
static char *scriptpos = NULL;
static int waitframes = 0;
static int pendingup = 0;
static SDL_Scancode upcode;
static int upmods;
static const char *typing = NULL;

static const struct
{
  const char *name;
  SDL_Scancode code;
  char text;
} keynames[] =
{
  {"SPACE", SDL_SCANCODE_SPACE, ' '},
  {"RETURN", SDL_SCANCODE_RETURN, 0},
  {"ESC", SDL_SCANCODE_ESCAPE, 0},
  {"TAB", SDL_SCANCODE_TAB, 0},
  {"UP", SDL_SCANCODE_UP, 0},
  {"DOWN", SDL_SCANCODE_DOWN, 0},
  {"LEFT", SDL_SCANCODE_LEFT, 0},
  {"RIGHT", SDL_SCANCODE_RIGHT, 0},
  {"BKSP", SDL_SCANCODE_BACKSPACE, 0},
  {"DEL", SDL_SCANCODE_DELETE, 0},
  {"INS", SDL_SCANCODE_INSERT, 0},
  {"HOME", SDL_SCANCODE_HOME, 0},
  {"END", SDL_SCANCODE_END, 0},
  {"PGUP", SDL_SCANCODE_PAGEUP, 0},
  {"PGDN", SDL_SCANCODE_PAGEDOWN, 0},
  {"KPDIV", SDL_SCANCODE_KP_DIVIDE, '/'},
  {"KPMUL", SDL_SCANCODE_KP_MULTIPLY, '*'},
  {"KPPLUS", SDL_SCANCODE_KP_PLUS, '+'},
  {"KPMINUS", SDL_SCANCODE_KP_MINUS, '-'},
  {"GRAVE", SDL_SCANCODE_GRAVE, '`'},
  {"MINUS", SDL_SCANCODE_MINUS, '-'},
  {"PLUS", SDL_SCANCODE_EQUALS, '+'},
  {"COMMA", SDL_SCANCODE_COMMA, ','},
  {"PERIOD", SDL_SCANCODE_PERIOD, '.'},
  {"SLASH", SDL_SCANCODE_SLASH, '/'},
  {"CAPSLOCK", SDL_SCANCODE_CAPSLOCK, 0},
  {NULL, SDL_SCANCODE_UNKNOWN, 0}
};

static void pushkey(SDL_Scancode code, int down, int mods, char text)
{
  SDL_Event ev;
  memset(&ev, 0, sizeof ev);
  ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  ev.key.keysym.scancode = code;
  ev.key.keysym.sym = SDL_GetKeyFromScancode(code);
  ev.key.keysym.mod = mods;
  SDL_PushEvent(&ev);
  if ((down) && (text) && (!(mods & (KMOD_CTRL | KMOD_GUI))))
  {
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_TEXTINPUT;
    ev.text.text[0] = text;
    ev.text.text[1] = 0;
    SDL_PushEvent(&ev);
  }
}

static void pushmods(int mods, int down)
{
  if (mods & KMOD_LSHIFT) pushkey(SDL_SCANCODE_LSHIFT, down, mods, 0);
  if (mods & KMOD_LCTRL) pushkey(SDL_SCANCODE_LCTRL, down, mods, 0);
  if (mods & KMOD_LALT) pushkey(SDL_SCANCODE_LALT, down, mods, 0);
}

// Window coordinates of the centre of a text cell
static void cellpos(int cx, int cy, int *wx, int *wy)
{
  int winw = 1, winh = 1, outw = 1, outh = 1;
  double fx = (cx * CELLW + CELLW / 2) * (double)plat_fbw / (MAX_COLUMNS * CELLW);
  double fy = (cy * CELLH + CELLH / 2) * (double)plat_fbh / (MAX_ROWS * CELLH);
  double ox = plat_dstx + fx * plat_dstw / plat_fbw;
  double oy = plat_dsty + fy * plat_dsth / plat_fbh;
  SDL_GetWindowSize(plat_window, &winw, &winh);
  SDL_GetRendererOutputSize(plat_renderer, &outw, &outh);
  *wx = (int)(ox * winw / outw);
  *wy = (int)(oy * winh / outh);
}

static void pushmouse(int cx, int cy, int button, int down)
{
  SDL_Event ev;
  int wx, wy;
  cellpos(cx, cy, &wx, &wy);
  memset(&ev, 0, sizeof ev);
  ev.type = SDL_MOUSEMOTION;
  ev.motion.x = wx;
  ev.motion.y = wy;
  SDL_PushEvent(&ev);
  memset(&ev, 0, sizeof ev);
  ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  ev.button.button = button;
  ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  ev.button.clicks = 1;
  ev.button.x = wx;
  ev.button.y = wy;
  SDL_PushEvent(&ev);
}

static int dotoken(const char *tok)
{
  int mods = 0;
  int i;

  if (!strncmp(tok, "wait:", 5))
  {
    waitframes = atoi(tok + 5);
    return 1;
  }
  if (!strncmp(tok, "shot:", 5))
  {
    fliptoscreen();
    savescreenshot(tok + 5);
    return 1;
  }
  if (!strcmp(tok, "quit"))
  {
    exit(0);
  }
  if (!strncmp(tok, "type:", 5))
  {
    static char typebuf[512];
    strncpy(typebuf, tok + 5, sizeof typebuf - 1);
    typing = typebuf[0] ? typebuf : NULL;
    return 1;
  }
  if ((!strncmp(tok, "click:", 6)) || (!strncmp(tok, "rclick:", 7)) || (!strncmp(tok, "dclick:", 7)))
  {
    int x = 0, y = 0;
    int button = (tok[0] == 'r') ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    sscanf(strchr(tok, ':') + 1, "%d,%d", &x, &y);
    pushmouse(x, y, button, 1);
    pendingup = 2 + button;  // release on the next tick
    upcode = (SDL_Scancode)((x << 8) | y);
    if (tok[0] == 'd')
    {
      pushmouse(x, y, button, 0);
      pushmouse(x, y, button, 1);
    }
    return 1;
  }
  if (!strncmp(tok, "move:", 5))
  {
    SDL_Event ev;
    int x = 0, y = 0, wx, wy;
    sscanf(tok + 5, "%d,%d", &x, &y);
    cellpos(x, y, &wx, &wy);
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = wx;
    ev.motion.y = wy;
    SDL_PushEvent(&ev);
    return 1;
  }
  if (!strncmp(tok, "wheel:", 6))
  {
    int x = 0, y = 0, n = 0;
    SDL_Event ev;
    sscanf(tok + 6, "%d,%d,%d", &x, &y, &n);
    int wx, wy;
    cellpos(x, y, &wx, &wy);
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = wx;
    ev.motion.y = wy;
    SDL_PushEvent(&ev);
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_MOUSEWHEEL;
    ev.wheel.y = n;
    SDL_PushEvent(&ev);
    return 1;
  }

  while ((tok[0]) && (tok[1] == '+') && (tok[2]))
  {
    if (tok[0] == 'S') mods |= KMOD_LSHIFT;
    if (tok[0] == 'C') mods |= KMOD_LCTRL;
    if (tok[0] == 'A') mods |= KMOD_LALT;
    tok += 2;
  }
  pushmods(mods, 1);
  if ((tok[0] == 'F') && (tok[1] >= '1') && (tok[1] <= '9'))
  {
    int n = atoi(tok + 1);
    SDL_Scancode code = (n <= 12) ? (SDL_Scancode)(SDL_SCANCODE_F1 + n - 1) : SDL_SCANCODE_F1;
    pushkey(code, 1, mods, 0);
    upcode = code;
  }
  else if (strlen(tok) == 1)
  {
    char c = tok[0];
    SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
    char text = c;
    if ((c >= 'a') && (c <= 'z')) code = (SDL_Scancode)(SDL_SCANCODE_A + c - 'a');
    if ((c >= '1') && (c <= '9')) code = (SDL_Scancode)(SDL_SCANCODE_1 + c - '1');
    if (c == '0') code = SDL_SCANCODE_0;
    if ((mods & KMOD_SHIFT) && (c >= 'a') && (c <= 'z')) text = c - 32;
    pushkey(code, 1, mods, text);
    upcode = code;
  }
  else
  {
    for (i = 0; keynames[i].name; i++)
    {
      if (!strcmp(tok, keynames[i].name))
      {
        pushkey(keynames[i].code, 1, mods, keynames[i].text);
        upcode = keynames[i].code;
        break;
      }
    }
    if (!keynames[i].name)
    {
      fprintf(stderr, "GG_SCRIPT: unknown token %s\n", tok);
      upcode = SDL_SCANCODE_UNKNOWN;
    }
  }
  upmods = mods;
  pendingup = 1;
  return 1;
}

void autoscript_tick(void)
{
  char tok[512];
  int l = 0;

  if (!script)
  {
    const char *s = getenv("GG_SCRIPT");
    if (!s) return;
    script = strdup(s);
    scriptpos = script;
    waitframes = 20;
  }

  // Release keys/buttons pressed on the previous tick
  if (pendingup == 1)
  {
    if (upcode != SDL_SCANCODE_UNKNOWN) pushkey(upcode, 0, upmods, 0);
    pushmods(upmods, 0);
    pendingup = 0;
    waitframes = 3;
    return;
  }
  if (pendingup >= 2)
  {
    pushmouse(upcode >> 8, upcode & 0xff, pendingup - 2, 0);
    pendingup = 0;
    waitframes = 3;
    return;
  }
  if (waitframes > 0)
  {
    waitframes--;
    return;
  }
  if (typing)
  {
    char c = *typing++;
    char t[2] = {c == '_' ? ' ' : c, 0};
    if (!*typing) typing = NULL;
    if ((c >= 'a') && (c <= 'z')) pushkey((SDL_Scancode)(SDL_SCANCODE_A + c - 'a'), 1, 0, c);
    else if ((c >= 'A') && (c <= 'Z')) pushkey((SDL_Scancode)(SDL_SCANCODE_A + c - 'A'), 1, KMOD_LSHIFT, c);
    else if ((c >= '0') && (c <= '9')) pushkey(c == '0' ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + c - '1'), 1, 0, c);
    else pushkey(SDL_SCANCODE_SPACE, 1, 0, t[0]);
    upcode = SDL_SCANCODE_UNKNOWN;
    upmods = 0;
    pendingup = 1;
    return;
  }

  while (*scriptpos == ' ') scriptpos++;
  if (!*scriptpos)
  {
    // End of script: keep running until the screenshot (if any) is done
    return;
  }
  while ((*scriptpos) && (*scriptpos != ' ') && (l < 511)) tok[l++] = *scriptpos++;
  tok[l] = 0;
  dotoken(tok);
}

//
// C64 GrooveGoat - simple modal dialogs drawn in the character screen
//

#define GDIALOG_C

#include "goattrk2.h"
#include "gres.h"
#include <math.h>

// Text wrapped at word boundaries into lines of "width" columns; returns the
// number of lines printed (at most maxlines)
int dlg_printwrapped(int x, int y, int width, int maxlines, int color, const char *text)
{
  int n = 0;
  char line[MAX_COLUMNS + 1];
  while ((*text) && (n < maxlines))
  {
    int len = strlen(text), cut = len;
    if (width > MAX_COLUMNS) width = MAX_COLUMNS;
    if (len > width)
    {
      cut = width;
      while ((cut > width / 2) && (text[cut] != ' ')) cut--;
      if (text[cut] != ' ') cut = width;
    }
    snprintf(line, sizeof line, "%.*s", cut, text);
    printtext(x + (n ? 2 : 0), y + n, color, line);
    n++;
    text += cut;
    while (*text == ' ') text++;
    if (n) width -= 0;
  }
  return n;
}

void dlg_drawframe(int x, int y, int w, int h, const char *title)
{
  int c;
  snprintf(tip_context, sizeof tip_context, "%s", title ? title : "");
  tip_newframe();
  clearclip();
  setdimrect(x, y, w, h);
  // a card with rounded corners on the dimmed screen
  fillarea(x, y, w, h, MKCOL(8, C_PANEL), ' ');
  printblankc(x, y, MKCOL(C_HEADERTEXT, C_HEADER), w);
  if (title)
  {
    printglyph(x + 2, y, MKCOL(2, C_HEADER), GL_BULLET);
    printtext(x + 4, y, MKCOL(15, C_HEADER), title);
  }
  // text inside the window stays inside the frame
  setclip(x + 1, y + 1, x + w - 2, y + h - 2);
}

// Generic dialog: text lines and a row of buttons. Returns chosen button
// index, or -1 for cancel (ESC / close window).
int dlg_choose(const char *title, const char **lines, int numlines, const char **options, int numoptions, int def)
{
  int w = 30;
  int h;
  int x, y, c;
  int sel = def;
  int btnw = 0;
  int result = -2;

  // long lines are wrapped at word boundaries
  static char wrapped[64][MAX_COLUMNS];
  static const char *wl[64];
  int nw = 0, maxw = MAX_COLUMNS - 12;
  for (c = 0; (c < numlines) && (nw < 64); c++)
  {
    const char *t = lines[c];
    do
    {
      int len = strlen(t), cut = len;
      if (len > maxw)
      {
        cut = maxw;
        while ((cut > maxw / 2) && (t[cut] != ' ')) cut--;
        if (t[cut] != ' ') cut = maxw;
      }
      snprintf(wrapped[nw], MAX_COLUMNS, "%.*s", cut, t);
      wl[nw] = wrapped[nw];
      nw++;
      t += cut;
      while (*t == ' ') t++;
    } while ((*t) && (nw < 64));
  }
  lines = wl;
  numlines = nw;
  if (title) w = strlen(title) + 14;
  for (c = 0; c < numlines; c++)
    if ((int)strlen(lines[c]) + 6 > w) w = strlen(lines[c]) + 6;
  for (c = 0; c < numoptions; c++)
    btnw += strlen(options[c]) + 4 + 2;
  if (btnw + 4 > w) w = btnw + 4;
  if (w > MAX_COLUMNS - 4) w = MAX_COLUMNS - 4;
  h = numlines + 6;
  x = (MAX_COLUMNS - w) / 2;
  y = (MAX_ROWS - h) / 2 - 2;

  key = 0;
  rawkey = 0;
  while (result == -2)
  {
    int bx;
    int hit = -1;

    printstatus();
    dlg_drawframe(x, y, w, h, title);
    for (c = 0; c < numlines; c++)
      printtext(x + 3, y + 2 + c, MKCOL(c == 0 ? 15 : 7, C_PANEL), lines[c]);

    bx = x + (w - btnw) / 2;
    for (c = 0; c < numoptions; c++)
    {
      char buf[80];
      int bw;
      sprintf(buf, "  %s  ", options[c]);
      bw = strlen(buf);
      printtext(bx, y + h - 3, MKCOL(C_BUTTONTEXT, c == sel ? C_BUTTONHI : C_BUTTON), buf);
      if ((mousey == y + h - 3) && (mousex >= bx) && (mousex < bx + bw)) hit = c;
      bx += bw + 2;
    }
    fliptoscreen();
    getkey();

    if (win_quitted)
    {
      result = -1;
      break;
    }
    if ((hit >= 0) && (mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      result = hit;
      break;
    }
    switch (rawkey)
    {
      case KEY_LEFT:
      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_RIGHT:
      case KEY_DOWN:
      case KEY_TAB:
      if (sel < numoptions - 1) sel++;
      else if (rawkey == KEY_TAB) sel = 0;
      break;

      case KEY_ENTER:
      case KEY_SPACE:
      result = sel;
      break;

      case KEY_ESC:
      result = -1;
      break;
    }
    // Hotkeys: first letter of an option
    if ((result == -2) && (key))
    {
      for (c = 0; c < numoptions; c++)
      {
        if (tolower((unsigned char)options[c][0]) == tolower(key))
        {
          result = c;
          break;
        }
      }
    }
  }
  key = 0;
  rawkey = 0;
  printmainscreen();
  return result;
}

int dlg_confirm(const char *title, const char *text)
{
  const char *lines[1];
  const char *opts[2] = {"Yes", "No"};
  lines[0] = text;
  return dlg_choose(title, lines, 1, opts, 2, 0) == 0;
}

void dlg_message(const char *title, const char **lines, int numlines)
{
  const char *opts[1] = {"OK"};
  dlg_choose(title, lines, numlines, opts, 1, 0);
}

// Edit a hexadecimal value. Returns 1 if accepted.
int dlg_edithex(const char *title, const char *label, unsigned *value, int digits)
{
  unsigned v = *value;
  int col = 0;
  int w = strlen(label) + digits + 12;
  int h = 7;
  int x, y;
  int result = -2;

  if ((title) && ((int)strlen(title) + 14 > w)) w = strlen(title) + 14;
  if (w < 34) w = 34;
  x = (MAX_COLUMNS - w) / 2;
  y = (MAX_ROWS - h) / 2 - 2;
  key = 0;
  rawkey = 0;

  while (result == -2)
  {
    char buf[32];
    printstatus();
    dlg_drawframe(x, y, w, h, title);
    printtext(x + 3, y + 2, MKCOL(7, C_PANEL), label);
    sprintf(buf, "$%0*X", digits, v);
    printtext(x + 4 + strlen(label), y + 2, MKCOL(15, C_BEATROW), buf);
    printcursor(x + 5 + strlen(label) + col, y + 2, 1);
    printtext(x + 3, y + 4, MKCOL(3, C_PANEL), "0-9 A-F edit, ENTER = OK, ESC = cancel");
    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      result = 0;
      break;
    }
    {
      // getkey() does not convert hex itself
      int k = tolower(key);
      int n = -1;
      if ((k >= '0') && (k <= '9')) n = k - '0';
      if ((k >= 'a') && (k <= 'f')) n = k - 'a' + 10;
      if (n >= 0)
      {
        int shift = (digits - 1 - col) * 4;
        v &= ~(0xfu << shift);
        v |= (unsigned)n << shift;
        if (col < digits - 1) col++;
      }
    }
    switch (rawkey)
    {
      case KEY_LEFT:
      case KEY_BACKSPACE:
      if (col > 0) col--;
      break;
      case KEY_RIGHT:
      if (col < digits - 1) col++;
      break;
      case KEY_ENTER:
      result = 1;
      break;
      case KEY_ESC:
      result = 0;
      break;
    }
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && ((mousex < x) || (mousex >= x + w) || (mousey < y) || (mousey >= y + h)))
      result = 0;
  }
  key = 0;
  rawkey = 0;
  if (result == 1) *value = v;
  printmainscreen();
  return result == 1;
}

// Edit a line of text. Returns 1 if accepted. "decimal" allows only digits
// and ':' (for times like 3:25).
int dlg_edittext(const char *title, const char *label, char *text, int maxlen, int decimal)
{
  char buf[256];
  int w, h = 7;
  int x, y;
  int result = -2;
  int fieldw = maxlen < 40 ? maxlen + 1 : 40;

  if (maxlen > 255) maxlen = 255;
  strncpy(buf, text, maxlen);
  buf[maxlen] = 0;
  w = strlen(label) + fieldw + 10;
  if ((title) && ((int)strlen(title) + 14 > w)) w = strlen(title) + 14;
  if (w < 44) w = 44;
  x = (MAX_COLUMNS - w) / 2;
  y = (MAX_ROWS - h) / 2 - 2;
  key = 0;
  rawkey = 0;

  editstring_click(buf, maxlen);     // cursor at the end
  while (result == -2)
  {
    int len = strlen(buf), cur = editstring_cursor(buf);
    int ofs = cur >= fieldw ? cur - fieldw + 1 : 0;
    int fx = x + 4 + strlen(label);
    char vis[64];
    printstatus();
    dlg_drawframe(x, y, w, h, title);
    printtext(x + 3, y + 2, MKCOL(7, C_PANEL), label);
    printblankc(fx, y + 2, MKCOL(15, C_BEATROW), fieldw);
    strncpy(vis, buf + ofs, fieldw);
    vis[fieldw] = 0;
    printtext(fx, y + 2, MKCOL(15, C_BEATROW), vis);
    printcursor(fx + cur - ofs, y + 2, 1);
    printtext(x + 3, y + 4, MKCOL(3, C_PANEL), "ENTER = OK, ESC = cancel, CTRL+V paste");
    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      result = 0;
      break;
    }
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (mousey == y + 2) && (mousex >= fx) && (mousex < fx + fieldw))
      editstring_click(buf, mousex - fx + ofs);
    switch (rawkey)
    {
      case KEY_ENTER:
      result = 1;
      break;
      case KEY_ESC:
      result = 0;
      break;
      default:
      if ((decimal) && (key >= 32) && !(((key >= '0') && (key <= '9')) || (key == ':') || (key == '.'))) key = 0;
      editstring(buf, maxlen + 1);
      break;
    }
    (void)len;
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && ((mousex < x) || (mousex >= x + w) || (mousey < y) || (mousey >= y + h)))
      result = 0;
  }
  key = 0;
  rawkey = 0;
  if (result == 1) strcpy(text, buf);
  printmainscreen();
  return result == 1;
}

// ---------------------------------------------------------------------------
// The menu of the colour schemes (the button in the top right corner).
// Pointing at a scheme shows it at once; a click keeps it, ESC or a click
// outside goes back to the scheme from before.
// ---------------------------------------------------------------------------

static unsigned mixrgb(unsigned a, unsigned b, int t)
{
  int r = (((a >> 16) & 0xff) * (256 - t) + ((b >> 16) & 0xff) * t) >> 8;
  int g = (((a >> 8) & 0xff) * (256 - t) + ((b >> 8) & 0xff) * t) >> 8;
  int bl = ((a & 0xff) * (256 - t) + (b & 0xff) * t) >> 8;
  return (r << 16) | (g << 8) | bl;
}

// coverage (0-256) of a pixel by a rounded rectangle
static int rrcover(int px, int py, int x0, int y0, int x1, int y1, int r)
{
  float fx = px + 0.5f, fy = py + 0.5f, cx, cy, dx, dy, d;
  if ((fx < x0) || (fx > x1) || (fy < y0) || (fy > y1)) return 0;
  cx = fx < x0 + r ? x0 + r : (fx > x1 - r ? x1 - r : fx);
  cy = fy < y0 + r ? y0 + r : (fy > y1 - r ? y1 - r : fy);
  dx = fx - cx;
  dy = fy - cy;
  if ((dx == 0) && (dy == 0)) return 256;
  d = r - sqrtf(dx * dx + dy * dy) + 0.5f;
  if (d <= 0) return 0;
  if (d >= 1) return 256;
  return (int)(d * 256);
}

static void drawswatches(int x, int y, int sel)
{
  int cw = 6, ch = NUM_THEMES, t, px, py;
  int pw = cw * 16;
  unsigned *buf = fxcanvas_begin(0, cw, ch);
  unsigned panel = theme_color(theme, C_PANEL);
  if (!buf) return;
  for (t = 0; t < NUM_THEMES; t++)
  {
    unsigned bgc = (t == sel) ? theme_color(theme, C_SELECT) : panel;
    unsigned *row = buf + t * 32 * pw;
    for (py = 0; py < 32; py++)
      for (px = 0; px < pw; px++)
      {
        unsigned c = bgc;
        int k, a;
        // the card: background and frame of the scheme
        a = rrcover(px, py, 4, 5, pw - 4, 27, 8);
        if (a) c = mixrgb(c, theme_color(t, 27), a);
        a = rrcover(px, py, 5, 6, pw - 5, 26, 7);
        if (a) c = mixrgb(c, theme_color(t, 0), a);
        // three dots: accent, second accent, playing
        for (k = 0; k < 3; k++)
        {
          static const int idx[3] = {2, 5, 4};
          a = rrcover(px, py, 16 + k * 24 - 6, 10, 16 + k * 24 + 6, 22, 6);
          if (a) c = mixrgb(c, theme_color(t, idx[k]), a);
        }
        row[py * pw + px] = c;
      }
  }
  fxcanvas_draw(0, x, y);
}

void themechooser(void)
{
  int prevmx = mousex, prevmy = mousey;
  int old = theme, sel = theme, c, done = 0;
  int w = 26, h = NUM_THEMES + 2;
  int x = MAX_COLUMNS - 17 - w, y = 1;

  key = 0;
  rawkey = 0;
  while (!done)
  {
    int hit = -1;
    printstatus();
    clearclip();
    snprintf(tip_context, sizeof tip_context, "%s", "Colour scheme");
    tip_newframe();
    setdimrect(x, y, w - 1, h - 1);
    setdimamount(60);
    fillarea(x, y, w, h, MKCOL(8, C_PANEL), ' ');
    for (c = 0; c < NUM_THEMES; c++)
    {
      int ry = y + 1 + c;
      int bg = c == sel ? C_SELECT : C_PANEL;
      printblankc(x + 1, ry, MKCOL(8, bg), w - 2);
      printtext(x + 9, ry, MKCOL(c == old ? 15 : 8, bg), theme_name(c));
      if (c == old) printtext(x + w - 3, ry, MKCOL(2, bg), GS_CHECK);
    }
    drawswatches(x + 2, y + 1, sel);
    fliptoscreen();
    getkey();

    // the scheme under the pointer (after the pointer moved) is shown at once
    hit = -1;
    if ((mousex >= x) && (mousex < x + w) && (mousey >= y + 1) && (mousey < y + 1 + NUM_THEMES)) hit = mousey - y - 1;
    if ((hit >= 0) && (hit != sel) && ((mousex != prevmx) || (mousey != prevmy))) sel = hit;
    if (win_quitted) { sel = old; break; }
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      if (hit < 0) sel = old;
      else sel = hit;
      done = 1;
    }
    switch (rawkey)
    {
      case KEY_UP: if (sel > 0) sel--; break;
      case KEY_DOWN: if (sel < NUM_THEMES - 1) sel++; break;
      case KEY_ENTER: case KEY_SPACE: done = 1; break;
      case KEY_ESC: sel = old; done = 1; break;
    }
    prevmx = mousex;
    prevmy = mousey;
    if (sel != theme) settheme(sel);
  }
  if (sel != theme) settheme(sel);
  // wait until the mouse button is up, the click is not passed on
  while ((mouseb) && (!win_quitted))
  {
    fliptoscreen();
    getkey();
  }
  fxcanvas_end();
  key = 0;
  rawkey = 0;
  printmainscreen();
}

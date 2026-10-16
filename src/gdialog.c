//
// C64 GrooveGoat - simple modal dialogs drawn in the character screen
//

#define GDIALOG_C

#include "goattrk2.h"
#include "gres.h"

void dlg_drawframe(int x, int y, int w, int h, const char *title)
{
  int c;
  // drop shadow
  for (c = 1; c <= h; c++)
    printbgx(x + w, y + c, C_LINE, 1);
  printbgx(x + 1, y + h, C_LINE, w);

  fillarea(x, y, w, h, MKCOL(8, C_PANEL), ' ');
  printblankc(x, y, MKCOL(C_HEADERTEXT, C_HEADER), w);
  if (title) printtext(x + 2, y, MKCOL(C_HEADERTEXT, C_HEADER), title);
  for (c = 0; c < 5; c++)
    printglyph(x + w - 7 + c, y, MKCOL(C_RB1 + c, C_HEADER), GL_RIGHTH);
  for (c = 1; c < h; c++)
  {
    printglyph(x, y + c, MKCOL(C_LINE, C_PANEL), GL_VLINE);
    printglyph(x + w - 1, y + c, MKCOL(C_LINE, C_PANEL), GL_VLINE);
  }
  for (c = 1; c < w - 1; c++)
    printglyph(x + c, y + h - 1, MKCOL(C_LINE, C_PANEL), GL_HLINE);
  printglyph(x, y + h - 1, MKCOL(C_LINE, C_PANEL), GL_BL);
  printglyph(x + w - 1, y + h - 1, MKCOL(C_LINE, C_PANEL), GL_BR);
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

//
// C64 GrooveGoat screen display routines
// (based on the GoatTracker v2 display code)
//

#define GDISPLAY_C

#include "goattrk2.h"
#include "gres.h"

char *notename[] =
 {"C-0", "C#0", "D-0", "D#0", "E-0", "F-0", "F#0", "G-0", "G#0", "A-0", "A#0", "B-0",
  "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
  "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
  "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
  "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4",
  "C-5", "C#5", "D-5", "D#5", "E-5", "F-5", "F#5", "G-5", "G#5", "A-5", "A#5", "B-5",
  "C-6", "C#6", "D-6", "D#6", "E-6", "F-6", "F#6", "G-6", "G#6", "A-6", "A#6", "B-6",
  "C-7", "C#7", "D-7", "D#7", "E-7", "F-7", "F#7", "G-7", "G#7", "...", "---", "+++"};

char timechar[] = {':', ' '};

int timemin = 0;
int timesec = 0;
int timeframe = 0;
char statusmessage[256] = {0};
static char shownmessage[256] = {0};
static int messagetimer = 0;
int chnview = 0;
int ordchnview = 0;

UIBUTTON uibuttons[MAX_UIBUTTONS];
int numuibuttons = 0;

// Colors used for the pattern display
#define COL_ROWNUM MKCOL(3, 0)
#define COL_ROWNUMBEAT MKCOL(7, 0)

void ui_addbutton(int x, int y, int w, int id, int param)
{
  if (numuibuttons >= MAX_UIBUTTONS) return;
  uibuttons[numuibuttons].x = x;
  uibuttons[numuibuttons].y = y;
  uibuttons[numuibuttons].w = w;
  uibuttons[numuibuttons].id = id;
  uibuttons[numuibuttons].param = param;
  numuibuttons++;
}

int ui_hitbutton(int mx, int my, int *param)
{
  int c;
  for (c = numuibuttons - 1; c >= 0; c--)
  {
    if ((my == uibuttons[c].y) && (mx >= uibuttons[c].x) && (mx < uibuttons[c].x + uibuttons[c].w))
    {
      if (param) *param = uibuttons[c].param;
      return uibuttons[c].id;
    }
  }
  return BTN_NONE;
}

// A button: label drawn with button colors; "active" shows it highlighted
void ui_drawbutton(int x, int y, const char *label, int id, int param, int active)
{
  int w = strlen(label);
  int hover = (mousey == y) && (mousex >= x) && (mousex < x + w);
  int bg = active ? C_BUTTONHI : C_BUTTON;
  if ((hover) && (!active)) bg = C_SELECT;
  printtext(x, y, MKCOL(C_BUTTONTEXT, bg), label);
  ui_addbutton(x, y, w, id, param);
}

void setstatusmessage(const char *msg)
{
  strncpy(statusmessage, msg, sizeof statusmessage - 1);
  statusmessage[sizeof statusmessage - 1] = 0;
}

void formatsongtime(int frames, char *buf)
{
  int fps;
  int secs;
  if (editorInfo.multiplier)
    fps = (editorInfo.ntsc ? NTSCFRAMERATE : PALFRAMERATE) * editorInfo.multiplier;
  else
    fps = (editorInfo.ntsc ? NTSCFRAMERATE : PALFRAMERATE) / 2;
  secs = frames / fps;
  sprintf(buf, "%02d:%02d", secs / 60, secs % 60);
}

void validatechnview(void)
{
  int group = VISIBLECHANNELS;
  if (numchannels <= VISIBLECHANNELS)
  {
    chnview = 0;
  }
  else
  {
    if (editorInfo.epchn < chnview) chnview = (editorInfo.epchn / group) * group;
    if (editorInfo.epchn >= chnview + group) chnview = (editorInfo.epchn / group) * group;
    if (chnview > numchannels - group) chnview = numchannels - group;
    if (chnview < 0) chnview = 0;
  }
  if (numchannels <= VISIBLEORDERCHNS)
    ordchnview = 0;
  else
  {
    if (editorInfo.eschn < ordchnview) ordchnview = editorInfo.eschn;
    if (editorInfo.eschn >= ordchnview + VISIBLEORDERCHNS) ordchnview = editorInfo.eschn - VISIBLEORDERCHNS + 1;
    if (ordchnview > numchannels - VISIBLEORDERCHNS) ordchnview = numchannels - VISIBLEORDERCHNS;
    if (ordchnview < 0) ordchnview = 0;
  }
}

void printmainscreen(void)
{
  clearscreen();
  printstatus();
  forceredraw();
  fliptoscreen();
}

void displayupdate(void)
{
  if (cursorflashdelay >= 6)
  {
    cursorflashdelay %= 6;
    cursorflash++;
    cursorflash &= 3;
  }
  printstatus();
  fliptoscreen();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

static void drawtoolbar(void)
{
  int x;
  char buf[80];
  int hc = MKCOL(C_HEADERTEXT, C_HEADER);

  printblankc(0, TOOLBAR_Y, hc, MAX_COLUMNS);
  drawimage(0, TOOLBAR_Y, "logo", 2, 1);
  printtext(3, TOOLBAR_Y, hc, PROGRAMNAME);
  printtext(3 + strlen(PROGRAMNAME) + 1, TOOLBAR_Y, MKCOL(C_RB3, C_HEADER), PROGRAMVERSION);
  // the C64 rainbow
  for (x = 0; x < 5; x++)
    printglyph(23 + x, TOOLBAR_Y, MKCOL(C_RB1 + x, C_HEADER), GL_RIGHTH);

  x = 29;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_NEWDOC " New ", BTN_NEW, 0, 0); x += 8;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_FOLDER " Open ", BTN_OPEN, 0, 0); x += 9;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_DISK " Save ", BTN_SAVE, 0, 0); x += 9;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_LOADINS " Load Ins ", BTN_INSLOAD, 0, 0); x += 13;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_SAVEINS " Save Ins ", BTN_INSSAVE, 0, 0); x += 13;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_CHIP " Export SID ", BTN_EXPORTSID, 0, 0); x += 15;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_PRG " Export PRG ", BTN_EXPORTPRG, 0, 0); x += 15;

  x = MAX_COLUMNS - 30;
  if (theme == THEME_NIGHT)
    ui_drawbutton(x, TOOLBAR_Y, " " GS_MOON " Night ", BTN_THEME, 0, 0);
  else
    ui_drawbutton(x, TOOLBAR_Y, " " GS_SUN " Day   ", BTN_THEME, 0, 0);
  x += 11;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_HELP " Help ", BTN_HELP, 0, 0); x += 10;
  ui_drawbutton(x, TOOLBAR_Y, " " GS_INFO " Info ", BTN_INFO, 0, 0);

  (void)buf;
}

static void drawsettingsbar(void)
{
  int x = 0;
  int pc = MKCOL(7, C_PANEL);
  char buf[80];
  int c;

  printblankc(0, SETTINGS_Y, pc, MAX_COLUMNS);
  x = 1;
  ui_drawbutton(x, SETTINGS_Y, " " GS_PLAY " Play ", BTN_PLAY, 0, isplaying() && lastsonginit == PLAY_BEGINNING); x += 9;
  ui_drawbutton(x, SETTINGS_Y, " " GS_PLAYPOS " Pos ", BTN_PLAYPOS, 0, isplaying() && lastsonginit == PLAY_POS); x += 8;
  ui_drawbutton(x, SETTINGS_Y, " " GS_PLAYPAT " Patt ", BTN_PLAYPATT, 0, isplaying() && lastsonginit == PLAY_PATTERN); x += 9;
  ui_drawbutton(x, SETTINGS_Y, " " GS_STOP " Stop ", BTN_STOP, 0, 0); x += 9;
  ui_drawbutton(x, SETTINGS_Y, " Follow ", BTN_FOLLOW, 0, followplay); x += 10;

  printtext(x, SETTINGS_Y, MKCOL(15, C_PANEL), "SIDs");
  x += 5;
  for (c = 0; c < 4; c++)
  {
    static const int n[4] = {1, 2, 4, 8};
    sprintf(buf, " %d ", n[c]);
    ui_drawbutton(x, SETTINGS_Y, buf, BTN_SIDS1 + c, n[c], numsids == n[c]);
    x += 4;
  }
  x += 1;
  ui_drawbutton(x, SETTINGS_Y, editorInfo.sidmodel ? " 8580 " : " 6581 ", BTN_MODEL, 0, 0); x += 7;
  ui_drawbutton(x, SETTINGS_Y, editorInfo.ntsc ? " NTSC " : " PAL ", BTN_NTSC, 0, 0); x += editorInfo.ntsc ? 7 : 6;
  if (editorInfo.multiplier)
    sprintf(buf, " " GS_PREV "%2dx" GS_NEXT " ", editorInfo.multiplier);
  else
    sprintf(buf, " " GS_PREV "25Hz" GS_NEXT " ");
  ui_drawbutton(x, SETTINGS_Y, buf, BTN_SPEED, 0, 0); x += strlen(buf) + 1;
  sprintf(buf, " HR %04X ", editorInfo.adparam);
  ui_drawbutton(x, SETTINGS_Y, buf, BTN_HR, 0, eamode);
  if (eamode) printcursor(x + 4 + editorInfo.eacolumn, SETTINGS_Y, 1);
  x += 10;
  ui_drawbutton(x, SETTINGS_Y, " FV ", BTN_FV, 0, editorInfo.finevibrato); x += 5;
  ui_drawbutton(x, SETTINGS_Y, " PO ", BTN_PO, 0, editorInfo.optimizepulse); x += 5;
  ui_drawbutton(x, SETTINGS_Y, " RO ", BTN_RO, 0, editorInfo.optimizerealtime); x += 6;
  ui_drawbutton(x, SETTINGS_Y, " INT ", BTN_INT, 0, interpolate & 1); x += 6;
  ui_drawbutton(x, SETTINGS_Y, " FP ", BTN_FP, 0, (interpolate & 2) ? 1 : 0); x += 6;
  ui_drawbutton(x, SETTINGS_Y, stereomode ? " Stereo " : " Mono ", BTN_STEREO, 0, 0);
}

// ---------------------------------------------------------------------------
// Pattern editor
// ---------------------------------------------------------------------------

static void drawlevel(int x, int y, int level, int bg)
{
  // 3 cells, 24 steps
  int steps = level * 24 / 255;
  int c;
  for (c = 0; c < 3; c++)
  {
    int v = steps - c * 8;
    int col = c == 0 ? C_RB4 : (c == 1 ? C_RB3 : C_RB1);
    if (v <= 0)
      printglyph(x + c, y, MKCOL(C_LINE, bg), GL_BAR1);
    else
    {
      if (v > 8) v = 8;
      printglyph(x + c, y, MKCOL(col, bg), GL_BAR1 + v - 1);
    }
  }
}

static int chnlevel(int c)
{
  int env;
  if (chn[c].mute) return 0;
  env = sid_getenv(c / 3, c % 3);
  if (!(sidreg[c / 3][4 + 7 * (c % 3)] & 0xf0)) env = env / 2;
  return env;
}

static void drawpatterns(void)
{
  int c, d, i;
  int cc = cursorcolortable[cursorflash];
  int vis = numchannels < VISIBLECHANNELS ? numchannels : VISIBLECHANNELS;
  char buf[80];

  (void)cc;
  validatechnview();

  // Panel header
  printblankc(PATT_X, PATT_Y, MKCOL(C_HEADERTEXT, editorInfo.editmode == EDIT_PATTERN ? C_BUTTONHI : C_HEADER), PATT_W);
  printtext(PATT_X + 1, PATT_Y, MKCOL(C_HEADERTEXT, editorInfo.editmode == EDIT_PATTERN ? C_BUTTONHI : C_HEADER), "PATTERNS");
  if (numsids > 2)
  {
    int x = PATT_X + 14;
    for (c = 0; c < numchannels; c += VISIBLECHANNELS)
    {
      sprintf(buf, " SID %d-%d ", c / 3 + 1, c / 3 + 2);
      ui_drawbutton(x, PATT_Y, buf, BTN_CHNVIEW, c, chnview == c);
      x += strlen(buf) + 1;
    }
  }
  sprintf(buf, "%d SID%s  %d channels", numsids, numsids > 1 ? "s" : "", numchannels);
  printtext(PATT_X + PATT_W - strlen(buf) - 1, PATT_Y, MKCOL(C_HEADERTEXT, editorInfo.editmode == EDIT_PATTERN ? C_BUTTONHI : C_HEADER), buf);

  // Background of the pattern area
  fillarea(PATT_X, PATT_CHNHDR_Y, PATT_W, VISIBLEPATTROWS + 1, MKCOL(8, C_PANEL), ' ');

  if ((followplay) && (isplaying()))
  {
    for (c = 0; c < numchannels; c++)
    {
      int newpos = chn[c].pattptr / 4;
      if (chn[c].advance) editorUndoInfo.editorInfo[c].epnum = chn[c].pattnum;

      if (newpos > pattlen[editorUndoInfo.editorInfo[c].epnum]) newpos = pattlen[editorUndoInfo.editorInfo[c].epnum];

      if (c == editorInfo.epchn)
      {
        editorInfo.eppos = newpos;
        editorInfo.epview = newpos-VISIBLEPATTROWS/2;
      }

      newpos = chn[c].songptr;
      newpos--;
      if (newpos < 0) newpos = 0;
      if (newpos > songlen[editorInfo.esnum][c]) newpos = songlen[editorInfo.esnum][c];

      if ((c == editorInfo.eschn) && (chn[c].advance))
      {
        editorInfo.eseditpos = newpos;
        if (newpos - editorInfo.esview < 0)
          editorInfo.esview = newpos;
        if (newpos - editorInfo.esview >= VISIBLEORDERLIST)
          editorInfo.esview = newpos - VISIBLEORDERLIST + 1;
      }
    }
  }

  for (i = 0; i < vis; i++)
  {
    int x = CHN_X(i);
    int ch = chnview + i;
    int pn;
    int hdrbg = (ch == editorInfo.epchn) ? C_SELECT : C_PANEL;
    if (ch >= numchannels) break;
    pn = editorUndoInfo.editorInfo[ch].epnum;

    // Channel header
    printblankc(x - 1, PATT_CHNHDR_Y, MKCOL(15, hdrbg), CHN_W - 1);
    sprintf(buf, "%d.%d", ch / 3 + 1, ch % 3 + 1);
    printtext(x, PATT_CHNHDR_Y, MKCOL(chn[ch].mute ? CMUTE : 15, hdrbg), buf);
    sprintf(buf, "P%02X", pn);
    printtext(x + 4, PATT_CHNHDR_Y, MKCOL(14, hdrbg), buf);
    ui_addbutton(x + 4, PATT_CHNHDR_Y, 3, BTN_PATTNUM, ch);
    drawlevel(x + 8, PATT_CHNHDR_Y, chnlevel(ch), hdrbg);
    printtext(x + 11, PATT_CHNHDR_Y, chn[ch].mute ? MKCOL(C_HEADERTEXT, 4) : MKCOL(7, hdrbg), "M");
    ui_addbutton(x + 11, PATT_CHNHDR_Y, 1, BTN_MUTE, ch);
    {
      int solo = !chn[ch].mute;
      for (c = 0; c < numchannels; c++)
        if ((c != ch) && (!chn[c].mute)) solo = 0;
      printtext(x + 12, PATT_CHNHDR_Y, solo ? MKCOL(C_HEADERTEXT, 10) : MKCOL(7, hdrbg), "S");
    }
    ui_addbutton(x + 12, PATT_CHNHDR_Y, 1, BTN_SOLO, ch);

    for (d = 0; d < VISIBLEPATTROWS; d++)
    {
      int p = editorInfo.epview + d;
      int y = PATT_ROWS_Y + d;
      int rowbg = C_PANEL;
      int playing = 0;
      int muted = chn[ch].mute;
      int beat = (p >= 0) && ((p % stepsize) == 0);
      unsigned char *pp;

      if ((p < 0) || (p > pattlen[pn]))
        continue;

      if ((pn == chn[ch].pattnum) && (isplaying()))
      {
        int chnrow = chn[ch].pattptr / 4;
        if (chnrow > pattlen[chn[ch].pattnum]) chnrow = pattlen[chn[ch].pattnum];
        if (chnrow == p) playing = 1;
      }

      if (beat) rowbg = C_BEATROW;
      if (p == editorInfo.eppos) rowbg = C_EDITROW;
      if (playing) rowbg = C_PLAYROW;

      printblankc(x - 1, y, MKCOL(8, rowbg), CHN_W - 1);

      if (!(patterndispmode & 1))
      {
        if (p < 100) sprintf(buf, " %02d", p);
        else sprintf(buf, "%03d", p);
      }
      else
        sprintf(buf, " %02X", p);
      printtext(x - 1, y, MKCOL(beat ? 7 : 3, rowbg), buf);

      pp = &pattern[pn][p*4];
      if (pp[0] == ENDPATT)
      {
        printtext(x + 3, y, MKCOL(muted ? CMUTE : 7, rowbg), "-- END --");
      }
      else
      {
        int note = pp[0];
        int notecol = 15;
        if (note == REST) notecol = 3;
        else if ((note == KEYOFF) || (note == KEYON)) notecol = 6;
        if (muted) notecol = CMUTE;
        printtext(x + 3, y, MKCOL(notecol, rowbg), notename[note - FIRSTNOTE]);

        if ((!pp[1]) && (patterndispmode & 2))
          printtext(x + 7, y, MKCOL(3, rowbg), "..");
        else
        {
          sprintf(buf, "%02X", pp[1]);
          printtext(x + 7, y, MKCOL(muted ? CMUTE : (pp[1] ? 11 : 3), rowbg), buf);
        }
        if ((!pp[2]) && (patterndispmode & 2))
          printtext(x + 9, y, MKCOL(3, rowbg), "...");
        else
        {
          sprintf(buf, "%01X", pp[2]);
          printtext(x + 9, y, MKCOL(muted ? CMUTE : (pp[2] ? 14 : 3), rowbg), buf);
          sprintf(buf, "%02X", pp[3]);
          printtext(x + 10, y, MKCOL(muted ? CMUTE : (pp[2] ? 14 : 3), rowbg), buf);
        }
      }

      // Marked area
      if (ch == editorInfo.epmarkchn)
      {
        int lo = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
        int hi = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkend : editorInfo.epmarkstart;
        if ((p >= lo) && (p <= hi))
          printbgx(x + 3, y, C_SELECT, 9);
      }

      // Cursor
      if ((p == editorInfo.eppos) && (editorInfo.editmode == EDIT_PATTERN) && (editorInfo.epchn == ch) && (!eamode))
      {
        switch (editorInfo.epcolumn)
        {
          case 0:
          printcursor(x + 3, y, 3);
          break;

          default:
          printcursor(x + 6 + editorInfo.epcolumn, y, 1);
          break;
        }
      }
    }
  }

  // Empty space when only one SID is used: logo & quick tips
  if (vis == 3)
  {
    int x = CHN_X(3) + 2;
    int y = PATT_ROWS_Y + 3;
    drawimage(x + 10, y, "logo", 12, 6);
    printtext(x + 6, y + 7, MKCOL(15, C_PANEL), "C64 GrooveGoat " PROGRAMVERSION);
    printtext(x + 6, y + 8, MKCOL(7, C_PANEL), "by " PROGRAMAUTHOR);
    for (c = 0; c < 5; c++)
      printglyph(x + 11 + c * 2, y + 10, MKCOL(C_RB1 + c, C_PANEL), GL_FULL),
      printglyph(x + 12 + c * 2, y + 10, MKCOL(C_RB1 + c, C_PANEL), GL_FULL);
    y += 12;
    printtext(x, y++, MKCOL(14, C_PANEL), "Quick start");
    printtext(x, y++, MKCOL(7, C_PANEL), "Z-M, Q-P   enter notes (piano layout)");
    printtext(x, y++, MKCOL(7, C_PANEL), "SPACE      toggle edit/jam mode");
    printtext(x, y++, MKCOL(7, C_PANEL), "F1 / F4    play / stop");
    printtext(x, y++, MKCOL(7, C_PANEL), "TAB        next editor panel");
    printtext(x, y++, MKCOL(7, C_PANEL), "F12        keyboard help");
    printtext(x, y++, MKCOL(7, C_PANEL), "CTRL/CMD+Z undo");
    y++;
    printtext(x, y++, MKCOL(7, C_PANEL), "Need more voices? Choose 2, 4 or 8");
    printtext(x, y++, MKCOL(7, C_PANEL), "SIDs in the toolbar above.");
    y++;
    printtext(x, y++, MKCOL(7, C_PANEL), "Instrument library: Load Ins button");
    printtext(x, y++, MKCOL(7, C_PANEL), "(6581 and 8580 instrument sets).");
  }
}

// ---------------------------------------------------------------------------
// Right panel: orderlist, instrument, tables, song info
// ---------------------------------------------------------------------------

static void panelheader(int y, const char *title, int active)
{
  int col = MKCOL(C_HEADERTEXT, active ? C_BUTTONHI : C_HEADER);
  printblankc(RP_X, y, col, RP_W);
  printtext(RP_X + 1, y, col, title);
}

static void draworderlist(void)
{
  int c, d;
  char buf[80];
  int active = editorInfo.editmode == EDIT_ORDERLIST;
  int hc = MKCOL(C_HEADERTEXT, active ? C_BUTTONHI : C_HEADER);

  panelheader(ORD_Y, "ORDERLIST", active);
  sprintf(buf, "SUBTUNE " GS_PREV "%02X" GS_NEXT, editorInfo.esnum);
  printtext(RP_X + 12, ORD_Y, hc, buf);
  ui_addbutton(RP_X + 20, ORD_Y, 4, BTN_SUBTUNE, 0);
  sprintf(buf, "POS %02X", editorInfo.eseditpos);
  printtext(RP_X + 27, ORD_Y, hc, buf);
  if (numchannels > VISIBLEORDERCHNS)
  {
    ui_drawbutton(RP_X + RP_W - 5, ORD_Y, " " GS_UP " ", BTN_ORDVIEW, -1, 0);
    ui_drawbutton(RP_X + RP_W - 3, ORD_Y, " " GS_DOWN " ", BTN_ORDVIEW, 1, 0);
  }

  fillarea(RP_X, ORD_ROWS_Y, RP_W, VISIBLEORDERCHNS, MKCOL(8, C_PANEL), ' ');

  for (c = ordchnview; (c < numchannels) && (c < ordchnview + VISIBLEORDERCHNS); c++)
  {
    int y = ORD_ROWS_Y + c - ordchnview;
    sprintf(buf, "%d.%d", c / 3 + 1, c % 3 + 1);
    printtext(RP_X + 1, y, MKCOL(c == editorInfo.eschn ? 15 : 7, C_PANEL), buf);

    for (d = 0; d < VISIBLEORDERLIST; d++)
    {
      int p = editorInfo.esview+d;
      int color = 8;
      int bg = C_PANEL;
      if (isplaying())
      {
        int chnpos = chn[c].songptr;
        chnpos--;
        if (chnpos < 0) chnpos = 0;
        if ((p == chnpos) && (chn[c].advance)) bg = C_PLAYROW;
      }
      if (p == editorUndoInfo.editorInfo[c].espos) color = CEDIT;
      if ((editorUndoInfo.editorInfo[c].esend) && (p == editorUndoInfo.editorInfo[c].esend)) color = CEDIT;

      if ((p < 0) || (p > (songlen[editorInfo.esnum][c]+1)) || (p > MAX_SONGLEN+1))
      {
        sprintf(buf, "   ");
      }
      else
      {
        unsigned char v = songorder[editorInfo.esnum][c][p];
        if (v < LOOPSONG)
        {
          if ((v < REPEAT) || (p >= songlen[editorInfo.esnum][c]))
          {
            sprintf(buf, "%02X ", v);
            if ((p >= songlen[editorInfo.esnum][c]) && (color == 8)) color = 6;
          }
          else
          {
            if (v >= TRANSUP)
            {
              sprintf(buf, "+%01X ", v&0xf);
              if (color == 8) color = 14;
            }
            else
            {
              if (v >= TRANSDOWN)
              {
                sprintf(buf, "-%01X ", 16-(v & 0x0f));
                if (color == 8) color = 14;
              }
              else
              {
                sprintf(buf, "R%01X ", (v+1) & 0x0f);
                if (color == 8) color = 13;
              }
            }
          }
        }
        if (v == LOOPSONG)
        {
          sprintf(buf, "RST");
          if (color == 8) color = 6;
        }
      }
      printtext(ORD_X + d*3, y, MKCOL(color, bg), buf);
      if (c == editorInfo.esmarkchn)
      {
        int lo = editorInfo.esmarkstart < editorInfo.esmarkend ? editorInfo.esmarkstart : editorInfo.esmarkend;
        int hi = editorInfo.esmarkstart < editorInfo.esmarkend ? editorInfo.esmarkend : editorInfo.esmarkstart;
        if ((p >= lo) && (p <= hi))
          printbgx(ORD_X + d*3, y, C_SELECT, p != hi ? 3 : 2);
      }
      if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (editorInfo.eschn == c) && (!eamode))
        printcursor(ORD_X + d*3 + editorInfo.escolumn, y, 1);
    }
  }
}

static void drawinstrument(void)
{
  char buf[80];
  int active = (editorInfo.editmode == EDIT_INSTRUMENT);
  int hc = MKCOL(C_HEADERTEXT, active ? C_BUTTONHI : C_HEADER);
  int c;
  static const char *names[9] = {"Attack/Decay", "Sustain/Release", "Wavetable Pos", "Pulsetable Pos",
    "Filtertable Pos", "Vibrato Param", "Vibrato Delay", "HR/Gate Timer", "1stFrame Wave"};
  int vals[9];

  panelheader(INS_Y, "INSTRUMENT", active);
  sprintf(buf, GS_PREV "%02X" GS_NEXT, editorInfo.einum);
  printtext(RP_X + 12, INS_Y, hc, buf);
  ui_addbutton(RP_X + 12, INS_Y, 4, BTN_INSTRNUM, 0);
  sprintf(buf, "%-16s", instr[editorInfo.einum].name);
  printtext(RP_X + 17, INS_Y, MKCOL(C_RB3, active ? C_BUTTONHI : C_HEADER), buf);
  ui_drawbutton(RP_X + RP_W - 10, INS_Y, " Library ", BTN_LIBRARY, 0, 0);

  fillarea(RP_X, INS_ROWS_Y, RP_W, 5, MKCOL(8, C_PANEL), ' ');
  vals[0] = instr[editorInfo.einum].ad;
  vals[1] = instr[editorInfo.einum].sr;
  vals[2] = instr[editorInfo.einum].ptr[WTBL];
  vals[3] = instr[editorInfo.einum].ptr[PTBL];
  vals[4] = instr[editorInfo.einum].ptr[FTBL];
  vals[5] = instr[editorInfo.einum].ptr[STBL];
  vals[6] = instr[editorInfo.einum].vibdelay;
  vals[7] = instr[editorInfo.einum].gatetimer;
  vals[8] = instr[editorInfo.einum].firstwave;
  for (c = 0; c < 9; c++)
  {
    int x = c < 5 ? INS_COL1_X : INS_COL2_X;
    int y = INS_ROWS_Y + (c % 5);
    int sel = (editorInfo.eipos == c);
    printtext(x, y, MKCOL(sel ? CEDIT : 7, C_PANEL), names[c]);
    sprintf(buf, "%02X", vals[c]);
    printtext(x + INS_VAL_OFS, y, MKCOL(sel ? CEDIT : 15, C_PANEL), buf);
  }

  if ((editorInfo.editmode == EDIT_INSTRUMENT) && (!eamode))
  {
    if (editorInfo.eipos < 9)
      printcursor((editorInfo.eipos < 5 ? INS_COL1_X : INS_COL2_X) + INS_VAL_OFS + editorInfo.eicolumn, INS_ROWS_Y + (editorInfo.eipos % 5), 1);
    else
      printcursor(RP_X + 17 + strlen(instr[editorInfo.einum].name), INS_Y, 1);
  }
}

static void drawtables(void)
{
  int c, d;
  char buf[80];
  int active = (editorInfo.editmode == EDIT_TABLES);
  static const char *tnames[4] = {"WAVE", "PULSE", "FILTER", "SPEED"};

  panelheader(TBL_Y, "", active);
  for (c = 0; c < MAX_TABLES; c++)
    printtext(TBL_X(c), TBL_Y, MKCOL(C_HEADERTEXT, active ? C_BUTTONHI : C_HEADER), tnames[c]);
  fillarea(RP_X, TBL_ROWS_Y, RP_W, VISIBLETABLEROWS, MKCOL(8, C_PANEL), ' ');

  for (c = 0; c < MAX_TABLES; c++)
  {
    for (d = 0; d < VISIBLETABLEROWS; d++)
    {
      int p = editorInfo.etview[c]+d;
      int color = 8;
      int bg = (p & 1) ? C_PANEL : C_BEATROW;

      if (p >= MAX_TABLELEN) continue;
      switch (c)
      {
        case WTBL:
        if (ltable[c][p] >= WAVECMD) color = 14;
        break;

        case PTBL:
        if (ltable[c][p] >= 0x80) color = 14;
        break;

        case FTBL:
        if ((ltable[c][p] >= 0x80) || ((!ltable[c][p]) && (rtable[c][p]))) color = 14;
        break;
      }
      if (ltable[c][p] == 0xff) color = 13;
      if ((!ltable[c][p]) && (!rtable[c][p])) color = 3;
      if ((p == editorInfo.etpos) && (editorInfo.etnum == c)) color = CEDIT;
      sprintf(buf, "%02X", p+1);
      printtext(TBL_X(c), TBL_ROWS_Y + d, MKCOL(3, bg), buf);
      sprintf(buf, "%02X %02X", ltable[c][p], rtable[c][p]);
      printtext(TBL_X(c) + 3, TBL_ROWS_Y + d, MKCOL(color, bg), buf);

      if (editorInfo.etmarknum == c)
      {
        int lo = editorInfo.etmarkstart < editorInfo.etmarkend ? editorInfo.etmarkstart : editorInfo.etmarkend;
        int hi = editorInfo.etmarkstart < editorInfo.etmarkend ? editorInfo.etmarkend : editorInfo.etmarkstart;
        if ((p >= lo) && (p <= hi))
          printbgx(TBL_X(c) + 3, TBL_ROWS_Y + d, C_SELECT, 5);
      }
    }
  }

  if ((editorInfo.editmode == EDIT_TABLES) && (!eamode))
    printcursor(TBL_X(editorInfo.etnum) + 3 + (editorInfo.etcolumn & 1) + (editorInfo.etcolumn/2)*3, TBL_ROWS_Y + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], 1);
}

static void drawnames(void)
{
  char buf[80];
  int active = (editorInfo.editmode == EDIT_NAMES);
  int c;
  static const char *labels[3] = {"Name", "Author", "Released"};
  char *values[3];

  values[0] = songname;
  values[1] = authorname;
  values[2] = copyrightname;

  panelheader(NAMES_Y, "SONG INFO", active);
  fillarea(RP_X, NAMES_ROWS_Y, RP_W, 3, MKCOL(8, C_PANEL), ' ');
  for (c = 0; c < 3; c++)
  {
    printtext(NAMES_X, NAMES_ROWS_Y + c, MKCOL(7, C_PANEL), labels[c]);
    sprintf(buf, "%-32s", values[c]);
    printtext(NAMES_VAL_X, NAMES_ROWS_Y + c, MKCOL(15, C_BEATROW), buf);
  }
  if ((editorInfo.editmode == EDIT_NAMES) && (!eamode))
    printcursor(NAMES_VAL_X + strlen(values[editorInfo.enpos]), NAMES_ROWS_Y + editorInfo.enpos, 1);
}

// ---------------------------------------------------------------------------
// Status area
// ---------------------------------------------------------------------------

static void drawstatusarea(void)
{
  char buf[200];
  int x, c;
  int sc = MKCOL(7, C_PANEL);

  fillarea(0, STATUS_Y - 1, MAX_COLUMNS, 1, MKCOL(8, 0), ' ');
  printblankc(0, STATUS_Y, sc, MAX_COLUMNS);
  printblankc(0, POSITIONS_Y, sc, MAX_COLUMNS);

  x = 1;
  sprintf(buf, " Octave " GS_PREV "%d" GS_NEXT " ", editorInfo.epoctave);
  ui_drawbutton(x, STATUS_Y, buf, BTN_OCTAVE, 0, 0);
  x += strlen(buf) + 1;
  ui_drawbutton(x, STATUS_Y, recordmode ? " " GS_DOT " EDIT MODE " : " JAM MODE  ", BTN_RECORD, 0, recordmode);
  x += 14;
  {
    static const char *aa[3] = {" Advance: all ", " Advance: notes ", " Advance: off "};
    ui_drawbutton(x, STATUS_Y, aa[autoadvance], BTN_AUTOADV, 0, 0);
    x += strlen(aa[autoadvance]) + 1;
  }
  {
    static const char *km[3] = {" Keys: Tracker ", " Keys: DMC ", " Keys: Janko "};
    ui_drawbutton(x, STATUS_Y, km[keypreset], BTN_KEYMODE, 0, 0);
    x += strlen(km[keypreset]) + 1;
  }
  sprintf(buf, "Step %d", stepsize);
  printtext(x, STATUS_Y, sc, buf);
  x += strlen(buf) + 2;
  ui_drawbutton(x, STATUS_Y, " " GS_UNDO " Undo ", BTN_UNDO, 0, 0);

  // Play status and time
  if (editorInfo.multiplier)
  {
    if (!editorInfo.ntsc)
      sprintf(buf, "%02d%c%02d", timemin, timechar[timeframe/(25* editorInfo.multiplier) & 1], timesec);
    else
      sprintf(buf, "%02d%c%02d", timemin, timechar[timeframe/(30* editorInfo.multiplier) & 1], timesec);
  }
  else
  {
    if (!editorInfo.ntsc)
      sprintf(buf, "%02d%c%02d", timemin, timechar[(timeframe/13) & 1], timesec);
    else
      sprintf(buf, "%02d%c%02d", timemin, timechar[(timeframe/15) & 1], timesec);
  }
  if (isplaying())
  {
    printglyph(MAX_COLUMNS - 22, STATUS_Y, MKCOL(10, C_PANEL), GL_PLAY);
    printtext(MAX_COLUMNS - 20, STATUS_Y, MKCOL(10, C_PANEL), "PLAYING");
  }
  else
  {
    printglyph(MAX_COLUMNS - 22, STATUS_Y, MKCOL(7, C_PANEL), GL_STOP);
    printtext(MAX_COLUMNS - 20, STATUS_Y, sc, "STOPPED");
  }
  printtext(MAX_COLUMNS - 11, STATUS_Y, MKCOL(15, C_PANEL), buf);
  if (songmodified) printtext(MAX_COLUMNS - 4, STATUS_Y, MKCOL(14, C_PANEL), "*");

  // Channel positions of visible channels
  x = 1;
  for (c = chnview; (c < numchannels) && (c < chnview + VISIBLECHANNELS); c++)
  {
    int chnpos = chn[c].songptr;
    int chnrow = chn[c].pattptr/4;
    chnpos--;
    if (chnpos < 0) chnpos = 0;
    if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
    if (chnrow >= 100) chnrow -= 100;
    sprintf(buf, "%d.%d", c / 3 + 1, c % 3 + 1);
    printtext(x, POSITIONS_Y, MKCOL(chn[c].mute ? CMUTE : 7, C_PANEL), buf);
    sprintf(buf, "%03d/%02d", chnpos, chnrow);
    printtext(x + 4, POSITIONS_Y, MKCOL(15, C_PANEL), buf);
    ui_addbutton(x, POSITIONS_Y, 10, BTN_MUTE, c);
    x += 12;
  }
  // SID addresses
  x = MAX_COLUMNS - 1 - numsids * 6 - 5;
  printtext(x, POSITIONS_Y, sc, "SIDs");
  for (c = 0; c < numsids; c++)
  {
    sprintf(buf, "$%04X", sidaddr[c]);
    printtext(x + 5 + c * 6, POSITIONS_Y, MKCOL(c >= chnview / 3 && c < (chnview + VISIBLECHANNELS) / 3 ? 15 : 7, C_PANEL), buf);
  }
  ui_addbutton(x, POSITIONS_Y, 5 + numsids * 6, BTN_SIDADDR, 0);

  // Message line
  if (strcmp(statusmessage, shownmessage))
  {
    strcpy(shownmessage, statusmessage);
    messagetimer = 50 * 6;
  }
  printblankc(0, MESSAGE_Y, MKCOL(C_HEADERTEXT, C_HEADER), MAX_COLUMNS);
  if ((messagetimer > 0) && (shownmessage[0]))
  {
    messagetimer--;
    printglyph(1, MESSAGE_Y, MKCOL(C_RB3, C_HEADER), GL_BULLET);
    sprintf(buf, "%.*s", MAX_COLUMNS - 4, shownmessage);
    printtext(3, MESSAGE_Y, MKCOL(C_HEADERTEXT, C_HEADER), buf);
  }
  else
  {
    if (strlen(loadedsongfilename))
      snprintf(buf, sizeof buf, "%s", loadedsongfilename);
    else
      sprintf(buf, "Untitled song");
    printtext(1, MESSAGE_Y, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    printtext(MAX_COLUMNS - 73, MESSAGE_Y, MKCOL(C_BUTTONHI == C_HEADER ? 7 : C_RB5, C_HEADER),
      "F1 Play  F4 Stop  F5-F8 Editors  F9 Export  F10 Load  F11 Save  F12 Help");
  }
}

void printstatus(void)
{
  menu = 0;
  numuibuttons = 0;

  drawtoolbar();
  drawsettingsbar();
  drawpatterns();
  printglyph(PATT_W, PATT_Y, MKCOL(8, 0), ' ');
  draworderlist();
  printblankc(RP_X, ORD_ROWS_Y + VISIBLEORDERCHNS, MKCOL(8, 0), RP_W);
  drawinstrument();
  printblankc(RP_X, INS_ROWS_Y + 5, MKCOL(8, 0), RP_W);
  drawtables();
  printblankc(RP_X, TBL_ROWS_Y + VISIBLETABLEROWS, MKCOL(8, 0), RP_W);
  drawnames();
  drawstatusarea();
}

void resettime(void)
{
  timemin = 0;
  timesec = 0;
  timeframe = 0;
}

void incrementtime(void)
{
  timeframe++;
  if (!editorInfo.ntsc)
  {
    if (((editorInfo.multiplier) && (timeframe >= PALFRAMERATE* (int)editorInfo.multiplier))
        || ((!editorInfo.multiplier) && (timeframe >= PALFRAMERATE/2)))
    {
      timeframe = 0;
      timesec++;
    }
  }
  else
  {
    if (((editorInfo.multiplier) && (timeframe >= NTSCFRAMERATE* (int)editorInfo.multiplier))
        || ((!editorInfo.multiplier) && (timeframe >= NTSCFRAMERATE/2)))
    {
      timeframe = 0;
      timesec++;
    }
  }
  if (timesec == 60)
  {
    timesec = 0;
    timemin++;
    timemin %= 60;
  }
}

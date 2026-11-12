// C64 GrooveGoat - "Speech generator" dialog (CTRL+K)
//
// English text (or phonemes between slashes) -> instruments, wave / pulse /
// filter tables and pattern rows on the channel of the pattern cursor.

#include "goattrk2.h"
#include "gres.h"
#include "gspeech.h"

static SPEECHPARAMS sp;
static int spinit = 0;
static char listname[MAX_FILENAME] = "speech.txt";
static int songspeed = 0;     // frames per row the speech is made for, 0 = from the song

// song data before a preview (instruments, tables, patterns, lengths)
static void *prevdata = NULL;
static int prevlen[MAX_PATT];
static int previewing = 0;


static void spnotename(int n, char *buf)
{
  static const char *nn[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  sprintf(buf, "%s%d", nn[n % 12], n / 12);
}

static void endpreview(void)
{
  if (!previewing) return;
  snd_lock();
  stopsong();
  snd_unlock();
  // wait until the player has stopped before the data changes back
  while (isplaying()) SDL_Delay(5);
  snd_lock();
  songdata_restore(prevdata);
  memcpy(pattlen, prevlen, sizeof prevlen);
  snd_unlock();
  songdata_free(prevdata);
  prevdata = NULL;
  previewing = 0;
}

// generate into the song; keep = 0 for a preview / listing (the song is
// restored by endpreview)
static int generate(int keep, int chn, int row, char *err, int errsize)
{
  int rows;
  endpreview();
  if (!keep)
  {
    prevdata = songdata_save();
    if (!prevdata)
    {
      snprintf(err, errsize, "Out of memory");
      return 0;
    }
    memcpy(prevlen, pattlen, sizeof prevlen);
    previewing = 1;
  }
  snd_lock();
  rows = speech_generate(&sp, chn, row, err, errsize);
  snd_unlock();
  if (!rows) endpreview();
  return rows;
}

static int savelisting(void)
{
  FILE *f;
  if (!fileselector(listname, songpath, "*.txt", "SAVE SPEECH LISTING (text)", 3)) return 0;
  if ((!strchr(listname, '.')) && (strlen(listname) < MAX_FILENAME - 5)) strcat(listname, ".txt");
  f = fopen(listname, "w");
  if (!f) return -1;
  fputs(speech_listing(), f);
  fclose(f);
  return 1;
}

void speechdialog(void)
{
  int w = 92, h = 24;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0;
  int restoresel = -1;
  int chn = editorInfo.epchn;
  int row = editorInfo.eppos;
  char msg[160] = "";
  int msgcol = 7;
  int textofs = 0;

  if (!spinit)
  {
    speech_defaults(&sp);
    strcpy(sp.text, "TOGETHER WE HUNT");
    spinit = 1;
  }
  if (editorInfo.editmode != EDIT_PATTERN) editorInfo.editmode = EDIT_PATTERN;
  if (isplaying())
  {
    stopsong();
  }
  key = 0;
  rawkey = 0;

  for (;;)
  {
    int detected = speech_songtempo(chn);
    int fps = editorInfo.multiplier ? 50 * editorInfo.multiplier : 25;
    sp.tempo = songspeed ? songspeed : detected;
    // 0 text, 1 voice, 2 pitch, 3 speed, 4 melody, 5 song speed, 6 tempo command,
    // 7 preview, 8 insert, 9 listing, 10 cancel
    char buf[256], phs[1024], err[160], nb[16];
    int c, hitrow = -1, hitbtn = -1;
    int rows = 0, ms = 0, ok;
    int patt = editorUndoInfo.editorInfo[chn].epnum;
    int btnx[4] = {x0 + 3, x0 + 17, x0 + 30, x0 + w - 13};
    static const char *btntext[4] = {" " GS_PLAY " Preview ", " Insert ", " Save listing ", "  Cancel  "};

    err[0] = 0;
    ok = speech_phonemes(sp.text, phs, sizeof phs, err, sizeof err);
    if (ok) ok = speech_analyze(&sp, &rows, &ms, err, sizeof err);

    printstatus();
    dlg_drawframe(x0, y0, w, h, "Speech generator");
    for (c = 0; c <= 7; c++)
    {
      int isel = (c == sel);
      int bg = isel ? C_SELECT : C_PANEL;
      int ry = y0 + 2 + c + (c > 0);
      buf[0] = 0;
      switch (c)
      {
        case 0: sprintf(buf, "Text              "); break;
        case 1: sprintf(buf, "Voice             " GS_PREV " %s " GS_NEXT "   (%d/%d, sets the pitch)", speech_voicename(sp.voice), sp.voice + 1, speech_numvoices()); break;
        case 2:
          spnotename(sp.basenote, nb);
          sprintf(buf, "Pitch             " GS_PREV " %s " GS_NEXT "   (base note, SHIFT = octave)", nb);
          break;
        case 3: sprintf(buf, "Speed             " GS_PREV " %d%% " GS_NEXT "   (%s)", sp.speed, sp.speed > 100 ? "slower" : (sp.speed < 100 ? "faster" : "normal")); break;
        case 4: sprintf(buf, "Melody            " GS_PREV " %s " GS_NEXT, speech_melodyname(sp.melody)); break;
        case 5:
          if (songspeed) sprintf(buf, "Song speed        " GS_PREV " F%02X " GS_NEXT "  %d ms per row%s", sp.tempo, sp.tempo * 1000 / fps,
                                 (sp.tempo != detected) && (!sp.settempo) ? "  (the song plays at F" : "");
          else sprintf(buf, "Song speed        " GS_PREV " Auto: F%02X " GS_NEXT "  %d ms per row (tempo of the song)", sp.tempo, sp.tempo * 1000 / fps);
          if ((songspeed) && (sp.tempo != detected) && (!sp.settempo)) sprintf(buf + strlen(buf), "%02X!)", detected);
          break;
        case 6: sprintf(buf, "[%c] Put the tempo command F%02X on the first row", sp.settempo ? 'x' : ' ', sp.tempo); break;
        case 7:
        {
          int cb = (chn / 3) * 3 + (chn % 3 + 1) % 3;
          if (sp.twovoices) sprintf(buf, "SID voices        " GS_PREV " 2 " GS_NEXT "   (%d.%d speaks, %d.%d synced to it: clearer vowels)", chn / 3 + 1,
                                    chn % 3 + 1, cb / 3 + 1, cb % 3 + 1);
          else sprintf(buf, "SID voices        " GS_PREV " 1 " GS_NEXT "   (2: one more voice of the SID for clearer vowels)");
        }
        break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if (c == 0)
      {
        int fw = w - 26;
        int cur = editstring_cursor(sp.text);
        int ofs = cur >= fw ? cur - fw + 1 : 0;
        char vis[128];
        printblankc(x0 + 21, ry, MKCOL(15, C_BEATROW), fw);
        strncpy(vis, sp.text + ofs, fw);
        vis[fw] = 0;
        printtext(x0 + 21, ry, MKCOL(15, C_BEATROW), vis);
        if (isel) printcursor(x0 + 21 + cur - ofs, ry, 1);
        textofs = ofs;
      }
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hitrow = c;
    }

    // phonemes and target
    {
      int y = y0 + 11;
      char vis[96];
      printtext(x0 + 3, y, MKCOL(14, C_PANEL), "Sounds:");
      strncpy(vis, ok ? phs : err, w - 26);
      vis[w - 26] = 0;
      printtext(x0 + 21, y, MKCOL(ok ? 8 : 10, C_PANEL), vis);
      if ((ok) && ((int)strlen(phs) > w - 26)) printtext(x0 + w - 6, y, MKCOL(8, C_PANEL), "...");
      printtext(x0 + 3, y + 1, MKCOL(14, C_PANEL), "Length:");
      if (ok) sprintf(buf, "%d rows, %.2f seconds", rows, ms / 1000.0);
      else strcpy(buf, "-");
      printtext(x0 + 21, y + 1, MKCOL(8, C_PANEL), buf);
      printtext(x0 + 3, y + 2, MKCOL(14, C_PANEL), "Target:");
      sprintf(buf, "Channel %d.%d (SID %d voice %d), pattern %02X from row %02X", chn / 3 + 1, chn % 3 + 1, chn / 3 + 1, chn % 3 + 1, patt, row);
      printtext(x0 + 21, y + 2, MKCOL(8, C_PANEL), buf);
      printtext(x0 + 3, y + 3, MKCOL(14, C_PANEL), "Filter:");
      if (sp.twovoices) sprintf(buf, "Only the speaking voice. %d.%d is used too (from row %02X).", chn / 3 + 1, (chn % 3 + 1) % 3 + 1, row);
      else sprintf(buf, "Only this voice. Keep the other voices of SID %d unfiltered.", chn / 3 + 1);
      printtext(x0 + 21, y + 3, MKCOL(8, C_PANEL), buf);
      printtext(x0 + 3, y + 5, MKCOL(3, C_PANEL), "English words, numbers and , . ? ! pauses. Exact sounds between slashes:");
      printtext(x0 + 3, y + 6, MKCOL(3, C_PANEL), "/HH AH0 L OW1 | W ER1 L D/  (CMU phonemes, stress 0-2, | = word gap)");
      if (msg[0]) printtext(x0 + 3, y + 8, MKCOL(msgcol, C_PANEL), msg);
    }

    for (c = 0; c < 4; c++)
    {
      int isel = (sel == 8 + c);
      printtext(btnx[c], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), btntext[c]);
      if ((mousey == y0 + h - 2) && (mousex >= btnx[c]) && (mousex < btnx[c] + (int)strlen(btntext[c]))) hitbtn = c;
    }
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL), "UP/DOWN select, LEFT/RIGHT change, F1 preview, ENTER insert, F2 save listing, ESC close");

    if (restoresel >= 0)
    {
      // a clicked button (or F1) did its job; the keyboard focus stays where it was
      sel = restoresel;
      restoresel = -1;
    }
    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      endpreview();
      return;
    }

    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn >= 0)
      {
        restoresel = sel;
        sel = 8 + hitbtn;
        rawkey = KEY_ENTER;
        key = 0;
      }
      else if (hitrow >= 0)
      {
        sel = hitrow;
        key = 0;
        rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        if (hitrow == 0)
        {
          rawkey = 0;
          if (mousex >= x0 + 21) editstring_click(sp.text, mousex - x0 - 21 + textofs);
        }
        if (hitrow == 6) rawkey = KEY_SPACE;
        if (ui_arrowat(mousex, mousey) < 0) rawkey = KEY_LEFT;
        if (ui_arrowat(mousex, mousey) > 0) rawkey = KEY_RIGHT;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h))
      {
        endpreview();
        break;
      }
    }

    // typing into the text field
    if ((sel == 0) && (rawkey != KEY_ENTER) && (rawkey != KEY_ESC) && (rawkey != KEY_UP) && (rawkey != KEY_DOWN) && (rawkey != KEY_TAB) &&
        (rawkey != KEY_F1) && (rawkey != KEY_F2) && ((key >= 32) || (key == 8) || (rawkey == KEY_BACKSPACE) || (rawkey == KEY_LEFT) || (rawkey == KEY_RIGHT) ||
        (rawkey == KEY_HOME) || (rawkey == KEY_END) || (rawkey == KEY_DEL) || (rawkey == KEY_V) || (rawkey == KEY_C)))
    {
      editstring(sp.text, SPEECH_MAXTEXT);
      continue;
    }

    if (rawkey == KEY_F1) { if (restoresel < 0) restoresel = sel; sel = 8; rawkey = KEY_ENTER; }
    if (rawkey == KEY_F2) { if (restoresel < 0) restoresel = sel; sel = 10; rawkey = KEY_ENTER; }

    switch (rawkey)
    {
      case KEY_ESC:
      endpreview();
      goto done;

      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_DOWN:
      case KEY_TAB:
      if (sel < 11) sel++;
      break;

      case KEY_BACKSPACE:
      if (sel == 0)
      {
        int len = strlen(sp.text);
        if (len) sp.text[len - 1] = 0;
      }
      break;

      case KEY_DEL:
      if (sel == 0) sp.text[0] = 0;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      case KEY_SPACE:
      {
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        switch (sel)
        {
          case 1:
          sp.voice = (sp.voice + dir + speech_numvoices()) % speech_numvoices();
          sp.basenote = speech_voicenote(sp.voice);
          break;
          case 2:
          sp.basenote += dir * (shiftpressed ? 12 : 1);
          if (sp.basenote < 12) sp.basenote = 12;
          if (sp.basenote > 72) sp.basenote = 72;
          break;
          case 3:
          sp.speed += dir * 10;
          if (sp.speed < 50) sp.speed = 50;
          if (sp.speed > 250) sp.speed = 250;
          break;
          case 4: sp.melody = (sp.melody + dir + speech_nummelodies()) % speech_nummelodies(); break;
          case 5:
          // Auto, F02 ... F10
          songspeed += dir;
          if ((songspeed == 1) || (songspeed == 2)) songspeed = dir > 0 ? 3 : 0;
          if (songspeed < 0) songspeed = 16;
          if (songspeed > 16) songspeed = 0;
          break;
          case 6: sp.settempo ^= 1; break;
          case 7: sp.twovoices ^= 1; break;
          default:
          if ((sel >= 8) && (rawkey != KEY_SPACE))
          {
            sel += dir;
            if (sel < 8) sel = 8;
            if (sel > 11) sel = 11;
          }
          break;
        }
        if (rawkey != KEY_SPACE || sel < 8) break;
      }
      // SPACE on a button = ENTER
      // fallthrough

      case KEY_ENTER:
      {
        int act = sel >= 8 ? sel : 9;
        char e2[160] = "";
        if (act == 11)
        {
          endpreview();
          goto done;
        }
        if (act == 8)
        {
          if (generate(0, chn, row, e2, sizeof e2))
          {
            initsongpos(editorInfo.esnum, PLAY_PATTERN, row);
            snprintf(msg, sizeof msg, "Playing the pattern from row %02X (preview, the song is not changed)", row);
            msgcol = 13;
          }
          else
          {
            snprintf(msg, sizeof msg, "%s", e2);
            msgcol = 10;
          }
        }
        if (act == 10)
        {
          int r;
          if (!generate(0, chn, row, e2, sizeof e2))
          {
            snprintf(msg, sizeof msg, "%s", e2);
            msgcol = 10;
            break;
          }
          endpreview();
          r = savelisting();
          if (r > 0) snprintf(msg, sizeof msg, "Listing saved as %s", listname), msgcol = 13;
          else if (r < 0) snprintf(msg, sizeof msg, "Could not write %s", listname), msgcol = 10;
          key = rawkey = 0;
        }
        if (act == 9)
        {
          void *ed;
          int r;
          endpreview();
          ed = undoinstrbegin();
          undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
          r = generate(1, chn, row, e2, sizeof e2);
          undoinstrend(ed);
          if (!r)
          {
            snprintf(msg, sizeof msg, "%s", e2);
            msgcol = 10;
            break;
          }
          songmodified = 1;
          {
            char st[120];
            if (sp.twovoices) snprintf(st, sizeof st, "Speech inserted on channels %d.%d + %d.%d, rows %02X-%02X (CTRL+K)", chn / 3 + 1, chn % 3 + 1,
                                       chn / 3 + 1, (chn % 3 + 1) % 3 + 1, row, row + r - 1);
            else snprintf(st, sizeof st, "Speech inserted on channel %d.%d, rows %02X-%02X (CTRL+K)", chn / 3 + 1, chn % 3 + 1, row, row + r - 1);
            setstatusmessage(st);
          }
          goto done;
        }
      }
      break;
    }
  }
done:
  key = 0;
  rawkey = 0;
  printmainscreen();
}

// C64 GrooveGoat - project folder at the start
//
// At the start the editor asks for the working folder of the project (songs,
// instruments and exports are loaded and saved there, the automatic backup is
// written there) and for the automatic backup: on / off and the interval.

#include <sys/stat.h>
#include "goattrk2.h"
#include "gres.h"
#include "gdisplay.h"
#include "gproject.h"

extern char songfilename[MAX_FILENAME];
extern char songpath[MAX_PATHNAME];
extern char instrpath[MAX_PATHNAME];
extern char packedpath[MAX_PATHNAME];

static int isdir(const char *p)
{
  struct stat st;
  return (!stat(p, &st)) && S_ISDIR(st.st_mode);
}

static int makedirs(const char *path)
{
  char tmp[MAX_PATHNAME];
  int c;
  snprintf(tmp, sizeof tmp, "%s", path);
  for (c = 1; tmp[c]; c++)
  {
    if ((tmp[c] == '/') || (tmp[c] == '\\'))
    {
      char k = tmp[c];
      tmp[c] = 0;
      if ((tmp[c - 1] != ':') && (!isdir(tmp)))
#ifdef __WIN32__
        mkdir(tmp);
#else
        mkdir(tmp, 0755);
#endif
      tmp[c] = k;
    }
  }
  if (!isdir(tmp))
#ifdef __WIN32__
    mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
  return isdir(tmp);
}

// change into the project folder; a song from the command line keeps its path
static int useproject(void)
{
  char cwd[MAX_PATHNAME];
  if (!projectdir[0]) return 1;
  if (!isdir(projectdir) && !makedirs(projectdir)) return 0;
  if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
  if ((songfilename[0]) && (songfilename[0] != '/') && (songfilename[0] != '\\') && (songfilename[1] != ':') && (cwd[0]))
  {
    char full[MAX_PATHNAME + MAX_FILENAME];
    snprintf(full, sizeof full, "%s/%s", cwd, songfilename);
    if (strlen(full) < MAX_FILENAME) strcpy(songfilename, full);
  }
  if (chdir(projectdir)) return 0;
  if (!getcwd(projectdir, MAX_PATHNAME)) return 0;
  strcpy(songpath, projectdir);
  strcpy(instrpath, projectdir);
  strcpy(packedpath, projectdir);
  return 1;
}

void projectstart(int interactive)
{
  int w = 100, h = 18;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 2;
  int sel = 0, ofs = 0;
  char msg[200] = "";
  static char browsename[MAX_FILENAME] = "";
  enum { S_FOLDER = 0, S_BROWSE, S_AUTO, S_MIN, S_ASK, S_START, S_CURRENT, S_NUM };

  if ((!interactive) || (!askproject))
  {
    if (!useproject()) projectdir[0] = 0;
    return;
  }
  if (!projectdir[0])
  {
    if (!getcwd(projectdir, MAX_PATHNAME)) projectdir[0] = 0;
  }
  editstring_click(projectdir, MAX_PATHNAME);
  key = rawkey = 0;

  for (;;)
  {
    char buf[300];
    int c, hit = -1, fw = w - 18, cur, fx = x0 + 14;
    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "Project folder");
    printtext(x0 + 3, y0 + 2, MKCOL(14, C_PANEL), "Where do you work today? Songs, instruments, exports and the backup go there.");

    // folder field
    cur = editstring_cursor(projectdir);
    if (cur < ofs) ofs = cur;
    if (cur >= ofs + fw) ofs = cur - fw + 1;
    printtext(x0 + 3, y0 + 4, MKCOL(sel == S_FOLDER ? 15 : 8, C_PANEL), "Folder");
    printblankc(fx, y0 + 4, MKCOL(15, C_BEATROW), fw);
    snprintf(buf, sizeof buf, "%.*s", fw, projectdir + ofs);
    printtext(fx, y0 + 4, MKCOL(15, C_BEATROW), buf);
    if (sel == S_FOLDER) printcursor(fx + cur - ofs, y0 + 4, 1);
    if ((mousey == y0 + 4) && (mousex >= fx) && (mousex < fx + fw)) hit = S_FOLDER;
    snprintf(buf, sizeof buf, "%s", isdir(projectdir) ? "(the folder exists)" : "(new folder - it is created)");
    printtext(fx, y0 + 5, MKCOL(3, C_PANEL), buf);

    for (c = S_BROWSE; c <= S_ASK; c++)
    {
      int ry = y0 + 6 + c, isel = (sel == c), bg = isel ? C_SELECT : C_PANEL;
      switch (c)
      {
        case S_BROWSE: sprintf(buf, "[ Browse... ]  choose the folder in the file selector"); break;
        case S_AUTO: sprintf(buf, "[%c] Automatic backup while you work", autosaveon ? 'x' : ' '); break;
        case S_MIN: sprintf(buf, "    every " GS_PREV " %u " GS_NEXT " minute%s  (one file in the folder, always overwritten: GrooveGoat-autosave.sng)", autosavemin, autosavemin > 1 ? "s" : ""); break;
        case S_ASK: sprintf(buf, "[%c] Ask at every start", askproject ? 'x' : ' '); break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : (c == S_MIN && !autosaveon ? 11 : 8), bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hit = c;
    }
    if (msg[0]) printtext(x0 + 3, y0 + h - 5, MKCOL(10, C_PANEL), msg);
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL), "Click into the folder name to edit it. ENTER start, ESC start in the current folder.");
    {
      const char *bt[2] = {"   Start   ", " Use the current folder "};
      int bx[2] = {x0 + 3, x0 + 18};
      for (c = 0; c < 2; c++)
      {
        int isel = sel == S_START + c;
        printtext(bx[c], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), bt[c]);
        if ((mousey == y0 + h - 2) && (mousex >= bx[c]) && (mousex < bx[c] + (int)strlen(bt[c]))) hit = S_START + c;
      }
    }
    fliptoscreen();
    getkey();
    if (win_quitted) return;

    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit >= 0))
    {
      sel = hit;
      if (hit == S_FOLDER) editstring_click(projectdir, mousex - fx + ofs);
      else
      {
        rawkey = KEY_ENTER;
        if ((hit == S_MIN) && (ui_arrowat(mousex, mousey))) rawkey = ui_arrowat(mousex, mousey) < 0 ? KEY_LEFT : KEY_RIGHT;
      }
      key = 0;
    }
    msg[0] = 0;
    if ((sel == S_FOLDER) && (rawkey != KEY_ENTER) && (rawkey != KEY_ESC) && (rawkey != KEY_UP) && (rawkey != KEY_DOWN) && (rawkey != KEY_TAB))
    {
      editstring(projectdir, MAX_PATHNAME);
      continue;
    }
    switch (rawkey)
    {
      case KEY_UP: if (sel > 0) sel--; break;
      case KEY_DOWN: case KEY_TAB: if (sel < S_NUM - 1) sel++; break;
      case KEY_ESC:
      if (!getcwd(projectdir, MAX_PATHNAME)) projectdir[0] = 0;
      useproject();
      key = rawkey = 0;
      return;
      case KEY_LEFT:
      case KEY_RIGHT:
      case KEY_SPACE:
      case KEY_ENTER:
      {
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        if ((rawkey == KEY_ENTER) && ((sel == S_FOLDER) || (sel == S_MIN))) sel = S_START;
        switch (sel)
        {
          case S_BROWSE:
          if (rawkey == KEY_ENTER || rawkey == KEY_SPACE)
          {
            char path[MAX_PATHNAME];
            snprintf(path, sizeof path, "%s", isdir(projectdir) ? projectdir : ".");
            if (fileselector(browsename, path, "*", "CHOOSE THE PROJECT FOLDER: open it, then pick any file (or ESC)", 0))
            {
              if (!getcwd(projectdir, MAX_PATHNAME)) projectdir[0] = 0;
            }
            else snprintf(projectdir, MAX_PATHNAME, "%s", path);
            editstring_click(projectdir, MAX_PATHNAME);
          }
          break;
          case S_AUTO: autosaveon ^= 1; break;
          case S_MIN:
          {
            int m = (int)autosavemin + dir;
            if (m < 1) m = 1;
            if (m > 60) m = 60;
            autosavemin = m;
          }
          break;
          case S_ASK: askproject ^= 1; break;
          case S_START:
          if (rawkey != KEY_ENTER && rawkey != KEY_SPACE) break;
          if (!useproject())
          {
            snprintf(msg, sizeof msg, "The folder cannot be used or created: %.70s", projectdir);
            break;
          }
          key = rawkey = 0;
          return;
          case S_CURRENT:
          if (rawkey != KEY_ENTER && rawkey != KEY_SPACE) break;
          if (!getcwd(projectdir, MAX_PATHNAME)) projectdir[0] = 0;
          useproject();
          key = rawkey = 0;
          return;
        }
      }
      break;
    }
  }
}

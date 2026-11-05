//
// C64 GrooveGoat file selector
// (based on the GoatTracker v2 file selector)
//
// - multiple filters separated by ';' (e.g. "*.sng;*.ggt")
// - instrument files show their internal instrument name
// - instrument mode: SPACE / click on the play icon previews the instrument
// - shortcuts to the home folder and to the bundled instrument library
//

#define GFILE_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include "goattrk2.h"
#include "gres.h"

DIRENTRY direntry[MAX_DIRFILES];
static char direntryinfo[MAX_DIRFILES][24];

#define FS_W 104
#define FS_X ((MAX_COLUMNS - FS_W) / 2)
#define FS_Y 3
#define FS_LIST_Y (FS_Y + 3)
#define FS_H (VISIBLEFILES + 10)

void initpaths(void)
{
  int c;

  for (c = 0; c < MAX_DIRFILES; c++)
     direntry[c].name = NULL;

  memset(loadedsongfilename, 0, sizeof loadedsongfilename);
  memset(songfilename, 0, sizeof songfilename);
  memset(instrfilename, 0, sizeof instrfilename);
  memset(songpath, 0, sizeof songpath);
  memset(instrpath, 0, sizeof instrpath);
  memset(packedpath, 0, sizeof packedpath);
  strcpy(songfilter, "*.sng;*.ggt");
  strcpy(instrfilter, "*.ins");

  getcwd(songpath, MAX_PATHNAME);
  strcpy(packedpath, songpath);
  // Instruments: start in the bundled library if it exists
  {
    struct stat st;
    if ((!stat(instrlibpath, &st)) && (st.st_mode & S_IFDIR))
      strcpy(instrpath, instrlibpath);
    else
      strcpy(instrpath, songpath);
  }
}

static int matchfilter(const char *filename, const char *filter)
{
  char lname[MAX_PATHNAME];
  char f[MAX_FILENAME];
  char *tok;
  int c;

  if ((!filter[0]) || (!strcmp(filter, "*")) || (!strcmp(filter, "*.*"))) return 1;
  strncpy(lname, filename, MAX_PATHNAME - 1);
  lname[MAX_PATHNAME - 1] = 0;
  for (c = 0; lname[c]; c++) lname[c] = tolower((unsigned char)lname[c]);
  strncpy(f, filter, MAX_FILENAME - 1);
  f[MAX_FILENAME - 1] = 0;
  for (c = 0; f[c]; c++) f[c] = tolower((unsigned char)f[c]);

  tok = f;
  while (tok && *tok)
  {
    char *p;
    char *next = tok;
    while ((*next) && (*next != ';') && (*next != ',') && (*next != ' ')) next++;
    if (*next)
    {
      *next = 0;
      next++;
    }
    else next = NULL;
    if (!*tok)
    {
      tok = next;
      continue;
    }
    p = strchr(tok, '*');
    if (p) p++;
    else p = tok;
    if ((!*p) || (!strcmp(p, ".*"))) return 1;
    // match the end of the name (extension)
    {
      int ln = strlen(lname);
      int lp = strlen(p);
      if ((ln >= lp) && (!strcmp(lname + ln - lp, p))) return 1;
      if (strstr(lname, p)) return 1;
    }
    tok = next;
  }
  return 0;
}

// Read the internal name of a GoatTracker instrument file
static void getinstrumentname(const char *filename, char *out)
{
  FILE *h = fopen(filename, "rb");
  unsigned char buf[4 + 9 + MAX_INSTRNAMELEN];
  out[0] = 0;
  if (!h) return;
  if (fread(buf, sizeof buf, 1, h) == 1)
  {
    if ((!memcmp(buf, "GTI3", 4)) || (!memcmp(buf, "GTI4", 4)) || (!memcmp(buf, "GTI5", 4)))
    {
      int c;
      memcpy(out, buf + 13, MAX_INSTRNAMELEN);
      out[MAX_INSTRNAMELEN - 1] = 0;
      for (c = 0; out[c]; c++)
        if (((unsigned char)out[c] < 32) || ((unsigned char)out[c] > 126)) out[c] = ' ';
    }
  }
  fclose(h);
}

static void freedir(void)
{
  int c;
  for (c = 0; c < MAX_DIRFILES; c++)
  {
    if (direntry[c].name)
    {
      free(direntry[c].name);
      direntry[c].name = NULL;
    }
  }
}

// Instrument preview: back up the current instrument + tables, load the
// file temporarily and play a test note.
// complete copy of instruments, tables and patterns before the preview
static void *previewdata = NULL;
static int previewactive = 0;

static void previewbackup(void)
{
  songdata_free(previewdata);
  previewdata = songdata_save();
  previewactive = 0;
}

static void previewrestore(void)
{
  if (!previewactive) return;
  releasenote(editorInfo.epchn);
  songdata_restore(previewdata);
  previewactive = 0;
}

static void previewplay(const char *name)
{
  char save[MAX_FILENAME];
  if (!editorInfo.einum) return;
  if (isplaying()) stopsong();
  snd_lock();
  releasenote(editorInfo.epchn);
  if (previewactive)
  {
    songdata_restore(previewdata);
  }
  strcpy(save, instrfilename);
  strncpy(instrfilename, name, MAX_FILENAME - 1);
  loadinstrument();
  strcpy(instrfilename, save);
  previewactive = 1;
  preview_note(FIRSTNOTE + editorInfo.epoctave * 12, editorInfo.einum, editorInfo.epchn);
  snd_unlock();
}

int fileselector(char *name, char *path, char *filter, char *title, int filemode)
{
  int c, d, scrrep;
  int color;
  int files;
  int filepos = 0;
  int fileview = 0;
  int lastclick = 0;
  int lastfile = 0;
  int lowest;
  int exitfilesel;
  int instrmode = (filemode & FILESEL_INSTRUMENT) ? 1 : 0;
  int pathofs = 0, filterofs = 0, nameofs = 0;     // scroll positions of the text fields

  DIR *dir;
  struct dirent *de;
  struct stat st;
  #ifdef __WIN32__
  char drivestr[] = "A:\\";
  char driveexists[26];
  #endif
  char tempname[MAX_PATHNAME];
  char buf[200];

  filemode &= 0xff;
  if (instrmode)
  {
    previewbackup();
    preview_begin();
  }

  // Set initial path (if any)
  if (strlen(path)) chdir(path);

  // Scan for all existing drives
  #ifdef __WIN32__
  for (c = 0; c < 26; c++)
  {
    drivestr[0] = 'A'+c;
    if (GetDriveType(drivestr) > 1) driveexists[c] = 1;
    else driveexists[c] = 0;
  }
  #endif

  // Read new directory
  NEWPATH:
  getcwd(path, MAX_PATHNAME);
  files = 0;
  freedir();

  // Process directory
  dir = opendir(".");
  if (dir)
  {
    while ((de = readdir(dir)))
    {
      if (!strcmp(de->d_name, ".")) continue;
      if ((files < MAX_DIRFILES) && (strlen(de->d_name) < MAX_FILENAME))
      {
        if (stat(de->d_name, &st)) continue;
        direntryinfo[files][0] = 0;
        if (st.st_mode & S_IFDIR)
        {
          // hide dot directories except ".."
          if ((de->d_name[0] == '.') && (strcmp(de->d_name, ".."))) continue;
          direntry[files].name = strdup(de->d_name);
          direntry[files].attribute = 1;
          files++;
        }
        else
        {
          if (de->d_name[0] == '.') continue;
          if (matchfilter(de->d_name, filter))
          {
            direntry[files].name = strdup(de->d_name);
            direntry[files].attribute = 0;
            if (instrmode)
              getinstrumentname(de->d_name, direntryinfo[files]);
            else
            {
              if (st.st_size < 10000)
                sprintf(direntryinfo[files], "%6d bytes", (int)st.st_size);
              else
                sprintf(direntryinfo[files], "%6d KB", (int)(st.st_size / 1024));
            }
            files++;
          }
        }
      }
    }
    closedir(dir);
  }
  #ifdef __WIN32__
  // Drive letters
  for (c = 0; c < 26; c++)
  {
    if ((driveexists[c]) && (files < MAX_DIRFILES))
    {
      drivestr[0] = 'A'+c;
      direntry[files].name = strdup(drivestr);
      direntry[files].attribute = 2;
      direntryinfo[files][0] = 0;
      files++;
    }
  }
  #endif

  // Sort: ".." first, then directories, files, drives
  for (c = 0; c < files; c++)
  {
    lowest = c;
    for (d = c+1; d < files; d++)
    {
      int ad = direntry[d].attribute == 1 ? (strcmp(direntry[d].name, "..") ? 1 : 0) : (direntry[d].attribute == 0 ? 2 : 3);
      int al = direntry[lowest].attribute == 1 ? (strcmp(direntry[lowest].name, "..") ? 1 : 0) : (direntry[lowest].attribute == 0 ? 2 : 3);
      if (ad < al) lowest = d;
      else if ((ad == al) && (cmpname(direntry[d].name, direntry[lowest].name) < 0)) lowest = d;
    }
    if (lowest != c)
    {
      DIRENTRY swaptemp = direntry[c];
      char infotemp[24];
      direntry[c] = direntry[lowest];
      direntry[lowest] = swaptemp;
      memcpy(infotemp, direntryinfo[c], 24);
      memcpy(direntryinfo[c], direntryinfo[lowest], 24);
      memcpy(direntryinfo[lowest], infotemp, 24);
    }
  }

  // Search for the current filename
  fileview = 0;
  filepos = 0;
  for (c = 0; c < files; c++)
  {
    if ((!direntry[c].attribute) && (!cmpname(name, direntry[c].name)))
      filepos = c;
  }

  exitfilesel = -1;
  while (exitfilesel < 0)
  {
    int hitbutton = 0;
    int bx;

    if (cursorflashdelay >= 6)
    {
      cursorflashdelay %= 6;
      cursorflash++;
      cursorflash &= 3;
    }

    // Validate filelist view
    if (filepos < fileview) fileview = filepos;
    if (filepos - fileview >= VISIBLEFILES) fileview = filepos - VISIBLEFILES + 1;
    if (fileview < 0) fileview = 0;

    // Draw
    printstatus();
    dlg_drawframe(FS_X, FS_Y, FS_W, FS_H, title);

    // Path line
    printtext(FS_X + 2, FS_Y + 1, MKCOL(7, C_PANEL), "Folder");
    drawtextfield(FS_X + 10, FS_Y + 1, FS_W - 12, path, MKCOL(filemode == 1 ? CEDIT : 15, C_BEATROW), filemode == 1, &pathofs);

    // Column header
    printblankc(FS_X + 1, FS_Y + 2, MKCOL(7, C_PANEL), FS_W - 2);
    printtext(FS_X + 4, FS_Y + 2, MKCOL(7, C_PANEL), "Name");
    printtext(FS_X + 72, FS_Y + 2, MKCOL(7, C_PANEL), instrmode ? "Instrument name" : "Size");

    for (c = 0; c < VISIBLEFILES; c++)
    {
      int y = FS_LIST_Y + c;
      int idx = fileview + c;
      int bg = (c & 1) ? C_PANEL : C_BEATROW;
      printblankc(FS_X + 1, y, MKCOL(8, bg), FS_W - 2);
      if ((idx >= 0) && (idx < files))
      {
        int glyph = 0;
        color = 8;
        switch (direntry[idx].attribute)
        {
          case 0: glyph = instrmode ? GL_NOTE : GL_BULLET; color = 15; break;
          case 1: glyph = GL_FOLDER; color = 14; break;
          case 2: glyph = GL_DISK; color = 11; break;
        }
        if (idx == filepos) bg = C_SELECT;
        printblankc(FS_X + 1, y, MKCOL(8, bg), FS_W - 2);
        printglyph(FS_X + 2, y, MKCOL(color, bg), glyph);
        sprintf(buf, "%-64.64s", direntry[idx].name);
        printtext(FS_X + 4, y, MKCOL(color, bg), buf);
        if (direntry[idx].attribute == 1)
          printtext(FS_X + 72, y, MKCOL(7, bg), strcmp(direntry[idx].name, "..") ? "<folder>" : "<up>");
        else if (direntry[idx].attribute == 2)
          printtext(FS_X + 72, y, MKCOL(7, bg), "<drive>");
        else
          printtext(FS_X + 72, y, MKCOL(instrmode ? 11 : 7, bg), direntryinfo[idx]);
        if ((instrmode) && (!direntry[idx].attribute))
        {
          printglyph(FS_X + FS_W - 4, y, MKCOL(10, bg), GL_PLAY);
          if ((mousey == y) && (mousex == FS_X + FS_W - 4)) hitbutton = 100 + idx;
        }
        if ((!filemode) && (idx == filepos)) printbgx(FS_X + 1, y, C_SELECT, FS_W - 2);
      }
    }
    // Scrollbar
    if (files > VISIBLEFILES)
    {
      int barh = VISIBLEFILES * VISIBLEFILES / files;
      int bary = (VISIBLEFILES - barh) * fileview / (files - VISIBLEFILES);
      if (barh < 1) barh = 1;
      for (c = 0; c < VISIBLEFILES; c++)
        printglyph(FS_X + FS_W - 2, FS_LIST_Y + c, MKCOL((c >= bary) && (c < bary + barh) ? 9 : C_LINE, C_PANEL), GL_FULL);
    }

    // Filter & name
    printtext(FS_X + 2, FS_LIST_Y + VISIBLEFILES + 1, MKCOL(7, C_PANEL), "Filter");
    drawtextfield(FS_X + 10, FS_LIST_Y + VISIBLEFILES + 1, 40, filter, MKCOL(filemode == 2 ? CEDIT : 15, C_BEATROW), filemode == 2, &filterofs);

    printtext(FS_X + 2, FS_LIST_Y + VISIBLEFILES + 2, MKCOL(7, C_PANEL), "Name");
    drawtextfield(FS_X + 10, FS_LIST_Y + VISIBLEFILES + 2, 60, name, MKCOL(filemode == 3 ? CEDIT : 15, C_BEATROW), filemode == 3, &nameofs);

    // Buttons
    bx = FS_X + 2;
    {
      const char *labels[7] = {" Home ", " Library ", " Examples ", " Up ", "  OK  ", " Cancel ", NULL};
      int ids[7] = {1, 2, 6, 3, 4, 5, 0};
      int nb = 6;
      struct stat exst;
      int haveexamples = (!stat(examplespath, &exst)) && (exst.st_mode & S_IFDIR);
      int by = FS_LIST_Y + VISIBLEFILES + 4;
      for (c = 0; c < nb; c++)
      {
        int w = strlen(labels[c]);
        int x = bx;
        if (ids[c] == 4) x = FS_X + FS_W - 22;
        if (ids[c] == 5) x = FS_X + FS_W - 12;
        if ((ids[c] == 2) && (!instrmode)) continue;
        if ((ids[c] == 6) && ((instrmode) || (!haveexamples))) continue;
        printtext(x, by, MKCOL(C_BUTTONTEXT, (mousey == by) && (mousex >= x) && (mousex < x + w) ? C_SELECT : C_BUTTON), labels[c]);
        if ((mousey == by) && (mousex >= x) && (mousex < x + w)) hitbutton = ids[c];
        if ((ids[c] < 4) || (ids[c] == 6)) bx += w + 2;
      }
      if (instrmode)
        printtext(bx + 2, by, MKCOL(3, C_PANEL), "SPACE = preview, ENTER = load");
      else
        printtext(bx + 2, by, MKCOL(3, C_PANEL), "TAB = path/filter/name, BACKSPACE = parent folder");
    }

    fliptoscreen();
    getkey();
    if (lastclick) lastclick--;

    if (win_quitted)
    {
      exitfilesel = 0;
      break;
    }

    // Mouse wheel
    if (mousewheel)
    {
      fileview -= mousewheel * 3;
      if (fileview > files - VISIBLEFILES) fileview = files - VISIBLEFILES;
      if (fileview < 0) fileview = 0;
      if (filepos < fileview) filepos = fileview;
      if (filepos >= fileview + VISIBLEFILES) filepos = fileview + VISIBLEFILES - 1;
      if (filepos >= files) filepos = files - 1;
      if ((filepos >= 0) && (!direntry[filepos].attribute)) strcpy(name, direntry[filepos].name);
    }

    if ((mouseb) && (!prevmouseb))
    {
      if (hitbutton >= 100)
      {
        filepos = hitbutton - 100;
        strcpy(name, direntry[filepos].name);
        previewplay(name);
      }
      else if (hitbutton)
      {
        switch (hitbutton)
        {
          case 1:
          {
            char *home = getenv("HOME");
            #ifdef __WIN32__
            if (!home) home = getenv("USERPROFILE");
            #endif
            if (home) chdir(home);
            goto NEWPATH;
          }
          case 2:
          chdir(instrlibpath);
          goto NEWPATH;
          case 6:
          chdir(examplespath);
          goto NEWPATH;
          case 3:
          chdir("..");
          goto NEWPATH;
          case 4:
          if ((filemode == 0) && (files) && (direntry[filepos].attribute)) goto ENTERFILE;
          filemode = 3;
          goto ENTERFILE;
          case 5:
          exitfilesel = 0;
          break;
        }
      }
      // Cancel (click outside)
      else if ((mousey < FS_Y) || (mousey >= FS_Y + FS_H) || (mousex < FS_X) || (mousex >= FS_X + FS_W))
      {
        exitfilesel = 0;
      }
      // Select path / filter / name field
      else if (mousey == FS_Y + 1)
      {
        filemode = 1;
        editstring_click(path, mousex - FS_X - 10 + pathofs);
      }
      else if (mousey == FS_LIST_Y + VISIBLEFILES + 1) { filemode = 2; editstring_click(filter, mousex - FS_X - 10 + filterofs); }
      else if (mousey == FS_LIST_Y + VISIBLEFILES + 2) { filemode = 3; editstring_click(name, mousex - FS_X - 10 + nameofs); }
    }
    if ((mouseb) && (hitbutton < 100) && (!hitbutton))
    {
      // Select file from list
      if ((mousey >= FS_LIST_Y) && (mousey < FS_LIST_Y + VISIBLEFILES) && (mousex > FS_X) && (mousex < FS_X + FS_W - 2))
      {
        int newpos = mousey - FS_LIST_Y + fileview;
        if (newpos < files)
        {
          filemode = 0;
          filepos = newpos;
          if (!direntry[filepos].attribute)
            strcpy(name, direntry[filepos].name);
          if ((!prevmouseb) && (lastclick) && (lastfile == filepos)) goto ENTERFILE;
        }
      }
    }

    if (!filemode)
    {
      if (((key >= '0') && (key <= '9')) || ((key >= 'a') && (key <= 'z')) || ((key >= 'A') && (key <= 'Z')))
      {
        char k = tolower(key);
        int oldfilepos = filepos;

        for (filepos = oldfilepos + 1; filepos < files; filepos++)
          if (tolower(direntry[filepos].name[0]) == k) break;
        if (filepos >= files)
        {
          for (filepos = 0; filepos < oldfilepos; filepos++)
             if (tolower(direntry[filepos].name[0]) == k) break;
        }

        if ((filepos < files) && (!direntry[filepos].attribute)) strcpy(name, direntry[filepos].name);
      }
    }

    switch(rawkey)
    {
      case KEY_ESC:
      exitfilesel = 0;
      break;

      case KEY_SPACE:
      if ((instrmode) && (!filemode) && (files) && (!direntry[filepos].attribute))
      {
        if (shiftpressed) releasenote(editorInfo.epchn);
        else previewplay(direntry[filepos].name);
      }
      break;

      case KEY_BACKSPACE:
      if (!filemode)
      {
        chdir("..");
        goto NEWPATH;
      }
      break;

      case KEY_HOME:
      if ((!filemode) && (files))
      {
        filepos = 0;
        if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
      }
      break;

      case KEY_END:
      if ((!filemode) && (files))
      {
        filepos = files-1;
        if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
      }
      break;

      case KEY_PGUP:
      for (scrrep = VISIBLEFILES - 1; scrrep; scrrep--)
      {
        if ((!filemode) && (filepos > 0))
        {
          filepos--;
          if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
        }
      }
      break;

      case KEY_UP:
      if ((!filemode) && (filepos > 0))
      {
        filepos--;
        if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
      }
      break;

      case KEY_PGDN:
      for (scrrep = VISIBLEFILES - 1; scrrep; scrrep--)
      {
        if ((!filemode) && (filepos < files-1))
        {
          filepos++;
          if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
        }
      }
      break;

      case KEY_DOWN:
      if ((!filemode) && (filepos < files-1))
      {
        filepos++;
        if (!direntry[filepos].attribute) strcpy(name, direntry[filepos].name);
      }
      break;

      case KEY_TAB:
      if (!shiftpressed)
      {
        filemode++;
        if (filemode > 3) filemode = 0;
      }
      else
      {
        filemode--;
        if (filemode < 0) filemode = 3;
      }
      break;

      case KEY_ENTER:
      ENTERFILE:
      switch(filemode)
      {
        case 0:
        if (!files) break;
        switch (direntry[filepos].attribute)
        {
          case 0:
          strcpy(name, direntry[filepos].name);
          exitfilesel = 1;
          break;

          case 1:
          chdir(direntry[filepos].name);
          goto NEWPATH;

          case 2:
          strcpy(tempname, direntry[filepos].name);
          if (strlen(tempname))
          {
            if (tempname[strlen(tempname)-1] != '\\')
              strcat(tempname, "\\");
          }
          chdir(tempname);
          goto NEWPATH;
        }
        break;

        case 1:
        chdir(path);
        // fallthrough
        case 2:
        filemode = 0;
        goto NEWPATH;

        case 3:
        if (strlen(name)) exitfilesel = 1;
        break;
      }
      break;
    }

    switch(filemode)
    {
      case 1:
      editstring(path, MAX_PATHNAME);
      break;

      case 2:
      editstring(filter, MAX_FILENAME);
      break;

      case 3:
      editstring(name, MAX_FILENAME);
      break;
    }

    if ((mouseb) && (!prevmouseb))
    {
      lastclick = DOUBLECLICKDELAY;
      lastfile = filepos;
    }
  }

  freedir();
  if (instrmode)
  {
    previewrestore();
    preview_end();
    songdata_free(previewdata);
    previewdata = NULL;
  }

  // Restore screen & exit
  key = 0;
  rawkey = 0;
  printmainscreen();
  return exitfilesel;
}

// Text editing with a cursor (all name / text fields): the cursor belongs to
// the buffer edited last; a new buffer starts with the cursor at its end.
// LEFT / RIGHT / HOME / END move, BACKSPACE / DEL delete, typing inserts,
// CTRL+V pastes from the clipboard, CTRL+C copies the field. Word by word:
// CTRL+LEFT/RIGHT (ALT on a Mac), start / end: CMD+LEFT/RIGHT on a Mac.
static char *edbuf = NULL;
static int edpos = 0;

int editstring_cursor(char *buffer)
{
  int len = strlen(buffer);
  if (buffer != edbuf)
  {
    edbuf = buffer;
    edpos = len;
  }
  if (edpos > len) edpos = len;
  if (edpos < 0) edpos = 0;
  return edpos;
}

void editstring_click(char *buffer, int column)
{
  int len = strlen(buffer);
  edbuf = buffer;
  edpos = column < 0 ? 0 : (column > len ? len : column);
}

void editstring(char *buffer, int maxlength)
{
  int len = strlen(buffer), pos = editstring_cursor(buffer);

  if (((ctrlpressed) || (metapressed)) && ((rawkey == KEY_V) || (rawkey == KEY_C)))
  {
    if (rawkey == KEY_C) SDL_SetClipboardText(buffer);
    else
    {
      char *clip = SDL_GetClipboardText();
      if (clip)
      {
        const char *c;
        for (c = clip; *c && (len < maxlength - 1); c++)
        {
          unsigned char ch = *c;
          if ((ch < 32) || (ch == 127)) continue;
          memmove(buffer + pos + 1, buffer + pos, len - pos + 1);
          buffer[pos++] = ch;
          len++;
        }
        SDL_free(clip);
      }
      edpos = pos;
    }
    key = 0;
    rawkey = 0;
    return;
  }
  if ((rawkey == KEY_LEFT) || (rawkey == KEY_RIGHT))
  {
    int right = rawkey == KEY_RIGHT;
    if (metapressed) edpos = right ? len : 0;                   // CMD: start / end of the text
    else if ((ctrlpressed) || (altpressed))                     // word by word
    {
      int p = pos;
#define WORDCH(ch) (isalnum((unsigned char)(ch)) || ((unsigned char)(ch) >= 128))
      if (right)
      {
        while ((p < len) && (WORDCH(buffer[p]))) p++;
        while ((p < len) && (!WORDCH(buffer[p]))) p++;
      }
      else
      {
        while ((p > 0) && (!WORDCH(buffer[p - 1]))) p--;
        while ((p > 0) && (WORDCH(buffer[p - 1]))) p--;
      }
#undef WORDCH
      edpos = p;
    }
    else if (right) { if (pos < len) edpos = pos + 1; }
    else if (pos > 0) edpos = pos - 1;
    key = 0;
    rawkey = 0;
    return;
  }
  switch (rawkey)
  {
    case KEY_HOME: edpos = 0; return;
    case KEY_END: edpos = len; return;
    case KEY_DEL:
    if (pos < len) memmove(buffer + pos, buffer + pos + 1, len - pos);
    return;
  }
  if ((key) || (rawkey == KEY_BACKSPACE))
  {
    if ((key == 8) || (rawkey == KEY_BACKSPACE))
    {
      if (pos > 0)
      {
        memmove(buffer + pos - 1, buffer + pos, len - pos + 1);
        edpos = pos - 1;
      }
      return;
    }
    if ((key >= 32) && (key < 256) && (key != 127))
    {
      if (len < maxlength-1)
      {
        memmove(buffer + pos + 1, buffer + pos, len - pos + 1);
        buffer[pos] = key;
        edpos = pos + 1;
      }
    }
  }
}

// A text field of a given width that scrolls with the cursor: prints the
// visible part (and the cursor when active). *ofs is the scroll position of
// the field; a click maps to mousex - x + *ofs (editstring_click).
void drawtextfield(int x, int y, int width, char *buffer, int color, int active, int *ofs)
{
  char buf[MAX_PATHNAME + 2];
  int len = strlen(buffer), cur;
  if (width < 2) width = 2;
  if (active)
  {
    cur = editstring_cursor(buffer);
    if (cur < *ofs) *ofs = cur;
    if (cur >= *ofs + width) *ofs = cur - width + 1;
  }
  else if (len >= width) *ofs = len - width + 1;               // the end of a long text
  else *ofs = 0;
  if (*ofs > len) *ofs = len;
  if (*ofs < 0) *ofs = 0;
  snprintf(buf, sizeof buf, "%-*.*s", width, width, buffer + *ofs);
  if ((*ofs > 0) && (!active)) buf[0] = buf[1] = buf[2] = '.';
  printtext(x, y, color, buf);
  if (active) printcursor(x + editstring_cursor(buffer) - *ofs, y, 1);
}

int cmpname(char *string1, char *string2)
{
  for (;;)
  {
    unsigned char char1 = tolower(*string1++);
    unsigned char char2 = tolower(*string2++);
    if (char1 < char2) return -1;
    if (char1 > char2) return 1;
    if ((!char1) || (!char2)) return 0;
  }
}

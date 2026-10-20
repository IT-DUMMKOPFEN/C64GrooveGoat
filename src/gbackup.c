//
// C64 GrooveGoat - backup copies
//
//  - Saving a song keeps the previous version of the file as NAME.bak
//  - Every 2 minutes an unsaved song is written to the autosave folder in
//    the configuration folder (the newest 10 files are kept)
//  - A session file remembers the newest autosave. If the program was not
//    closed properly, the next start offers to restore it.
//

#include <errno.h>
#include <time.h>
#include "goattrk2.h"
#include "gbackup.h"

#ifdef __WIN32__
#include <windows.h>
#else
#include <signal.h>
#endif

#define AUTOSAVE_INTERVAL (2 * 60 * 1000)
#define AUTOSAVE_KEEP 10

static char autosavedir[MAX_PATHNAME];
static char sessionfile[MAX_PATHNAME];
static char songfullpath[MAX_PATHNAME];
static char lastautosave[MAX_PATHNAME];
static Uint32 lastsave = 0;
static int initialized = 0;

static int mypid(void)
{
#ifdef __WIN32__
  return (int)GetCurrentProcessId();
#else
  return (int)getpid();
#endif
}

static int processalive(int pid)
{
  if (pid <= 0) return 0;
  if (pid == mypid()) return 1;
#ifdef __WIN32__
  {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    DWORD r;
    if (!h) return 0;
    r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;
  }
#else
  return (kill(pid, 0) == 0) || (errno == EPERM);
#endif
}

static void makedir(const char *path)
{
#ifdef __WIN32__
  mkdir(path);
#else
  mkdir(path, S_IRWXU);
#endif
}

static void writesession(void)
{
  FILE *h;
  if (!initialized) return;
  h = fopen(sessionfile, "wt");
  if (!h) return;
  fprintf(h, "C64 GrooveGoat session\n%s\n%s\n", songfullpath, lastautosave);
  fclose(h);
}

// Copy the current file to NAME.bak before it is overwritten
int backup_copybeforesave(const char *filename)
{
  FILE *in, *out;
  char bak[MAX_PATHNAME];
  char buf[16384];
  size_t n;
  int ok = 1;

  in = fopen(filename, "rb");
  if (!in) return 0;
  snprintf(bak, sizeof bak, "%s.bak", filename);
  out = fopen(bak, "wb");
  if (!out)
  {
    fclose(in);
    return 0;
  }
  while ((n = fread(buf, 1, sizeof buf, in)) > 0)
    if (fwrite(buf, 1, n, out) != n) ok = 0;
  fclose(in);
  if (fclose(out)) ok = 0;
  return ok;
}

// Called after the song was saved, loaded or cleared: nothing to recover
void backup_saved(void)
{
  char cwd[MAX_PATHNAME];
  lastsave = SDL_GetTicks();
  lastautosave[0] = 0;
  songfullpath[0] = 0;
  if (loadedsongfilename[0])
  {
    if ((loadedsongfilename[0] == '/') || (loadedsongfilename[0] == '\\') || (loadedsongfilename[1] == ':'))
      snprintf(songfullpath, sizeof songfullpath, "%s", loadedsongfilename);
    else if (getcwd(cwd, sizeof cwd))
    {
#ifdef __WIN32__
      snprintf(songfullpath, sizeof songfullpath, "%s\\%s", cwd, loadedsongfilename);
#else
      snprintf(songfullpath, sizeof songfullpath, "%s/%s", cwd, loadedsongfilename);
#endif
    }
  }
  writesession();
}

static int cmpstr(const void *a, const void *b)
{
  return strcmp(*(char * const *)a, *(char * const *)b);
}

// Delete all but the newest autosave files (the names start with a time stamp)
static void pruneautosaves(void)
{
  DIR *d = opendir(autosavedir);
  struct dirent *de;
  char *names[1024];
  int n = 0, c;
  if (!d) return;
  while ((de = readdir(d)) && (n < 1024))
  {
    if (de->d_name[0] == '.') continue;
    if ((strlen(de->d_name) < 16) || (de->d_name[8] != '-')) continue;
    names[n++] = strdup(de->d_name);
  }
  closedir(d);
  qsort(names, n, sizeof(char *), cmpstr);
  for (c = 0; c < n; c++)
  {
    if (c < n - AUTOSAVE_KEEP)
    {
      char p[MAX_PATHNAME];
      snprintf(p, sizeof p, "%s/%s", autosavedir, names[c]);
      remove(p);
    }
    free(names[c]);
  }
}

static void autosave(void)
{
  char base[MAX_PATHNAME], name[MAX_PATHNAME], stamp[32];
  const char *src = loadedsongfilename;
  const char *slash;
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  int ggt = numsids > 2;
  char *dot;
  int c;

  slash = strrchr(src, '/');
  if (!slash) slash = strrchr(src, '\\');
  if (slash) src = slash + 1;
  snprintf(base, sizeof base, "%s", src[0] ? src : (songname[0] ? songname : "untitled"));
  dot = strrchr(base, '.');
  if (dot) *dot = 0;
  for (c = 0; base[c]; c++)
  {
    unsigned char ch = base[c];
    if ((ch < 32) || (strchr("\\/:*?\"<>|", ch))) base[c] = '_';
  }
  strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", tm);
  snprintf(name, sizeof name, "%s/%s_%.60s%s", autosavedir, stamp, base, ggt ? ".ggt" : ".sng");
  if (writesongfile(name, ggt))
  {
    strcpy(lastautosave, name);
    writesession();
    pruneautosaves();
    setstatusmessage("Backup copy saved (automatic backup every 2 minutes)");
  }
}

void backup_tick(void)
{
  Uint32 now;
  if (!initialized) return;
  now = SDL_GetTicks();
  if (!songmodified)
  {
    lastsave = now;
    return;
  }
  if (now - lastsave >= (getenv("GG_AUTOSAVEMS") ? (Uint32)atoi(getenv("GG_AUTOSAVEMS")) : AUTOSAVE_INTERVAL))
  {
    autosave();
    lastsave = now;
  }
}

// Look for sessions of earlier runs that did not end properly
static int findrecovery(char *autosavefile, char *origsong, time_t *when)
{
  char dir[MAX_PATHNAME];
  DIR *d;
  struct dirent *de;
  int found = 0;
  time_t best = 0;

  configpath(dir, "");
  d = opendir(dir[0] ? dir : ".");
  if (!d) return 0;
  while ((de = readdir(d)))
  {
    int pid;
    char p[MAX_PATHNAME];
    FILE *h;
    if (sscanf(de->d_name, "session-%d.txt", &pid) != 1) continue;
    if (processalive(pid)) continue;
    configpath(p, de->d_name);
    h = fopen(p, "rt");
    if (h)
    {
      char l1[MAX_PATHNAME], l2[MAX_PATHNAME], l3[MAX_PATHNAME];
      struct stat st;
      l1[0] = l2[0] = l3[0] = 0;
      if (fgets(l1, sizeof l1, h)) {}
      if (fgets(l2, sizeof l2, h)) {}
      if (fgets(l3, sizeof l3, h)) {}
      fclose(h);
      l2[strcspn(l2, "\r\n")] = 0;
      l3[strcspn(l3, "\r\n")] = 0;
      if ((l3[0]) && (!stat(l3, &st)) && (st.st_mtime >= best))
      {
        best = st.st_mtime;
        strcpy(autosavefile, l3);
        strcpy(origsong, l2);
        found = 1;
      }
    }
    remove(p);
  }
  closedir(d);
  *when = best;
  return found;
}

void backup_init(void)
{
  char file[MAX_PATHNAME], orig[MAX_PATHNAME];
  char name[64];
  time_t when;

  configpath(autosavedir, "autosave");
  makedir(autosavedir);
  snprintf(name, sizeof name, "session-%d.txt", mypid());
  configpath(sessionfile, name);

  if (findrecovery(file, orig, &when))
  {
    char l0[200], l1[300], l2[200];
    const char *lines[4];
    const char *opts[2] = {"Restore", "Ignore"};
    const char *shortname = orig[0] ? orig : "an untitled song";
    char t[64];
    const char *s = strrchr(shortname, '/');
    if (!s) s = strrchr(shortname, '\\');
    if (s) shortname = s + 1;
    strftime(t, sizeof t, "%Y-%m-%d %H:%M", localtime(&when));
    snprintf(l0, sizeof l0, "C64 GrooveGoat was not closed properly last time.");
    snprintf(l1, sizeof l1, "There is an automatic backup of %.60s from %s.", shortname, t);
    snprintf(l2, sizeof l2, "Restore it? (The backups are kept in the autosave folder.)");
    lines[0] = l0;
    lines[1] = l1;
    lines[2] = "";
    lines[3] = l2;
    if (dlg_choose("Restore backup", lines, 4, opts, 2, 0) == 0)
    {
      char save[MAX_FILENAME];
      strcpy(save, songfilename);
      strncpy(songfilename, file, MAX_FILENAME - 1);
      songfilename[MAX_FILENAME - 1] = 0;
      stopsong();
      loadsong();
      do_sound_init();
      undoInitAllAreas();
      // Saving goes to the original file again
      if (orig[0])
      {
        char dir[MAX_PATHNAME];
        const char *b = strrchr(orig, '/');
        if (!b) b = strrchr(orig, '\\');
        if (b)
        {
          snprintf(dir, sizeof dir, "%.*s", (int)(b - orig), orig);
          if (!chdir(dir)) strncpy(songpath, dir, MAX_PATHNAME - 1);
          strncpy(loadedsongfilename, b + 1, MAX_FILENAME - 1);
        }
        else strncpy(loadedsongfilename, orig, MAX_FILENAME - 1);
      }
      else loadedsongfilename[0] = 0;
      strcpy(songfilename, loadedsongfilename[0] ? loadedsongfilename : save);
      songmodified = 1;
      snprintf(statusmessage, sizeof statusmessage, "Restored the backup from %s - save the song to keep it", t);
    }
  }
  initialized = 1;
  backup_saved();
  if (songmodified) lastsave = SDL_GetTicks();
}

void backup_shutdown(void)
{
  if (!initialized) return;
  remove(sessionfile);
  initialized = 0;
}

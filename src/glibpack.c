// C64 GrooveGoat - the instrument library as one file in the macOS app
//
// macOS checks every file of an app bundle when it is started from a
// download (Gatekeeper, app translocation): the more than 5000 instruments
// as single files made the start slow. The macOS app therefore carries the
// library as one tar file (Resources/instruments.tar); at the first start of
// a version it is unpacked into ~/Library/Application Support/C64 GrooveGoat.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include "glibpack.h"

#ifdef __WIN32__
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static void mkdirs(char *path)
{
  char *p;
  for (p = path + 1; *p; p++)
    if (*p == '/')
    {
      *p = 0;
      MKDIR(path);
      *p = '/';
    }
  MKDIR(path);
}

static void rmtree(const char *path)
{
  DIR *d = opendir(path);
  struct dirent *de;
  if (!d)
  {
    remove(path);
    return;
  }
  while ((de = readdir(d)))
  {
    char sub[2048];
    struct stat st;
    if ((!strcmp(de->d_name, ".")) || (!strcmp(de->d_name, ".."))) continue;
    snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
    if ((!stat(sub, &st)) && (S_ISDIR(st.st_mode))) rmtree(sub);
    else remove(sub);
  }
  closedir(d);
  rmdir(path);
}

static long octal(const unsigned char *p, int n)
{
  long v = 0;
  int k;
  for (k = 0; (k < n) && (p[k]); k++)
    if ((p[k] >= '0') && (p[k] <= '7')) v = v * 8 + (p[k] - '0');
  return v;
}

// unpacks tarfile into dir (a plain ustar archive: files and folders)
static int untar(const char *tarfile, const char *dir)
{
  FILE *f = fopen(tarfile, "rb");
  unsigned char h[512];
  char *buf = NULL;
  long cap = 0;
  int ok = 1, files = 0;
  if (!f) return 0;
  while (fread(h, 1, 512, f) == 512)
  {
    char name[512], full[2048];
    long size;
    int type = h[156];
    if (!h[0]) break;                                  // end of the archive
    size = octal(h + 124, 12);
    if ((!memcmp(h + 257, "ustar", 5)) && (h[345]))
      snprintf(name, sizeof name, "%.155s/%.100s", (const char *)h + 345, (const char *)h);
    else
      snprintf(name, sizeof name, "%.100s", (const char *)h);
    if ((strstr(name, "..")) || (name[0] == '/')) type = 'x';   // nothing outside the folder
    snprintf(full, sizeof full, "%s/%s", dir, name);
    if (type == '5')
    {
      size_t l = strlen(full);
      if ((l) && (full[l - 1] == '/')) full[l - 1] = 0;
      mkdirs(full);
    }
    if (size > 0)
    {
      long blocks = (size + 511) / 512 * 512;
      if (blocks > cap)
      {
        cap = blocks;
        buf = (char *)realloc(buf, cap);
      }
      if ((long)fread(buf, 1, blocks, f) != blocks)
      {
        ok = 0;
        break;
      }
      if ((type == '0') || (type == 0))
      {
        FILE *o;
        char *sep = strrchr(full, '/');
        if (sep)
        {
          *sep = 0;
          mkdirs(full);
          *sep = '/';
        }
        o = fopen(full, "wb");
        if ((!o) || ((long)fwrite(buf, 1, size, o) != size)) ok = 0;
        if (o) fclose(o);
        files++;
      }
    }
  }
  free(buf);
  fclose(f);
  return ok && files;
}

int libpack_unpack(const char *tarfile, const char *base, const char *version, char *libpath, int libpathsize)
{
  char dir[2048], tmp[2048], stamp[2048], want[128], have[128] = "";
  struct stat st;
  FILE *f;
  if (stat(tarfile, &st)) return 0;
  snprintf(want, sizeof want, "%s %ld", version, (long)st.st_size);
  snprintf(dir, sizeof dir, "%s/instruments", base);
  snprintf(stamp, sizeof stamp, "%s/.unpacked", dir);
  snprintf(libpath, libpathsize, "%s", dir);
  f = fopen(stamp, "rb");
  if (f)
  {
    if (!fgets(have, sizeof have, f)) have[0] = 0;
    fclose(f);
    have[strcspn(have, "\r\n")] = 0;
    if (!strcmp(have, want)) return 1;                 // unpacked before
  }
  // unpack next to it, then swap (a broken start leaves no half library)
  snprintf(tmp, sizeof tmp, "%s/instruments.new", base);
  {
    char b2[2048];
    snprintf(b2, sizeof b2, "%s", base);
    mkdirs(b2);
  }
  rmtree(tmp);
  MKDIR(tmp);
  if (!untar(tarfile, tmp))
  {
    rmtree(tmp);
    return 0;
  }
  rmtree(dir);
  if (rename(tmp, dir)) return 0;
  f = fopen(stamp, "wb");
  if (f)
  {
    fprintf(f, "%s\n", want);
    fclose(f);
  }
  return 1;
}

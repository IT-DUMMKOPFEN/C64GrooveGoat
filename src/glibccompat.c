//
// C64 GrooveGoat - glibc compatibility for the Linux AppImage
//
// The AppImage is built on a current Linux, but should also run on older
// distributions (glibc 2.35, e.g. Ubuntu 22.04 and Debian 12). Newer glibc
// headers redirect some functions to symbols that only exist since glibc
// 2.38; they are provided here with the older implementations.
//

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

// C23 versions of strtol & co. (they also accept 0b prefixes, which nothing
// in this program needs)
long __isoc23_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
long long __isoc23_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
int __isoc23_vsscanf(const char *s, const char *f, va_list ap) { return vsscanf(s, f, ap); }
int __isoc23_sscanf(const char *s, const char *f, ...)
{
  va_list ap;
  int r;
  va_start(ap, f);
  r = vsscanf(s, f, ap);
  va_end(ap);
  return r;
}
int __isoc23_fscanf(FILE *fp, const char *f, ...)
{
  va_list ap;
  int r;
  va_start(ap, f);
  r = vfscanf(fp, f, ap);
  va_end(ap);
  return r;
}

// fmod got a new symbol version in glibc 2.38; use the old one
__asm__(".symver fmod_glibc225, fmod@GLIBC_2.2.5");
__asm__(".symver fmodf_glibc225, fmodf@GLIBC_2.2.5");
double fmod_glibc225(double x, double y);
float fmodf_glibc225(float x, float y);
double fmod(double x, double y) { return fmod_glibc225(x, y); }
float fmodf(float x, float y) { return fmodf_glibc225(x, y); }

// strlcpy & co. exist in glibc since 2.38 only
size_t strlcpy(char *dst, const char *src, size_t size)
{
  size_t len = strlen(src);
  if (size)
  {
    size_t n = len >= size ? size - 1 : len;
    memcpy(dst, src, n);
    dst[n] = 0;
  }
  return len;
}

size_t strlcat(char *dst, const char *src, size_t size)
{
  size_t dl = strnlen(dst, size);
  if (dl == size) return size + strlen(src);
  return dl + strlcpy(dst + dl, src, size - dl);
}

size_t wcslcpy(wchar_t *dst, const wchar_t *src, size_t size)
{
  size_t len = wcslen(src);
  if (size)
  {
    size_t n = len >= size ? size - 1 : len;
    wmemcpy(dst, src, n);
    dst[n] = 0;
  }
  return len;
}

size_t wcslcat(wchar_t *dst, const wchar_t *src, size_t size)
{
  size_t dl = wcsnlen(dst, size);
  if (dl == size) return size + wcslen(src);
  return dl + wcslcpy(dst + dl, src, size - dl);
}

#endif

// C64 GrooveGoat - the explanations of the dialog buttons (tooltips)
//
// A button is found by the title of its dialog and its label (printable
// characters only, spaces collapsed); "*" stands for every dialog.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gconsole.h"

typedef struct
{
  const char *context, *label, *tip;
} TIP;

static const TIP tips[] =
{
#include "gtips.h"
  {NULL, NULL, NULL}
};

static void normalize(const char *raw, char *out, int size)
{
  int o = 0;
  for (; *raw && (o < size - 1); raw++)
  {
    unsigned char ch = *raw;
    if ((ch < 32) || (ch > 126)) ch = ' ';
    if ((ch == ' ') && ((!o) || (out[o - 1] == ' '))) continue;
    out[o++] = ch;
  }
  while ((o) && (out[o - 1] == ' ')) o--;
  out[o] = 0;
}

static int ctxmatch(const char *pattern, const char *context)
{
  // the start of the title is enough ("Beat" matches "Beat editor - ...")
  return (!strcmp(pattern, "*")) || (!strncmp(context, pattern, strlen(pattern)));
}

const char *tip_lookup(const char *context, const char *label)
{
  int k;
  for (k = 0; tips[k].label; k++)
    if ((strcmp(tips[k].context, "*")) && (ctxmatch(tips[k].context, context)) && (!strcmp(tips[k].label, label))) return tips[k].tip;
  for (k = 0; tips[k].label; k++)
    if ((!strcmp(tips[k].context, "*")) && (!strcmp(tips[k].label, label))) return tips[k].tip;
  return NULL;
}

void tip_dump(const char *context, const char *raw)
{
  static char seen[4096][96];
  static int nseen = 0;
  char label[64], key[96];
  int k;
  FILE *f;
  normalize(raw, label, sizeof label);
  if (!label[0]) return;
  snprintf(key, sizeof key, "%s|%s", context, label);
  for (k = 0; k < nseen; k++)
    if (!strcmp(seen[k], key)) return;
  if (nseen < 4096) snprintf(seen[nseen++], 96, "%s", key);
  f = fopen(getenv("GG_TIPDUMP"), "at");
  if (!f) return;
  fprintf(f, "%s%s\n", tip_lookup(context, label) ? "ok   " : "MISS ", key);
  fclose(f);
}

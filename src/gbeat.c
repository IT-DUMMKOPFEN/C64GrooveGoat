// C64 GrooveGoat - beat editor (CTRL+D)
//
// Drum patterns from genre templates (gbeatcat.c) on the channel of the
// pattern cursor. The template is looped over the whole length of the
// channel's current pattern; missing drum instruments are generated.
//
// One SID voice plays one sound at a time: hits on the same row are resolved
// by priority (crash > snare > clap > kick > toms > rimshot > open hat >
// closed hat).

#include <time.h>
#include "goattrk2.h"
#include "gres.h"
#include "gbeat.h"

// ---------------------------------------------------------------------------
// Template grids
// ---------------------------------------------------------------------------

static const char rolechars[BR_NUM] = {'K', 'S', 'C', 'H', 'O', 'T', 'M', 'L', 'R', 'Y'};
static const int roleprio[BR_NUM] = {6, 8, 7, 0, 1, 5, 4, 3, 2, 9};
// pattern note per role (General MIDI drum numbers: C-3 = 36 kick ...)
static const int rolenote[BR_NUM] = {36, 38, 39, 42, 46, 48, 45, 41, 37, 49};

#define MAXHITS 512

int beat_parse(const char *grid, BEATHIT *hits, int maxhits, int *steps)
{
  int n = 0, len = -1;
  const char *s = grid;
  if (steps) *steps = 0;
  if (!grid) return 0;
  while (*s)
  {
    int role, c, l = 0;
    while (*s == ' ') s++;
    if (!*s) break;
    for (role = 0; role < BR_NUM; role++)
      if (rolechars[role] == *s) break;
    if ((role >= BR_NUM) || (s[1] != ':')) return -1;
    s += 2;
    for (c = 0; s[c] && s[c] != ' '; c++)
    {
      int lev = -1, roll = 0;
      switch (s[c])
      {
        case 'x': lev = BL_NORMAL; break;
        case 'X': lev = BL_ACCENT; break;
        case 'g': lev = BL_SOFT; break;
        case 'r': lev = BL_NORMAL; roll = 1; break;
        case '.': break;
        default: return -1;
      }
      if ((lev >= 0) && (n < maxhits))
      {
        hits[n].pos32 = c * 2;
        hits[n].role = role;
        hits[n].level = lev;
        n++;
        if ((roll) && (n < maxhits))
        {
          hits[n] = hits[n - 1];
          hits[n].pos32++;
          n++;
        }
      }
      l++;
    }
    if ((l != 16) && (l != 32)) return -1;
    if ((len >= 0) && (l != len)) return -1;
    len = l;
    s += c;
  }
  if (steps) *steps = len < 0 ? 16 : len;
  return n;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

typedef struct
{
  int genre, sub, var;
  int kit;            // -1 = the kit of the subgenre
  int rows16;         // rows per 16th note, 0 = auto (from the tempo)
  int swing;          // -1 = template, else 50 ... 75
  int fill;           // 1 = fill in the last bar
  int variation;      // 0 = original, else random seed
  int beatonly;       // preview: mute the other channels
} BEATSET;

static BEATSET bs = {0, 0, 0, -1, 0, -1, 0, 0, 1};
static int assign[BR_NUM] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // library instrument per role, -1 = generated kit sound
static int genslot[BR_NUM][BL_NUM];

static const int swingvalues[] = {-1, 50, 58, 62, 66, 75};
#define NUMSWING 6

static int numgenres(void)
{
  int n = 0;
  while (beatcatalog[n].name) n++;
  return n;
}

static int numsubs(int g)
{
  int n = 0;
  while ((n < BEAT_MAXSUB) && (beatcatalog[g].sub[n].name)) n++;
  return n;
}

static int basevars(int g, int s)
{
  int n = 0;
  while ((n < BEAT_MAXVAR) && (beatcatalog[g].sub[s].var[n].name)) n++;
  return n;
}

// Every subgenre gets its catalog beats plus generated variations of its
// first beats (more hats, open hats, ghost notes, half-time, double-time ...)
typedef struct
{
  char name[64];
  char grid[600];
  int swing;
} XVAR;

#define MAXGEN 24
#define MAXSUBS (BEAT_MAXSUB)
#define MAXXV 48
static XVAR *xv[MAXGEN][MAXSUBS];
static int nxv[MAXGEN][MAXSUBS];

typedef struct { char c[BR_NUM][33]; int steps; } GRID;

static int grid_read(const char *g, GRID *gr)
{
  BEATHIT h[MAXHITS];
  int n = beat_parse(g, h, MAXHITS, &gr->steps), k, r;
  if (n < 0) return 0;
  for (r = 0; r < BR_NUM; r++)
  {
    memset(gr->c[r], '.', 32);
    gr->c[r][32] = 0;
  }
  for (k = 0; k < n; k++)
  {
    char *cell = &gr->c[h[k].role][h[k].pos32 / 2];
    if (h[k].pos32 & 1) *cell = 'r';
    else if (*cell != 'r') *cell = h[k].level == BL_ACCENT ? 'X' : (h[k].level == BL_SOFT ? 'g' : 'x');
  }
  return 1;
}

static void grid_write(const GRID *gr, char *out, int size)
{
  int r;
  out[0] = 0;
  for (r = 0; r < BR_NUM; r++)
  {
    int s, any = 0;
    for (s = 0; s < gr->steps; s++)
      if (gr->c[r][s] != '.') any = 1;
    if (!any) continue;
    snprintf(out + strlen(out), size - strlen(out), "%s%c:%.*s", out[0] ? " " : "", rolechars[r], gr->steps, gr->c[r]);
  }
}

static int has(const GRID *g, int role)
{
  int s;
  for (s = 0; s < g->steps; s++)
    if (g->c[role][s] != '.') return 1;
  return 0;
}

static void setat(GRID *g, int role, int step16, char v)
{
  int s;
  for (s = step16; s < g->steps; s += 16) g->c[role][s] = v;
}

static int backbeat(const GRID *g)
{
  return has(g, BR_SNARE) ? BR_SNARE : (has(g, BR_CLAP) ? BR_CLAP : (has(g, BR_RIM) ? BR_RIM : BR_SNARE));
}

#define NUMXFORMS 13
static const char *xformnames[NUMXFORMS] = {"16th hats", "open hats", "ghost notes", "kick push", "half-time", "double-time",
                                            "sparse", "crash", "hat rolls", "rimshot", "clap", "tom tag", "2 bars + fill"};

// a variation of a grid; 0 = does not apply to this beat
static int xform(int x, GRID *g, const char *fill)
{
  int s, bb = backbeat(g);
  switch (x)
  {
    case 0:
    if (!has(g, BR_CHAT)) return 0;
    for (s = 0; s < g->steps; s++)
      if (g->c[BR_CHAT][s] == '.') g->c[BR_CHAT][s] = 'x';
    return 1;
    case 1:
    for (s = 2; s < 16; s += 4) { setat(g, BR_OHAT, s, 'x'); setat(g, BR_CHAT, s, '.'); }
    return 1;
    case 2:
    if (!has(g, bb)) return 0;
    for (s = 0; s < g->steps; s++)
      if (((s % 16) == 7) || ((s % 16) == 9) || ((s % 16) == 15))
        if (g->c[bb][s] == '.') g->c[bb][s] = 'g';
    return 1;
    case 3:
    setat(g, BR_KICK, 6, 'x');
    setat(g, BR_KICK, 14, 'x');
    return 1;
    case 4:
    for (s = 0; s < g->steps; s++)
    {
      int p = s % 16;
      if ((g->c[BR_KICK][s] != '.') && (p != 0) && (p != 10) && (p != 11)) g->c[BR_KICK][s] = '.';
      if ((g->c[bb][s] == 'x') || (g->c[bb][s] == 'X')) g->c[bb][s] = '.';
    }
    setat(g, BR_KICK, 0, 'x');
    setat(g, bb, 8, 'X');
    return 1;
    case 5:
    for (s = 0; s < g->steps; s++)
    {
      int p = s % 16;
      g->c[BR_KICK][s] = (p % 4 == 0) ? 'x' : '.';
      g->c[bb][s] = (p % 4 == 2) ? 'X' : '.';
    }
    return 1;
    case 6:
    for (s = 0; s < g->steps; s++)
    {
      int p = s % 16, r;
      for (r = 0; r < BR_NUM; r++)
      {
        if ((r == BR_KICK) || (r == bb)) { if (g->c[r][s] == 'g') g->c[r][s] = '.'; continue; }
        if (r == BR_CHAT) { g->c[r][s] = (p % 4 == 0) && has(g, BR_CHAT) ? 'x' : '.'; continue; }
        g->c[r][s] = '.';
      }
    }
    return 1;
    case 7:
    g->c[BR_CRASH][0] = 'x';
    return 1;
    case 8:
    if (!has(g, BR_CHAT)) return 0;
    setat(g, BR_CHAT, 7, 'r');
    setat(g, BR_CHAT, 15, 'r');
    return 1;
    case 9:
    case 10:
    if (!has(g, BR_SNARE)) return 0;
    for (s = 0; s < g->steps; s++)
    {
      char v = g->c[BR_SNARE][s];
      if (v == '.') continue;
      g->c[BR_SNARE][s] = '.';
      if (v != 'g') g->c[x == 9 ? BR_RIM : BR_CLAP][s] = v;
    }
    return 1;
    case 11:
    s = g->steps - 4;
    g->c[bb][s] = '.';
    g->c[BR_TOMH][s] = 'x';
    g->c[BR_TOMM][s + 1] = 'x';
    g->c[BR_TOML][s + 2] = 'x';
    g->c[BR_TOML][s + 3] = 'x';
    return 1;
    case 12:
    {
      GRID f;
      int r;
      if ((g->steps != 16) || (!fill) || (!grid_read(fill, &f))) return 0;
      for (r = 0; r < BR_NUM; r++)
      {
        memcpy(&g->c[r][16], g->c[r], 16);
        memcpy(&g->c[r][24], &f.c[r][8], 8);
      }
      g->steps = 32;
    }
    return 1;
  }
  return 0;
}

static void buildvars(int g, int s)
{
  XVAR *v;
  int n = 0, b, x, nb = basevars(g, s);
  if (xv[g][s]) return;
  v = (XVAR *)calloc(MAXXV, sizeof(XVAR));
  if (!v) return;
  for (b = 0; b < nb; b++)
  {
    snprintf(v[n].name, sizeof v[n].name, "%s", beatcatalog[g].sub[s].var[b].name);
    snprintf(v[n].grid, sizeof v[n].grid, "%s", beatcatalog[g].sub[s].var[b].grid);
    v[n].swing = beatcatalog[g].sub[s].var[b].swing;
    n++;
  }
  for (b = 0; b < nb && b < 2; b++)
    for (x = 0; x < NUMXFORMS && n < MAXXV; x++)
    {
      GRID gr;
      int k, dup = 0;
      if (!grid_read(beatcatalog[g].sub[s].var[b].grid, &gr)) continue;
      if (!xform(x, &gr, beatcatalog[g].sub[s].fill)) continue;
      grid_write(&gr, v[n].grid, sizeof v[n].grid);
      for (k = 0; k < n; k++)
        if (!strcmp(v[k].grid, v[n].grid)) dup = 1;
      if (dup) continue;
      snprintf(v[n].name, sizeof v[n].name, "%s + %s", beatcatalog[g].sub[s].var[b].name, xformnames[x]);
      v[n].swing = beatcatalog[g].sub[s].var[b].swing;
      n++;
    }
  xv[g][s] = v;
  nxv[g][s] = n;
}

static int numvars(int g, int s)
{
  buildvars(g, s);
  return nxv[g][s];
}

static const BEATSUBGENRE *cursub(void) { return &beatcatalog[bs.genre].sub[bs.sub]; }
// Dice: a new beat in the style of the subgenre. For every drum and 16th
// note: how often it plays there in the beats of the subgenre; strong places
// always, rare ones never, the rest by chance. The hi-hat line comes from one
// of the beats (then slightly changed), so it stays a coherent figure.
static XVAR randvar;
static int israndom = 0;

static unsigned rs;
static int rnd(int n) { rs = rs * 1103515245u + 12345u; return (int)((rs >> 16) % (unsigned)n); }

static void randombeat(int g, int s, unsigned seed)
{
  GRID gr, src;
  int cnt[BR_NUM][16], lev[BR_NUM][3][16], nb = basevars(g, s), b, r, st, k, total = 0, hatsrc;
  memset(cnt, 0, sizeof cnt);
  memset(lev, 0, sizeof lev);
  rs = seed * 2654435761u + 7;
  for (b = 0; b < nb; b++)
  {
    if (!grid_read(beatcatalog[g].sub[s].var[b].grid, &src)) continue;
    total++;
    for (r = 0; r < BR_NUM; r++)
      for (st = 0; st < src.steps; st++)
      {
        char c = src.c[r][st];
        if (c == '.') continue;
        cnt[r][st % 16]++;
        lev[r][c == 'g' ? 0 : (c == 'X' ? 2 : 1)][st % 16]++;
      }
  }
  if (!total) return;
  // the beats of two bars count twice per step: normalise to "per bar"
  memset(&gr, 0, sizeof gr);
  for (r = 0; r < BR_NUM; r++) memset(gr.c[r], '.', 32);
  gr.steps = 32;
  hatsrc = rnd(nb);
  grid_read(beatcatalog[g].sub[s].var[hatsrc].grid, &src);
  for (r = 0; r < BR_NUM; r++)
  {
    int hat = (r == BR_CHAT) || (r == BR_OHAT);
    for (st = 0; st < 16; st++)
    {
      int p = cnt[r][st] * 100 / total, hit;
      if (p > 100) p = 100;
      if (hat) hit = (src.c[r][st] != '.') ? (rnd(100) >= 8) : (rnd(100) < p / 5);
      else hit = (p >= 75) || ((p > 5) && (rnd(100) < p));
      if (!hit) continue;
      {
        int a = lev[r][2][st], g0 = lev[r][0][st], n = lev[r][1][st] + a + g0, x = n ? rnd(n) : 0;
        gr.c[r][st] = (x < g0) ? 'g' : ((x < g0 + a) ? 'X' : 'x');
      }
    }
  }
  if ((cnt[BR_KICK][0]) && (gr.c[BR_KICK][0] == '.')) gr.c[BR_KICK][0] = 'x';
  // second bar: the same with a few changes, sometimes a fill at the end
  for (r = 0; r < BR_NUM; r++) memcpy(&gr.c[r][16], gr.c[r], 16);
  for (k = 0; k < 3; k++)
  {
    int rr = rnd(2) ? BR_KICK : BR_CHAT, pos = 16 + 1 + rnd(14);
    if (gr.c[rr][pos] == '.') { if (cnt[rr][pos - 16]) gr.c[rr][pos] = 'x'; }
    else if (pos % 4) gr.c[rr][pos] = '.';
  }
  if ((rnd(100) < 40) && (beatcatalog[g].sub[s].fill))
  {
    GRID f;
    if (grid_read(beatcatalog[g].sub[s].fill, &f))
      for (r = 0; r < BR_NUM; r++) memcpy(&gr.c[r][28], &f.c[r][12], 4);
  }
  grid_write(&gr, randvar.grid, sizeof randvar.grid);
  snprintf(randvar.name, sizeof randvar.name, "Dice #%u", seed % 10000);
  randvar.swing = beatcatalog[g].sub[s].var[0].swing;
  israndom = 1;
}

static const XVAR *curvar(void)
{
  if (israndom) return &randvar;
  buildvars(bs.genre, bs.sub);
  return &xv[bs.genre][bs.sub][bs.var < nxv[bs.genre][bs.sub] ? bs.var : 0];
}
static int curkit(void) { return bs.kit >= 0 ? bs.kit : cursub()->kit; }
static int curswing(void) { return bs.swing >= 0 ? bs.swing : curvar()->swing; }

// ---------------------------------------------------------------------------
// Song information
// ---------------------------------------------------------------------------

static int chnpattern(int chn)
{
  return editorUndoInfo.editorInfo[chn].epnum;
}

// tempo (frames per row) of the song around the pattern: first tempo command
// in the current patterns, else the default tempo
static int songtempo(int chn)
{
  int c, r, p;
  for (c = -1; c < numchannels; c++)
  {
    p = chnpattern(c < 0 ? chn : c);
    for (r = 0; r < pattlen[p]; r++)
    {
      unsigned char *q = &pattern[p][r * 4];
      if ((q[2] == CMD_SETTEMPO) && (q[3] >= 2))
        return (q[3] & 0x7f) >= 2 ? (q[3] & 0x7f) : 6;
    }
  }
  if ((instr[MAX_INSTR - 1].ad >= 2) && (!instr[MAX_INSTR - 1].ptr[WTBL])) return instr[MAX_INSTR - 1].ad;
  return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

static int autorows16(int chn)
{
  // a 16th note of about 120 ms (125 BPM) = 6 frames at 1x speed
  int frames = editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
  int t = songtempo(chn);
  int r = (frames + t / 2) / t;
  if (r < 1) r = 1;
  if (r > 4) r = 4;
  return r;
}

static int currows16(int chn)
{
  return bs.rows16 ? bs.rows16 : autorows16(chn);
}

static int patternuses(int patt)
{
  int c, i, n = 0;
  for (c = 0; c < numchannels; c++)
    for (i = 0; i < songlen[editorInfo.esnum][c]; i++)
      if (songorder[editorInfo.esnum][c][i] == patt) n++;
  return n;
}

// ---------------------------------------------------------------------------
// Instruments: detection by name, generated drum kits
// ---------------------------------------------------------------------------

static int instrempty(int i)
{
  const INSTR *in = &instr[i];
  return (!in->ad) && (!in->sr) && (!in->ptr[0]) && (!in->ptr[1]) && (!in->ptr[2]) && (!in->ptr[3]) &&
         (!in->vibdelay) && (!in->name[0]);
}

static int contains(const char *lname, const char *w)
{
  return strstr(lname, w) != NULL;
}

// role and level of an instrument from its name, -1 = no drum
static int detectrole(int i, int *level)
{
  char n[MAX_INSTRNAMELEN + 1];
  int c;
  for (c = 0; c < MAX_INSTRNAMELEN && instr[i].name[c]; c++) n[c] = tolower((unsigned char)instr[i].name[c]);
  n[c] = 0;
  if (level) *level = contains(n, "soft") || contains(n, "ghost") ? BL_SOFT : (contains(n, " acc") ? BL_ACCENT : BL_NORMAL);
  if (!n[0]) return -1;
  if (contains(n, "crash") || contains(n, "cymbal")) return BR_CRASH;
  if (contains(n, "clap")) return BR_CLAP;
  if (contains(n, "snare") || !strncmp(n, "sd", 2)) return BR_SNARE;
  if (contains(n, "rim")) return BR_RIM;
  if (contains(n, "ohat") || (contains(n, "open") && (contains(n, "hat") || contains(n, "hh")))) return BR_OHAT;
  if (contains(n, "hat") || contains(n, "hh")) return BR_CHAT;
  if (contains(n, "tom"))
  {
    if (contains(n, "lo") || contains(n, "floor")) return BR_TOML;
    if (contains(n, "md") || contains(n, "mid")) return BR_TOMM;
    return BR_TOMH;
  }
  if (contains(n, "kick") || contains(n, "bass drum") || contains(n, "bassdrum") || !strncmp(n, "bd", 2)) return BR_KICK;
  return -1;
}

// ---------------------------------------------------------------------------
// Drum instruments of the library (instruments/<6581|8580>/Drums/<Kick ...>)
// ---------------------------------------------------------------------------

typedef struct { char *path; char *name; } LIBDRUM;
static LIBDRUM *libdrums[BR_NUM];
static int numlib[BR_NUM];
static int libmodel = -1;

static void addlib(int role, const char *path, const char *name)
{
  LIBDRUM *n = (LIBDRUM *)realloc(libdrums[role], (numlib[role] + 1) * sizeof(LIBDRUM));
  char *dot;
  if (!n) return;
  libdrums[role] = n;
  n[numlib[role]].path = strdup(path);
  n[numlib[role]].name = strdup(name);
  dot = strrchr(n[numlib[role]].name, '.');
  if (dot) *dot = 0;
  numlib[role]++;
}

static int cmplib(const void *a, const void *b)
{
  return strcmp(((const LIBDRUM *)a)->name, ((const LIBDRUM *)b)->name);
}

static void scanfolder(const char *dir, int role, int role2, int role3)
{
  DIR *d = opendir(dir);
  struct dirent *de;
  if (!d) return;
  while ((de = readdir(d)))
  {
    char full[MAX_PATHNAME * 2];
    int l = strlen(de->d_name);
    if ((l < 5) || (strcasecmp(de->d_name + l - 4, ".ins"))) continue;
    snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
    addlib(role, full, de->d_name);
    if (role2 >= 0) addlib(role2, full, de->d_name);
    if (role3 >= 0) addlib(role3, full, de->d_name);
  }
  closedir(d);
}

static void scanlibdrums(void)
{
  static const struct { const char *folder; int r1, r2, r3; } map[] = {
    {"Kick", BR_KICK, -1, -1}, {"Snare", BR_SNARE, -1, -1}, {"Clap", BR_CLAP, -1, -1}, {"HiHat", BR_CHAT, -1, -1},
    {"OpenHat", BR_OHAT, -1, -1}, {"Tom", BR_TOMH, BR_TOMM, BR_TOML}, {"Percussion", BR_RIM, -1, -1}, {"Cymbal", BR_CRASH, -1, -1}};
  int r, k, model = editorInfo.sidmodel ? 1 : 0;
  if (libmodel == model) return;
  for (r = 0; r < BR_NUM; r++)
  {
    for (k = 0; k < numlib[r]; k++) { free(libdrums[r][k].path); free(libdrums[r][k].name); }
    free(libdrums[r]);
    libdrums[r] = NULL;
    numlib[r] = 0;
    assign[r] = -1;
  }
  for (k = 0; k < (int)(sizeof map / sizeof map[0]); k++)
  {
    char dir[MAX_PATHNAME * 2];
    snprintf(dir, sizeof dir, "%s/%s/Drums/%s", instrlibpath, model ? "8580" : "6581", map[k].folder);
    scanfolder(dir, map[k].r1, map[k].r2, map[k].r3);
    snprintf(dir, sizeof dir, "%s/%s/Electro/Drums/%s", instrlibpath, model ? "8580" : "6581", map[k].folder);
    scanfolder(dir, map[k].r1, map[k].r2, map[k].r3);
  }
  for (r = 0; r < BR_NUM; r++)
    if (numlib[r] > 1) qsort(libdrums[r], numlib[r], sizeof(LIBDRUM), cmplib);
  libmodel = model;
}

static int progsame(int t, int a, int b)
{
  int k;
  if ((!a) || (!b)) return a == b;
  for (k = 0; k < 64; k++)
  {
    int pa = a - 1 + k, pb = b - 1 + k;
    if ((pa >= MAX_TABLELEN) || (pb >= MAX_TABLELEN)) return 0;
    if (ltable[t][pa] != ltable[t][pb]) return 0;
    if (ltable[t][pa] == 0xff)
    {
      // stop, or a jump to the same place in the program
      if (!rtable[t][pa] && !rtable[t][pb]) return 1;
      return (rtable[t][pa] && rtable[t][pb]) && (rtable[t][pa] - a == rtable[t][pb] - b);
    }
    if (rtable[t][pa] != rtable[t][pb]) return 0;
  }
  return 0;
}

// another instrument with exactly the same sound (instrument i was just
// added): then i is removed again and the other one is used
static int samesound(int i, int oldlen[MAX_TABLES])
{
  int k, t;
  for (k = 1; k < MAX_INSTR - 1; k++)
  {
    const INSTR *a = &instr[i], *b = &instr[k];
    if ((k == i) || (instrempty(k))) continue;
    if ((a->ad != b->ad) || (a->sr != b->sr) || (a->gatetimer != b->gatetimer) || (a->firstwave != b->firstwave) || (a->vibdelay != b->vibdelay)) continue;
    for (t = 0; t < MAX_TABLES; t++)
      if (t == STBL ? (a->ptr[t] != b->ptr[t]) && (!a->ptr[t] || !b->ptr[t] || ltable[t][a->ptr[t] - 1] != ltable[t][b->ptr[t] - 1] || rtable[t][a->ptr[t] - 1] != rtable[t][b->ptr[t] - 1]) : !progsame(t, a->ptr[t], b->ptr[t])) break;
    if (t < MAX_TABLES) continue;
    // remove the new one and its table rows
    memset(&instr[i], 0, sizeof(INSTR));
    for (t = 0; t < MAX_TABLES; t++)
    {
      int r;
      for (r = oldlen[t]; r < MAX_TABLELEN; r++) ltable[t][r] = rtable[t][r] = 0;
    }
    return k;
  }
  return i;
}

static int loadlibdrum(int role, int idx, char *err, int errsize)
{
  int slot, old[MAX_TABLES], t, keep = editorInfo.einum;
  char save[MAX_FILENAME];
  for (slot = 1; slot < MAX_INSTR - 1; slot++)
    if (instrempty(slot)) break;
  if (slot >= MAX_INSTR - 1)
  {
    snprintf(err, errsize, "No free instrument slot for %s", libdrums[role][idx].name);
    return 0;
  }
  for (t = 0; t < MAX_TABLES; t++) old[t] = gettablelen(t);
  strcpy(save, instrfilename);
  snprintf(instrfilename, MAX_FILENAME, "%s", libdrums[role][idx].path);
  editorInfo.einum = slot;
  loadinstrument();
  editorInfo.einum = keep;
  strcpy(instrfilename, save);
  if (instrloadfailed)
  {
    memset(&instr[slot], 0, sizeof(INSTR));
    snprintf(err, errsize, "Could not load %s (tables full?)", libdrums[role][idx].name);
    return 0;
  }
  if (!instr[slot].name[0]) snprintf(instr[slot].name, MAX_INSTRNAMELEN, "%.15s", libdrums[role][idx].name);
  return samesound(slot, old);
}

static void autoassign(void)
{
  int r, i, lev;
  for (r = 0; r < BR_NUM; r++)
  {
    assign[r] = 0;
    for (i = 1; i < MAX_INSTR - 1; i++)
    {
      if ((detectrole(i, &lev) == r) && (lev == BL_NORMAL))
      {
        assign[r] = i;
        break;
      }
    }
  }
}

typedef struct
{
  unsigned char l[32], r[32];
  int n;
} DTBL;

typedef struct
{
  char name[MAX_INSTRNAMELEN];
  int ad, sr, firstwave, gatetimer;
  DTBL w, p;
} DRUMINS;

#define W_TRI 0x11
#define W_PUL 0x41
#define W_NOI 0x81
#define W_SILENT 0xe1
#define AN(n) (0x80 + (n))

static void dadd(DTBL *t, int l, int r)
{
  if (t->n < 31)
  {
    t->l[t->n] = l;
    t->r[t->n] = r;
    t->n++;
  }
}

// jump to row (0-based) of this program, or stop (row < 0)
static void djump(DTBL *t, int row)
{
  dadd(t, 0xff, row < 0 ? 0 : row + 1);
}

static void dpulse(DTBL *t, int pw)
{
  dadd(t, 0x80 | ((pw >> 8) & 0x0f), pw & 0xff);
  djump(t, -1);
}

static void falling(DTBL *w, int wave, int from, int to, int step)
{
  int n;
  for (n = from; n > to; n -= step) dadd(w, wave, AN(n));
  dadd(w, wave, AN(to));
}

static void makedrum(DRUMINS *d, int role, int level, int kit, int tempo)
{
  DTBL *w = &d->w, *p = &d->p;
  int decay = 8, attack = 0, c;
  int tone = (kit == KIT_808 || kit == KIT_909) ? W_TRI : W_PUL;
  static const char *lev[BL_NUM] = {" soft", "", " acc"};

  memset(d, 0, sizeof *d);
  d->firstwave = 0x09;
  d->gatetimer = tempo >= 3 ? 2 : 1;
  switch (role)
  {
    case BR_KICK:
    decay = (kit == KIT_808) ? 9 : 8;
    if (kit != KIT_808) dadd(w, W_NOI, AN(kit == KIT_909 ? 84 : (kit == KIT_ROCK ? 76 : 80)));
    if (kit == KIT_808) falling(w, W_TRI, 52, 28, 3);
    else if (kit == KIT_909) falling(w, W_TRI, 50, 29, 4);
    else if (kit == KIT_ROCK) falling(w, W_PUL, 46, 26, 5);
    else falling(w, W_PUL, 48, 24, 7);
    djump(w, -1);
    dpulse(p, kit == KIT_ROCK ? 0x600 : 0x800);
    break;

    case BR_SNARE:
    decay = 8;
    dadd(w, W_NOI, AN(kit == KIT_909 ? 92 : (kit == KIT_CHIP ? 86 : 90)));
    dadd(w, tone, AN(kit == KIT_909 ? 50 : (kit == KIT_ROCK ? 48 : 44)));
    if (kit != KIT_CHIP) dadd(w, tone, AN(kit == KIT_909 ? 47 : (kit == KIT_ROCK ? 45 : 42)));
    dadd(w, W_NOI, AN(kit == KIT_909 ? 86 : (kit == KIT_CHIP ? 78 : 82)));
    dadd(w, W_NOI, AN(kit == KIT_909 ? 80 : (kit == KIT_CHIP ? 74 : 79)));
    djump(w, w->n - 1);
    dpulse(p, kit == KIT_909 ? 0x500 : 0x700);
    break;

    case BR_CLAP:
    decay = 7;
    dadd(w, W_NOI, AN(88));
    dadd(w, W_SILENT, 0x80);
    dadd(w, W_NOI, AN(88));
    dadd(w, W_SILENT, 0x80);
    dadd(w, W_NOI, AN(86));
    djump(w, w->n - 1);
    break;

    case BR_CHAT:
    case BR_OHAT:
    decay = role == BR_OHAT ? ((kit == KIT_909 || kit == KIT_ROCK) ? 8 : 7) : (kit == KIT_ROCK ? 5 : (kit == KIT_909 ? 4 : 3));
    dadd(w, W_NOI, AN(95));
    dadd(w, W_NOI, AN(92));
    djump(w, 1);
    break;

    case BR_TOMH:
    case BR_TOMM:
    case BR_TOML:
    {
      int start = role == BR_TOMH ? 54 : (role == BR_TOMM ? 47 : 40);
      decay = 9;
      if ((kit == KIT_909) || (kit == KIT_ROCK)) dadd(w, W_NOI, AN(80));
      for (c = 0; c < 5; c++) dadd(w, tone, AN(start - c * 2));
      dadd(w, tone, AN(start - 9));
      djump(w, -1);
      dpulse(p, 0x600);
    }
    break;

    case BR_RIM:
    decay = 5;
    dadd(w, W_NOI, AN(90));
    dadd(w, W_PUL, AN(72));
    dadd(w, W_PUL & 0xfe, AN(72));
    djump(w, -1);
    dpulse(p, 0x400);
    break;

    case BR_CRASH:
    decay = kit == KIT_CHIP ? 9 : 0xa;
    dadd(w, W_NOI, AN(84));
    dadd(w, W_TRI, AN(44));
    dadd(w, W_TRI, AN(36));
    dadd(w, W_NOI, AN(93));
    dadd(w, W_NOI, AN(89));
    djump(w, w->n - 1);
    break;
  }
  if (level == BL_ACCENT)
  {
    if (decay < 0xf) decay++;
  }
  d->sr = 0x00;
  if (level == BL_SOFT)
  {
    // soft (ghost) note: slow attack, the gate goes off after the first
    // table frame, so the envelope never reaches the full level
    attack = 7;
    if (decay > 1) decay--;
    d->sr = 0x03;
    for (c = 1; c < w->n; c++)
      if (w->l[c] != 0xff) w->l[c] &= 0xfe;
  }
  d->ad = (attack << 4) | decay;
  snprintf(d->name, MAX_INSTRNAMELEN, "%s %s%s", beatrolenames[role], kitnames[kit], lev[level]);
}

static int tablespace(const DRUMINS *d)
{
  return (gettablelen(WTBL) + d->w.n <= MAX_TABLELEN) && (gettablelen(PTBL) + d->p.n <= MAX_TABLELEN);
}

// write a generated drum into a free instrument slot; returns the slot or 0
static int adddrum(const DRUMINS *d)
{
  int i, t, c;
  for (i = 1; i < MAX_INSTR - 1; i++)
    if (instrempty(i)) break;
  if ((i >= MAX_INSTR - 1) || (!tablespace(d))) return 0;
  instr[i].ad = d->ad;
  instr[i].sr = d->sr;
  instr[i].firstwave = d->firstwave;
  instr[i].gatetimer = d->gatetimer;
  instr[i].vibdelay = 0;
  memset(instr[i].name, 0, MAX_INSTRNAMELEN);
  strncpy(instr[i].name, d->name, MAX_INSTRNAMELEN - 1);
  for (t = 0; t < 2; t++)
  {
    const DTBL *s = t ? &d->p : &d->w;
    int start = gettablelen(t);
    instr[i].ptr[t] = 0;
    if (!s->n) continue;
    for (c = 0; c < s->n; c++)
    {
      ltable[t][start + c] = s->l[c];
      rtable[t][start + c] = s->r[c];
      if ((s->l[c] == 0xff) && (s->r[c])) rtable[t][start + c] = s->r[c] + start;
    }
    instr[i].ptr[t] = start + 1;
  }
  instr[i].ptr[FTBL] = 0;
  instr[i].ptr[STBL] = 0;
  return i;
}

// generated drum in a free slot (for the Improve function); 0 = no room
int beat_newdrum(int role, int level, int kit, int tempo)
{
  DRUMINS d;
  makedrum(&d, role, level, kit, tempo);
  return adddrum(&d);
}

// existing instrument of a role with the wanted level (same name family)
static int findlevel(int role, int level)
{
  int i, lev;
  for (i = 1; i < MAX_INSTR - 1; i++)
    if ((detectrole(i, &lev) == role) && (lev == level)) return i;
  return 0;
}

// ---------------------------------------------------------------------------
// Rendering: template -> one event per pattern row
// ---------------------------------------------------------------------------

typedef struct
{
  int role;           // -1 = nothing
  int level;
} ROWEV;

static unsigned rngstate;
static int rnd100(void)
{
  rngstate = rngstate * 1103515245u + 12345u;
  return (rngstate >> 16) % 100;
}

// template with the random variation applied
static int varianthits(BEATHIT *hits, int *steps)
{
  int n = beat_parse(curvar()->grid, hits, MAXHITS, steps);
  int s, k, hassnare = 0, hashat = 0;
  if (n < 0) return n;
  if (!bs.variation) return n;
  rngstate = bs.variation * 7919u;
  for (k = 0; k < n; k++)
  {
    if (hits[k].role == BR_SNARE) hassnare = 1;
    if (hits[k].role == BR_CHAT) hashat = 1;
  }
  // remove some hats, double some into rolls
  for (k = 0; k < n; k++)
  {
    if ((hits[k].role == BR_CHAT) && (rnd100() < 12))
    {
      hits[k] = hits[--n];
      k--;
    }
    else if ((hits[k].role == BR_CHAT) && (!(hits[k].pos32 & 1)) && (rnd100() < 10) && (n < MAXHITS))
    {
      hits[n] = hits[k];
      hits[n].pos32++;
      n++;
    }
  }
  for (s = 0; s < *steps; s++)
  {
    int used = 0;
    for (k = 0; k < n; k++)
      if (hits[k].pos32 / 2 == s) used = 1;
    if (used || (n >= MAXHITS - 1)) continue;
    // ghost snares on free off-beat 16ths, extra kicks after a kick
    if ((hassnare) && (s & 1) && (rnd100() < 18))
    {
      hits[n].pos32 = s * 2; hits[n].role = BR_SNARE; hits[n].level = BL_SOFT; n++;
    }
    else if ((s > 0) && (rnd100() < 10))
    {
      for (k = 0; k < n; k++)
        if ((hits[k].role == BR_KICK) && (hits[k].pos32 / 2 == s - 1)) break;
      if (k < n) { hits[n].pos32 = s * 2; hits[n].role = BR_KICK; hits[n].level = BL_NORMAL; n++; }
    }
    else if ((hashat) && (!(s & 1)) && (rnd100() < 8))
    {
      hits[n].pos32 = s * 2; hits[n].role = BR_OHAT; hits[n].level = BL_NORMAL; n++;
    }
  }
  return n;
}

static void place(ROWEV *ev, int rows, int row, const BEATHIT *h)
{
  if ((row < 0) || (row >= rows)) return;
  if ((ev[row].role < 0) || (roleprio[h->role] > roleprio[ev[row].role]))
  {
    ev[row].role = h->role;
    ev[row].level = h->level;
  }
}

// events for a pattern of "rows" rows; returns 0 on a template error
static int render(ROWEV *ev, int rows, int r16)
{
  static BEATHIT hits[MAXHITS], fhits[MAXHITS];
  int n, fn = 0, steps, fsteps = 16, base, k, swingoff, fillstart = rows;
  int sw = curswing();

  n = varianthits(hits, &steps);
  if (n < 0) return 0;
  if ((bs.fill) && (cursub()->fill))
  {
    fn = beat_parse(cursub()->fill, fhits, MAXHITS, &fsteps);
    if (fn < 0) return 0;
    if (rows >= 16 * r16) fillstart = rows - 16 * r16;
  }
  // swing: odd 16th notes later (only with 2 or more rows per 16th)
  swingoff = (2 * r16 * sw + 50) / 100 - r16;
  if (swingoff < 0) swingoff = 0;
  if (swingoff >= r16) swingoff = r16 - 1;

  for (k = 0; k < rows; k++) ev[k].role = -1;
  for (base = 0; base < fillstart; base += steps * r16)
  {
    for (k = 0; k < n; k++)
    {
      int row = base + hits[k].pos32 * r16 / 2;
      if ((hits[k].pos32 / 2) & 1) row += swingoff;
      if (row < fillstart) place(ev, rows, row, &hits[k]);
    }
  }
  for (k = 0; k < fn; k++)
  {
    int row = fillstart + fhits[k].pos32 * r16 / 2;
    if ((fhits[k].pos32 / 2) & 1) row += swingoff;
    if (fhits[k].pos32 < 32) place(ev, rows, row, &fhits[k]);
  }
  return 1;
}

// the instruments for the events (missing ones are generated); 0 = error
static int makeslots(const ROWEV *ev, int rows, char *err, int errsize, int tempo)
{
  int need[BR_NUM][BL_NUM], r, l, kit = curkit();
  memset(need, 0, sizeof need);
  for (r = 0; r < rows; r++)
    if (ev[r].role >= 0) need[ev[r].role][ev[r].level] = 1;

  // instruments per role and level
  for (r = 0; r < BR_NUM; r++)
  {
    for (l = 0; l < BL_NUM; l++)
    {
      genslot[r][l] = 0;
      if (!need[r][l]) continue;
      if ((assign[r] >= 0) && (assign[r] < numlib[r]))
      {
        // a library instrument (one for all levels)
        int k;
        for (k = 0; k < BL_NUM; k++)
          if (genslot[r][k]) break;
        genslot[r][l] = k < BL_NUM ? genslot[r][k] : loadlibdrum(r, assign[r], err, errsize);
        if (!genslot[r][l]) return 0;
      }
      else
      {
        DRUMINS d;
        int old[MAX_TABLES], t;
        for (t = 0; t < MAX_TABLES; t++) old[t] = gettablelen(t);
        makedrum(&d, r, l, kit, tempo);
        genslot[r][l] = adddrum(&d);
        if (genslot[r][l]) genslot[r][l] = samesound(genslot[r][l], old);
        if (!genslot[r][l])
        {
          snprintf(err, errsize, "No room for the instrument \"%s\" (instruments or tables full)", d.name);
          return 0;
        }
      }
    }
  }
  return 1;
}

// Write the beat into the pattern of channel chn (whole pattern). Missing
// instruments are generated. Returns the number of rows, 0 on error.
static int writebeat(int chn, char *err, int errsize)
{
  static ROWEV ev[MAX_PATTROWS];
  int patt = chnpattern(chn), rows = pattlen[patt], r16 = currows16(chn);
  int r, tempo = songtempo(chn);

  if (!render(ev, rows, r16))
  {
    snprintf(err, errsize, "Template error in %s / %s", cursub()->name, curvar()->name);
    return 0;
  }
  if (!makeslots(ev, rows, err, errsize, tempo)) return 0;
  // whole pattern: notes, instruments and commands are replaced; tempo
  // commands stay
  for (r = 0; r < rows; r++)
  {
    unsigned char *q = &pattern[patt][r * 4];
    q[0] = REST;
    q[1] = 0;
    if ((q[2] != CMD_SETTEMPO) && (q[2] != CMD_FUNKTEMPO))
    {
      q[2] = 0;
      q[3] = 0;
    }
    if (ev[r].role >= 0)
    {
      q[0] = FIRSTNOTE + rolenote[ev[r].role];
      q[1] = genslot[ev[r].role][ev[r].level];
    }
  }
  return rows;
}

// ---------------------------------------------------------------------------
// A beat for the whole song: the parts of the song (from the composer, or
// found in what the other voices play), the beat follows them - quieter in
// the intro and outro, fuller in the strong parts, a fill before every new
// part, a crash at the start of a strong part
// ---------------------------------------------------------------------------

SONGPARTS songparts;
static int bs_whole = 0;               // 1 = the whole subtune instead of the pattern

#define WS_MAXROWS 16384
#define WS_MAXSECS 256
static ROWEV *wev = NULL;
static int ws_total, ws_barrows, ws_n, ws_exact;
static int ws_start[WS_MAXSECS], ws_bars[WS_MAXSECS], ws_kind[WS_MAXSECS], ws_level[WS_MAXSECS], ws_new[WS_MAXSECS];
static char ws_info[200];
static int ws_emin;
static const char *ws_kindname[5] = {"intro", "verse", "chorus", "bridge", "outro"};

static int ws_flatlen(int sub, int c)
{
  int d, n = 0, rep = 0;
  for (d = 0; d < songlen[sub][c]; d++)
  {
    int v = songorder[sub][c][d];
    if (v >= TRANSDOWN) continue;
    if (v >= REPEAT) { rep = v - REPEAT; continue; }
    n += pattlen[v] * (rep + 1);
    rep = 0;
  }
  return n;
}

static int ws_empty(int c)
{
  int sub = editorInfo.esnum, d, r;
  for (d = 0; d < songlen[sub][c]; d++)
  {
    int v = songorder[sub][c][d];
    if (v >= REPEAT) continue;
    for (r = 0; r < pattlen[v]; r++)
      if (pattern[v][r * 4] <= LASTNOTE) return 0;
  }
  return 1;
}

// the voice for a whole-song beat: the cursor's voice when it is empty, else
// an empty voice (first on the same SID)
static int ws_target(int ch)
{
  int c;
  if (ws_empty(ch)) return ch;
  for (c = (ch / 3) * 3; (c < (ch / 3) * 3 + 3) && (c < numchannels); c++)
    if (ws_empty(c)) return c;
  for (c = 0; c < numchannels; c++)
    if (ws_empty(c)) return c;
  return ch;
}

static int ws_levelof(double e)
{
  return e < 0.3 ? 0 : e < 0.55 ? 1 : e < 0.8 ? 2 : 3;
}

// the parts of the subtune; 0 = nothing to follow
static int ws_analyse(int chn, char *err, int errsize)
{
  int sub = editorInfo.esnum, c, k, nbars, b;
  static int dens[WS_MAXROWS / 4 + 1];
  static unsigned sig[WS_MAXROWS / 4 + 1];
  ws_barrows = 16 * currows16(chn);
  ws_total = 0;
  for (c = 0; c < numchannels; c++)
  {
    int n = ws_flatlen(sub, c);
    if ((c != chn) && (n > ws_total)) ws_total = n;
  }
  if (ws_total > WS_MAXROWS) ws_total = WS_MAXROWS;
  if (ws_total < ws_barrows)
  {
    snprintf(err, errsize, "The other voices of subtune %02X are empty - write the song first (or use Length: This pattern)", sub);
    return 0;
  }
  ws_n = 0;
  ws_exact = 0;
  // a song of the composer: its parts exactly
  if ((songparts.n) && (!strcmp(songname, songparts.title)) && (songparts.barrows == ws_barrows) &&
      (abs(songparts.rows - ws_total) <= ws_barrows))
  {
    int bar = 0;
    for (k = 0; (k < songparts.n) && (ws_n < WS_MAXSECS); k++)
    {
      ws_start[ws_n] = bar;
      ws_bars[ws_n] = songparts.bars[k];
      ws_kind[ws_n] = songparts.kind[k];
      ws_level[ws_n] = ws_levelof(songparts.energy[k]);
      ws_new[ws_n] = (!k) || (songparts.kind[k] != songparts.kind[k - 1]);
      bar += songparts.bars[k];
      ws_n++;
    }
    ws_exact = 1;
  }
  else
  {
    // what the other voices play, bar by bar: how much and what
    int d, P, np, i, emax = 0;
    static int pe[WS_MAXSECS];
    static unsigned ps[WS_MAXSECS];
    nbars = (ws_total + ws_barrows - 1) / ws_barrows;
    memset(dens, 0, sizeof(int) * (nbars + 1));
    memset(sig, 0, sizeof(unsigned) * (nbars + 1));
    for (c = 0; c < numchannels; c++)
    {
      int row = 0, rep = 0, trans = 0;
      if (c == chn) continue;
      for (d = 0; (d < songlen[sub][c]) && (row < ws_total); d++)
      {
        int v = songorder[sub][c][d], x, r;
        if (v >= TRANSDOWN) { trans = v - TRANSUP; continue; }
        if (v >= REPEAT) { rep = v - REPEAT; continue; }
        for (x = 0; x <= rep; x++)
          for (r = 0; (r < pattlen[v]) && (row < ws_total); r++, row++)
          {
            unsigned char *q = &pattern[v][r * 4];
            if (q[0] <= LASTNOTE)
            {
              b = row / ws_barrows;
              dens[b]++;
              sig[b] = sig[b] * 31u + (unsigned)(c * 7919 + (row % ws_barrows) * 131 + (q[0] + trans) * 17 + q[1]);
            }
          }
        rep = 0;
      }
    }
    // phrases of 4 or 8 bars; the same phrase again belongs to the same part
    P = nbars > 128 ? 8 : (nbars >= 4 ? 4 : nbars);
    np = (nbars + P - 1) / P;
    if (np > WS_MAXSECS) np = WS_MAXSECS;
    for (i = 0; i < np; i++)
    {
      pe[i] = 0;
      ps[i] = 0;
      for (b = i * P; (b < (i + 1) * P) && (b < nbars); b++)
      {
        pe[i] += dens[b];
        ps[i] = ps[i] * 2654435761u + sig[b];
      }
      if (pe[i] > emax) emax = pe[i];
    }
    {
      int emin = emax;
      for (i = 0; i < np; i++)
        if ((pe[i] < emin) && (((i + 1) * P <= nbars) || (np == 1))) emin = pe[i];
      ws_emin = emin;
    }
    for (i = 0; i < np; i++)
    {
      // the energy relative to the quietest and the fullest phrase of the song
      double e = (emax - ws_emin > emax / 8) ? 0.35 + 0.6 * (pe[i] - ws_emin) / (double)(emax - ws_emin) : 0.65;
      int bars = (i + 1) * P <= nbars ? P : nbars - i * P;
      if ((ws_n) && (ps[i] == ps[i - 1]) && (ws_n > 0) && (ws_start[ws_n - 1] + ws_bars[ws_n - 1] == i * P) && (ws_bars[ws_n - 1] < 16))
      {
        ws_bars[ws_n - 1] += bars;            // the same phrase again: the part goes on
        continue;
      }
      ws_start[ws_n] = i * P;
      ws_bars[ws_n] = bars;
      ws_level[ws_n] = ws_levelof(e);
      ws_kind[ws_n] = e >= 0.8 ? SPK_CHORUS : SPK_VERSE;
      ws_new[ws_n] = (!ws_n) || (ps[i] != ps[i - 1]);
      ws_n++;
    }
    // neighbours of the same kind and strength make one part (up to 16 bars)
    for (i = 1; i < ws_n; i++)
      if ((ws_kind[i] == ws_kind[i - 1]) && (ws_level[i] == ws_level[i - 1]) && (ws_bars[i - 1] + ws_bars[i] <= 16))
      {
        ws_bars[i - 1] += ws_bars[i];
        memmove(&ws_start[i], &ws_start[i + 1], sizeof(int) * (ws_n - i - 1));
        memmove(&ws_bars[i], &ws_bars[i + 1], sizeof(int) * (ws_n - i - 1));
        memmove(&ws_kind[i], &ws_kind[i + 1], sizeof(int) * (ws_n - i - 1));
        memmove(&ws_level[i], &ws_level[i + 1], sizeof(int) * (ws_n - i - 1));
        memmove(&ws_new[i], &ws_new[i + 1], sizeof(int) * (ws_n - i - 1));
        ws_n--;
        i--;
      }
    if ((ws_n > 2) && (ws_level[0] < ws_level[1])) ws_kind[0] = SPK_INTRO;
    if ((ws_n > 2) && (ws_level[ws_n - 1] < ws_level[ws_n - 2])) ws_kind[ws_n - 1] = SPK_OUTRO;
  }
  for (k = 0; k < ws_n; k++)
    if (((ws_kind[k] == SPK_INTRO) || (ws_kind[k] == SPK_OUTRO)) && (ws_level[k] > 1)) ws_level[k] = 1;
  {
    int o = snprintf(ws_info, sizeof ws_info, "%d parts%s: ", ws_n, ws_exact ? " (of the composed song)" : " found");
    for (k = 0; (k < ws_n) && (o < (int)sizeof ws_info - 16); k++)
      o += snprintf(ws_info + o, sizeof ws_info - o, "%s%s %d", k ? ", " : "", ws_kindname[ws_kind[k]], ws_bars[k]);
    if (k < ws_n) snprintf(ws_info + o, sizeof ws_info - o, " ...");
  }
  return 1;
}

static void ws_put(int row, int role, int level)
{
  BEATHIT h;
  h.pos32 = 0;
  h.role = role;
  h.level = level;
  place(wev, ws_total, row, &h);
}

// the beat of one part: the template, thinned or filled by the level
static void ws_part(int s, const BEATHIT *hits, int n, int steps, int r16, int swingoff)
{
  int r0 = ws_start[s] * ws_barrows, rows = ws_bars[s] * ws_barrows, end = r0 + rows, lvl = ws_level[s], base, k, last = s == ws_n - 1;
  int fillrows = 0, big = 0, hashat = 0;
  if (end > ws_total) end = ws_total;
  for (k = 0; k < n; k++)
    if ((hits[k].role == BR_CHAT) || (hits[k].role == BR_OHAT)) hashat = 1;
  // a fill before the next part
  if ((bs.fill) && (!last) && (ws_bars[s] >= 2) && (lvl >= 1))
  {
    big = (ws_level[s + 1] > lvl) || ((ws_kind[s + 1] == SPK_CHORUS) && (ws_new[s + 1]));
    fillrows = big ? ws_barrows : ws_barrows / 4;
    if ((!big) && (!ws_new[s + 1])) fillrows = 0;
  }
  for (base = r0; base < end - fillrows; base += steps * r16)
  {
    for (k = 0; k < n; k++)
    {
      BEATHIT h = hits[k];
      int pos16 = h.pos32 / 2, row = base + h.pos32 * r16 / 2;
      if (pos16 & 1) row += swingoff;
      if (row >= end - fillrows) continue;
      if (lvl == 0)
      {
        // quiet: the hats on the 8ths, a kick on the one
        if ((h.role == BR_CHAT) && (!(pos16 & 1))) { h.level = BL_SOFT; place(wev, ws_total, row, &h); }
        else if ((h.role == BR_KICK) && (pos16 % 16 == 0)) place(wev, ws_total, row, &h);
        else if ((!hashat) && (h.role == BR_RIM)) place(wev, ws_total, row, &h);
        continue;
      }
      if (lvl == 1)
      {
        // light: no open hats, no ghost notes, hats only on the 8ths
        if (h.role == BR_OHAT) continue;
        if ((h.level == BL_SOFT) && (h.role != BR_CHAT)) continue;
        if ((h.role == BR_CHAT) && (pos16 & 1)) continue;
        if (h.role == BR_CRASH) continue;
      }
      place(wev, ws_total, row, &h);
    }
    if ((lvl == 3) && (hashat))
    {
      // full: the hats on every 16th, an open hat at the end of two bars
      int p;
      for (p = 0; p < steps; p++)
      {
        int row = base + p * r16 + ((p & 1) ? swingoff : 0);
        if (row < end - fillrows) ws_put(row, BR_CHAT, (p & 1) ? BL_SOFT : BL_NORMAL);
      }
      if (base + (steps - 2) * r16 < end - fillrows) ws_put(base + (steps - 2) * r16, BR_OHAT, BL_NORMAL);
    }
  }
  // crash at the start of a strong new part
  if (((lvl >= 2) && (ws_new[s])) || ((s) && (ws_kind[s - 1] == SPK_INTRO) && (lvl >= 1)))
  {
    ws_put(r0, BR_CRASH, BL_ACCENT);
  }
  // the fill
  if (fillrows)
  {
    int f0 = end - fillrows, p;
    if ((big) && (cursub()->fill))
    {
      static BEATHIT fh[MAXHITS];
      int fs = 16, fn = beat_parse(cursub()->fill, fh, MAXHITS, &fs);
      for (k = 0; k < fn; k++)
      {
        int row = f0 + fh[k].pos32 * r16 / 2;
        if ((fh[k].pos32 / 2) & 1) row += swingoff;
        if (row < end) place(wev, ws_total, row, &fh[k]);
      }
    }
    else
    {
      // a snare roll that gets louder, toms at the end of a big one
      int n16 = fillrows / r16;
      for (p = 0; p < n16; p++)
      {
        int row = f0 + p * r16, role = BR_SNARE, lev = p < n16 / 2 ? BL_SOFT : (p >= n16 - 2 ? BL_ACCENT : BL_NORMAL);
        if ((big) && (n16 >= 8) && (p >= n16 - 4)) role = p == n16 - 4 ? BR_TOMH : p == n16 - 3 ? BR_TOMM : BR_TOML;
        if ((!big) || (p >= n16 / 2) || (!(p & 1))) ws_put(row, role, lev);
      }
      ws_put(f0, BR_KICK, BL_NORMAL);
    }
  }
  // the end of the song: a last hit
  if (last)
  {
    int lb = (ws_start[s] + ws_bars[s] - 1) * ws_barrows;
    int r;
    if (lb < r0) lb = r0;
    for (r = lb; r < end; r++) wev[r].role = -1;
    ws_put(lb, BR_CRASH, BL_ACCENT);
  }
}

static int ws_render(int chn, char *err, int errsize)
{
  static BEATHIT hits[MAXHITS];
  int n, steps, k, r16 = currows16(chn), sw = curswing(), swingoff;
  if (!ws_analyse(chn, err, errsize)) return 0;
  n = varianthits(hits, &steps);
  if (n < 0)
  {
    snprintf(err, errsize, "Template error in %s / %s", cursub()->name, curvar()->name);
    return 0;
  }
  if (!wev) wev = (ROWEV *)malloc(sizeof(ROWEV) * (WS_MAXROWS + 1));
  if (!wev)
  {
    snprintf(err, errsize, "Out of memory");
    return 0;
  }
  swingoff = (2 * r16 * sw + 50) / 100 - r16;
  if (swingoff < 0) swingoff = 0;
  if (swingoff >= r16) swingoff = r16 - 1;
  for (k = 0; k < ws_total; k++) wev[k].role = -1;
  for (k = 0; k < ws_n; k++) ws_part(k, hits, n, steps, r16, swingoff);
  return 1;
}

static int ws_patternfree(int p, const unsigned char *reserved)
{
  int s2, c, d, r;
  if (reserved[p]) return 0;
  for (r = 0; r < pattlen[p]; r++)
    if ((pattern[p][r * 4] != REST) || (pattern[p][r * 4 + 1]) || (pattern[p][r * 4 + 2]) || (pattern[p][r * 4 + 3])) return 0;
  for (s2 = 0; s2 < MAX_SONGS; s2++)
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < songlen[s2][c]; d++)
        if (songorder[s2][c][d] == p) return 0;
  return 1;
}

// the whole subtune: new patterns and order list of voice chn (its tempo
// commands are kept). Returns the rows, 0 on error
static int writesongbeat(int chn, char *err, int errsize)
{
  static unsigned char reserved[MAX_PATT];
  static unsigned char tcmd[WS_MAXROWS + 1], tdat[WS_MAXROWS + 1];
  int sub = editorInfo.esnum, plen, start, n = 0, k, d, row, rep, old[MAX_SONGLEN], nold = 0;
  unsigned char order[MAX_SONGLEN + 2];
  if (!ws_render(chn, err, errsize)) return 0;
  if (!makeslots(wev, ws_total, err, errsize, songtempo(chn))) return 0;
  plen = ws_barrows * 4 <= MAX_PATTROWS ? ws_barrows * 4 : ws_barrows;
  // the tempo commands of the voice
  memset(tcmd, 0, ws_total);
  memset(tdat, 0, ws_total);
  row = 0;
  rep = 0;
  for (d = 0; (d < songlen[sub][chn]) && (row < ws_total); d++)
  {
    int v = songorder[sub][chn][d], x, r;
    if (v >= TRANSDOWN) continue;
    if (v >= REPEAT) { rep = v - REPEAT; continue; }
    for (x = 0; x <= rep; x++)
      for (r = 0; (r < pattlen[v]) && (row < ws_total); r++, row++)
      {
        int cm = pattern[v][r * 4 + 2];
        if ((cm == CMD_SETTEMPO) || (cm == CMD_FUNKTEMPO)) { tcmd[row] = cm; tdat[row] = pattern[v][r * 4 + 3]; }
      }
    rep = 0;
  }
  // the old patterns of the voice: emptied when nothing else uses them
  for (k = 0; k < songlen[sub][chn]; k++)
    if (songorder[sub][chn][k] < REPEAT) old[nold++] = songorder[sub][chn][k];
  songlen[sub][chn] = 0;
  songorder[sub][chn][0] = LOOPSONG;
  songorder[sub][chn][1] = 0;
  for (k = 0; k < nold; k++)
  {
    int p = old[k], s2, cc, used = 0;
    for (s2 = 0; (s2 < MAX_SONGS) && (!used); s2++)
      for (cc = 0; (cc < MAX_CHN) && (!used); cc++)
        for (d = 0; d < songlen[s2][cc]; d++)
          if (songorder[s2][cc][d] == p) { used = 1; break; }
    if (!used)
    {
      int r;
      for (r = 0; r < pattlen[p]; r++)
      {
        pattern[p][r * 4] = REST;
        pattern[p][r * 4 + 1] = pattern[p][r * 4 + 2] = pattern[p][r * 4 + 3] = 0;
      }
    }
  }
  memset(reserved, 0, sizeof reserved);
  for (start = 0; start < ws_total; start += plen)
  {
    unsigned char rows[MAX_PATTROWS * 4];
    int len = ws_total - start < plen ? ws_total - start : plen, r, p, q;
    for (r = 0; r < len; r++)
    {
      unsigned char *w = &rows[r * 4];
      int rr = start + r;
      w[0] = REST;
      w[1] = w[2] = w[3] = 0;
      if (wev[rr].role >= 0)
      {
        w[0] = FIRSTNOTE + rolenote[wev[rr].role];
        w[1] = genslot[wev[rr].role][wev[rr].level];
      }
      if (tcmd[rr]) { w[2] = tcmd[rr]; w[3] = tdat[rr]; }
    }
    p = -1;
    for (q = 0; q < MAX_PATT; q++)
      if ((reserved[q]) && (pattlen[q] == len) && (!memcmp(pattern[q], rows, len * 4))) { p = q; break; }
    if (p < 0)
    {
      for (q = 0; q < MAX_PATT; q++)
        if (ws_patternfree(q, reserved)) { p = q; break; }
      if (p < 0)
      {
        snprintf(err, errsize, "No free pattern left (all %d are used)", MAX_PATT);
        return 0;
      }
      memcpy(pattern[p], rows, len * 4);
      pattern[p][len * 4] = ENDPATT;
      pattern[p][len * 4 + 1] = pattern[p][len * 4 + 2] = pattern[p][len * 4 + 3] = 0;
      pattlen[p] = len;
      reserved[p] = 1;
    }
    if (n >= MAX_SONGLEN - 2)
    {
      snprintf(err, errsize, "The order list of the voice would be too long");
      return 0;
    }
    order[n++] = p;
  }
  for (k = 0; k < n; k++) songorder[sub][chn][k] = order[k];
  songorder[sub][chn][n] = LOOPSONG;
  songorder[sub][chn][n + 1] = 0;
  songlen[sub][chn] = n;
  countpatternlengths();
  editorUndoInfo.editorInfo[chn].epnum = order[0];
  return ws_total;
}

// ---------------------------------------------------------------------------
// Preview: song data snapshot, pattern loop, restore
// ---------------------------------------------------------------------------

static void *prevdata = NULL;
static int prevlen[MAX_PATT];
static unsigned char (*prevorder)[MAX_CHN][MAX_SONGLEN + 2] = NULL;   // the order lists (whole-song beat)
static int prevsonglen[MAX_SONGS][MAX_CHN], prevepnum[MAX_CHN];
static unsigned char prevmute[MAX_CHN];
static int previewing = 0;

// stop the player and wait until it has really stopped (stopsong() only asks
// for it; the player thread silences the SID on its next call)
static void stopandwait(void)
{
  int t;
  stopsong();
  for (t = 0; (t < 100) && (isplaying()); t++) SDL_Delay(5);
}

static void restoresong(void)
{
  snd_lock();
  songdata_restore(prevdata);
  memcpy(pattlen, prevlen, sizeof prevlen);
  if (prevorder)
  {
    int c;
    memcpy(songorder, prevorder, sizeof songorder);
    memcpy(songlen, prevsonglen, sizeof songlen);
    for (c = 0; c < MAX_CHN; c++) editorUndoInfo.editorInfo[c].epnum = prevepnum[c];
    countpatternlengths();
  }
  snd_unlock();
}

static void stoppreview(void)
{
  int c;
  if (!previewing) return;
  snd_lock();
  stopsong();
  snd_unlock();
  while (isplaying()) SDL_Delay(5);
  restoresong();
  for (c = 0; c < MAX_CHN; c++) chn[c].mute = prevmute[c];
  songdata_free(prevdata);
  prevdata = NULL;
  previewing = 0;
}

// start the preview, or rewrite the beat while it keeps playing
static int preview(int ch, char *err, int errsize)
{
  int c, ok;
  if (!previewing)
  {
    if (isplaying()) stopsong();
    prevdata = songdata_save();
    if (!prevdata)
    {
      snprintf(err, errsize, "Out of memory");
      return 0;
    }
    memcpy(prevlen, pattlen, sizeof prevlen);
    if (!prevorder) prevorder = malloc(sizeof songorder);
    if (prevorder)
    {
      memcpy(prevorder, songorder, sizeof songorder);
      memcpy(prevsonglen, songlen, sizeof songlen);
      for (c = 0; c < MAX_CHN; c++) prevepnum[c] = editorUndoInfo.editorInfo[c].epnum;
    }
    for (c = 0; c < MAX_CHN; c++) prevmute[c] = chn[c].mute;
  }
  else
  {
    stopandwait();
    restoresong();
  }
  snd_lock();
  ok = bs_whole ? writesongbeat(ch, err, errsize) : writebeat(ch, err, errsize);
  for (c = 0; c < numchannels; c++)
    chn[c].mute = bs.beatonly ? (c != ch) : prevmute[c];
  snd_unlock();
  if (!ok)
  {
    if (previewing) stoppreview();
    else
    {
      restoresong();
      songdata_free(prevdata);
      prevdata = NULL;
    }
    return 0;
  }
  if (!previewing)
  {
    previewing = 1;
    if (bs_whole) initsong(editorInfo.esnum, PLAY_BEGINNING);
    else initsongpos(editorInfo.esnum, PLAY_PATTERN, 0);
  }
  // loading a library instrument stops the player: go on with the new sound
  else
  {
    // a new drum sound or a new dice roll: stop, then start again (clean
    // SID state: filter, envelopes)
    stopandwait();
    if (bs_whole) initsong(editorInfo.esnum, PLAY_BEGINNING);
    else initsongpos(editorInfo.esnum, PLAY_PATTERN, 0);
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Command line / tests
// ---------------------------------------------------------------------------

int beat_generate_cli(int chn, int genre, int sub, int var, int fill, char *err, int errsize)
{
  if ((genre < 0) || (genre >= numgenres()) || (sub < 0) || (sub >= numsubs(genre)) || (var < 0) || (var >= numvars(genre, sub)))
  {
    snprintf(err, errsize, "No such beat %d,%d,%d", genre, sub, var);
    return 0;
  }
  bs.genre = genre;
  bs.sub = sub;
  bs.var = var;
  bs.fill = fill;
  if (getenv("GG_BEATROWS")) bs.rows16 = atoi(getenv("GG_BEATROWS"));
  if (getenv("GG_BEATVARIATION")) bs.variation = atoi(getenv("GG_BEATVARIATION"));
  scanlibdrums();
  // GG_BEATLIB=n: the n-th library instrument (from 1) for every drum
  if (getenv("GG_BEATLIB"))
  {
    int r;
    for (r = 0; r < BR_NUM; r++) assign[r] = numlib[r] ? (atoi(getenv("GG_BEATLIB")) - 1) % numlib[r] : -1;
  }
  if (getenv("GG_BEATWHOLE"))
  {
    int r;
    bs_whole = 1;
    r = writesongbeat(chn, err, errsize);
    if (r) fprintf(stderr, "%s\n", ws_info);
    return r;
  }
  return writebeat(chn, err, errsize);
}

// all templates must parse (beat_selftest via GG_BEATTEST)
int beat_selftest(void)
{
  static BEATHIT h[MAXHITS];
  int g, s, v, steps, fail = 0, count = 0;
  for (g = 0; g < numgenres(); g++)
    for (s = 0; s < numsubs(g); s++)
    {
      if (beat_parse(beatcatalog[g].sub[s].fill, h, MAXHITS, &steps) < 0 || steps != 16)
      {
        printf("FAIL fill %s / %s\n", beatcatalog[g].name, beatcatalog[g].sub[s].name);
        fail++;
      }
      for (v = 0; v < numvars(g, s); v++)
      {
        count++;
        if (beat_parse(xv[g][s][v].grid, h, MAXHITS, &steps) <= 0)
        {
          printf("FAIL %s / %s / %s: %s\n", beatcatalog[g].name, beatcatalog[g].sub[s].name, xv[g][s][v].name, xv[g][s][v].grid);
          fail++;
        }
      }
    }
  printf("%d genres, %d beats, %d errors\n", numgenres(), count, fail);
  return !fail;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

// next / previous used instrument slot (0 = new) for the assignment list
static int nextslot(int cur, int dir)
{
  int i = cur;
  do
  {
    i += dir;
    if (i < 0) i = MAX_INSTR - 2;
    if (i > MAX_INSTR - 2) i = 0;
    if ((i == 0) || (!instrempty(i))) return i;
  } while (i != cur);
  return 0;
}

static void drawgrid(int x, int y, int w, int chn, int *usedroles)
{
  static BEATHIT hits[MAXHITS];
  static ROWEV ev[MAX_PATTROWS];
  int n, steps, k, r, s, line = 0;
  int r16 = currows16(chn);
  char buf[160];
  int used[BR_NUM];

  memset(used, 0, sizeof used);
  n = varianthits(hits, &steps);
  if (n < 0) n = 0;
  for (k = 0; k < n; k++) used[hits[k].role] = 1;

  // header: beat numbers
  printtext(x, y, MKCOL(14, C_PANEL), "Beat");
  for (s = 0; s < steps; s++)
  {
    buf[0] = (s % 4) ? '.' : '1' + (s / 4) % 4;
    buf[1] = 0;
    printtext(x + 8 + s * 2, y, MKCOL((s % 16) < 4 || ((s % 16) >= 8 && (s % 16) < 12) ? 14 : 15, C_PANEL), buf);
  }
  line = 1;
  for (r = BR_NUM - 1; r >= 0; r--)
  {
    static const int order[BR_NUM] = {BR_CHAT, BR_OHAT, BR_RIM, BR_TOML, BR_TOMM, BR_TOMH, BR_KICK, BR_CLAP, BR_SNARE, BR_CRASH};
    int role = order[r];
    if (!used[role]) continue;
    printtext(x, y + line, MKCOL(8, C_PANEL), beatrolenames[role]);
    for (s = 0; s < steps; s++)
    {
      int ch = '.', col = 11, lev = -1;
      for (k = 0; k < n; k++)
        if ((hits[k].role == role) && (hits[k].pos32 / 2 == s))
        {
          if (lev < hits[k].level) lev = hits[k].level;
          if (hits[k].pos32 & 1) ch = 'r';
        }
      if (lev >= 0)
      {
        if (ch != 'r') ch = lev == BL_ACCENT ? 'X' : (lev == BL_SOFT ? 'g' : 'x');
        col = lev == BL_ACCENT ? 15 : (lev == BL_SOFT ? 12 : 13);
      }
      buf[0] = ch;
      buf[1] = 0;
      printtext(x + 8 + s * 2, y + line, MKCOL(col, C_PANEL), buf);
    }
    line++;
  }
  // what the voice actually plays (one sound per row) for the first loop
  k = bs.fill;
  bs.fill = 0;
  r = render(ev, steps * r16 < MAX_PATTROWS ? steps * r16 : MAX_PATTROWS, r16);
  bs.fill = k;
  if (r)
  {
    printtext(x, y + line, MKCOL(14, C_PANEL), "Voice");
    for (s = 0; s < steps; s++)
    {
      int role = -1, rr;
      for (rr = s * r16; rr < (s + 1) * r16 && rr < MAX_PATTROWS; rr++)
        if ((ev[rr].role >= 0) && ((role < 0) || (roleprio[ev[rr].role] > roleprio[role]))) role = ev[rr].role;
      buf[0] = role >= 0 ? rolechars[role] : '-';
      buf[1] = 0;
      printtext(x + 8 + s * 2, y + line, MKCOL(role >= 0 ? 15 : 11, C_PANEL), buf);
    }
  }
  if (usedroles) memcpy(usedroles, used, sizeof used);
  (void)w;
}

void beatdialog(void)
{
  int w = 108, h = 38;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 2;
  int restoresel = -1;
  int ch = editorInfo.epchn;
  char msg[160] = "";
  int msgcol = 7;
  enum { S_OPTIONS = 10, S_ROLES = S_OPTIONS + BR_NUM, S_PREVIEW = S_ROLES, S_INSERT, S_CANCEL, S_NUM };

  editorInfo.editmode = EDIT_PATTERN;
  if (isplaying()) stopsong();
  scanlibdrums();
  key = 0;
  rawkey = 0;

  for (;;)
  {
    char buf[200], err[160];
    int c, hitrow = -1, hitbtn = -1, hitdice = 0, changed = 0, tgt = bs_whole ? ws_target(ch) : ch;
    int patt = chnpattern(ch), rows = pattlen[patt], r16 = currows16(ch), steps = 16;
    int used[BR_NUM];
    static BEATHIT tmp[MAXHITS];
    int btnx[3] = {x0 + 3, x0 + 17, x0 + w - 13};
    const char *btntext[3] = {previewing ? " " GS_STOP " Stop     " : " " GS_PLAY " Preview  ", " Insert ", "  Cancel  "};

    beat_parse(curvar()->grid, tmp, MAXHITS, &steps);
    printstatus();
    dlg_drawframe(x0, y0, w, h, "Beat editor");

    // options (left)
    for (c = 0; c < S_OPTIONS; c++)
    {
      int isel = (c == sel), bg = isel ? C_SELECT : C_PANEL, ry = y0 + 2 + c;
      switch (c)
      {
        case 0: sprintf(buf, "Genre        " GS_PREV " %s " GS_NEXT "  %d/%d", beatcatalog[bs.genre].name, bs.genre + 1, numgenres()); break;
        case 1: sprintf(buf, "Subgenre     " GS_PREV " %s " GS_NEXT "  %d/%d", cursub()->name, bs.sub + 1, numsubs(bs.genre)); break;
        case 2:
          if (israndom) snprintf(buf, sizeof buf, "Beat         " GS_PREV " %.40s " GS_NEXT "  (dice)", curvar()->name);
          else snprintf(buf, sizeof buf, "Beat         " GS_PREV " %.40s " GS_NEXT "  %d/%d", curvar()->name, bs.var + 1, numvars(bs.genre, bs.sub));
          break;
        case 3:
          if (bs.kit < 0) sprintf(buf, "Drum sounds  " GS_PREV " Auto (%s) " GS_NEXT, kitnames[cursub()->kit]);
          else sprintf(buf, "Drum sounds  " GS_PREV " %s " GS_NEXT, kitnames[bs.kit]);
          break;
        case 4:
          if (bs.rows16) sprintf(buf, "Rows / 16th  " GS_PREV " %d " GS_NEXT, bs.rows16);
          else sprintf(buf, "Rows / 16th  " GS_PREV " Auto (%d, tempo %d) " GS_NEXT, autorows16(ch), songtempo(ch));
          break;
        case 5:
          if (bs.swing < 0) sprintf(buf, "Swing        " GS_PREV " Beat (%s) " GS_NEXT, curvar()->swing > 50 ? "shuffle" : "straight");
          else if (bs.swing == 50) sprintf(buf, "Swing        " GS_PREV " Straight " GS_NEXT);
          else sprintf(buf, "Swing        " GS_PREV " %d%% " GS_NEXT, bs.swing);
          if ((curswing() > 50) && (r16 < 2)) strcat(buf, " (needs 2+ rows)");
          break;
        case 6: sprintf(buf, "Fill         " GS_PREV " %s " GS_NEXT, bs.fill ? (bs_whole ? "Before every new part" : "In the last bar") : "Off"); break;
        case 7:
          if (bs.variation) sprintf(buf, "Variation    " GS_PREV " #%d " GS_NEXT "  (R = new)", bs.variation);
          else sprintf(buf, "Variation    " GS_PREV " Original " GS_NEXT "  (R = new)");
          break;
        case 8: sprintf(buf, "Preview      " GS_PREV " %s " GS_NEXT, bs.beatonly ? "Beat only" : "With the other voices"); break;
        case 9: sprintf(buf, "Length       " GS_PREV " %s " GS_NEXT, bs_whole ? "Whole song (follows its parts)" : "This pattern"); break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), 58);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + 60)) hitrow = c;
    }

    // dice button: a new random beat of the subgenre (D)
    printtext(x0 + 55, y0 + 4, MKCOL(C_BUTTONTEXT, C_BUTTON), " " GS_DICE GS_DICER " ");
    if ((mousey == y0 + 4) && (mousex >= x0 + 55) && (mousex < x0 + 59)) hitdice = 1;

    // grid
    drawgrid(x0 + 3, y0 + 13, w - 6, ch, used);

    // instruments (right)
    printtext(x0 + 63, y0 + 2 - 1 + 0, MKCOL(14, C_PANEL), "");
    for (c = 0; c < BR_NUM; c++)
    {
      int s = S_OPTIONS + c, isel = (s == sel), bg = isel ? C_SELECT : C_PANEL, ry = y0 + 2 + c;
      char nm[MAX_INSTRNAMELEN + 1];
      (void)nm;
      if ((assign[c] >= 0) && (assign[c] < numlib[c]))
        sprintf(buf, "%-6s " GS_PREV " Lib %.18s " GS_NEXT " %d/%d", beatrolenames[c], libdrums[c][assign[c]].name, assign[c] + 1, numlib[c]);
      else sprintf(buf, "%-6s " GS_PREV " Kit: %s %s " GS_NEXT " (%d in lib)", beatrolenames[c], beatrolenames[c], kitnames[curkit()], numlib[c]);
      printblankc(x0 + 62, ry, MKCOL(8, bg), w - 64);
      printtext(x0 + 63, ry, MKCOL(isel ? 15 : (used[c] ? 8 : 3), bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 62) && (mousex < x0 + w - 2)) hitrow = s;
    }

    // target
    {
      int y = y0 + 27;
      int uses = patternuses(patt);
      printtext(x0 + 3, y, MKCOL(14, C_PANEL), "Target:");
      if (bs_whole)
      {
        char e2[160];
        if (ws_analyse(tgt, e2, sizeof e2))
        {
          snprintf(buf, sizeof buf, "Channel %d.%d%s, the whole subtune %02X: %d bars. %s", tgt / 3 + 1, tgt % 3 + 1,
                   tgt != ch ? " (a free voice)" : "", editorInfo.esnum, (ws_total + ws_barrows - 1) / ws_barrows,
                   ws_empty(tgt) ? "The voice is empty." : "The notes of the voice are replaced.");
          printtext(x0 + 12, y, MKCOL(8, C_PANEL), buf);
          printtext(x0 + 12, y + 1, MKCOL(13, C_PANEL), ws_info);
        }
        else dlg_printwrapped(x0 + 12, y, w - 15, 2, MKCOL(10, C_PANEL), e2);
      }
      else
      {
      sprintf(buf, "Channel %d.%d, pattern %02X: %d rows = %d.%d bars (%d rows per 16th). The whole pattern is replaced.",
              ch / 3 + 1, ch % 3 + 1, patt, rows, rows / (16 * r16), (rows % (16 * r16)) * 4 / (16 * r16), r16);
      printtext(x0 + 12, y, MKCOL(8, C_PANEL), buf);
      if (uses > 1)
      {
        sprintf(buf, "The pattern is used %d times in the orderlist: the beat changes in all places.", uses);
        printtext(x0 + 12, y + 1, MKCOL(10, C_PANEL), buf);
      }
      }
      printtext(x0 + 3, y + 2, MKCOL(3, C_PANEL),
                "x hit  X accent  g ghost  r roll. One sound per row: crash > snare > clap > kick > toms > rim > hats.");
      if (msg[0]) printtext(x0 + 3, y + 4, MKCOL(msgcol, C_PANEL), msg);
    }

    for (c = 0; c < 3; c++)
    {
      int isel = (sel == S_PREVIEW + c);
      printtext(btnx[c], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), btntext[c]);
      if ((mousey == y0 + h - 2) && (mousex >= btnx[c]) && (mousex < btnx[c] + (int)strlen(btntext[c]))) hitbtn = c;
    }
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL),
              "UP/DOWN select, LEFT/RIGHT change, SPACE/F1 preview, D dice (new beat), R variation, ENTER insert, ESC");

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
      stoppreview();
      return;
    }

    if ((mouseb) && (!prevmouseb))
    {
      if (hitdice)
      {
        rawkey = KEY_D;
        key = 0;
      }
      else if (hitbtn >= 0)
      {
        restoresel = sel;
        sel = S_PREVIEW + hitbtn;
        rawkey = KEY_ENTER;
      }
      else if (hitrow >= 0)
      {
        sel = hitrow;
        rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        if (ui_arrowat(mousex, mousey) < 0) rawkey = KEY_LEFT;
        if (ui_arrowat(mousex, mousey) > 0) rawkey = KEY_RIGHT;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h))
      {
        stoppreview();
        break;
      }
    }

    if ((rawkey == KEY_F1) || (rawkey == KEY_SPACE))
    {
      sel = sel >= S_PREVIEW ? S_PREVIEW : sel;
      if (previewing) stoppreview();
      else if (preview(tgt, err, sizeof err)) msg[0] = 0;
      else { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
      continue;
    }
    if (rawkey == KEY_D)
    {
      static unsigned diceseed = 0;
      if (!diceseed) diceseed = (unsigned)time(NULL);
      diceseed = diceseed * 69069u + 1u;
      randombeat(bs.genre, bs.sub, diceseed % 100000);
      bs.variation = 0;
      sel = 2;
      changed = 1;
    }
    if (rawkey == KEY_R)
    {
      bs.variation = (bs.variation % 999) + 1 + (rand() % 50);
      changed = 1;
    }

    switch (rawkey)
    {
      case KEY_ESC:
      stoppreview();
      goto done;

      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_DOWN:
      case KEY_TAB:
      if (sel < S_NUM - 1) sel++;
      break;

      case KEY_PGUP:
      case KEY_PGDN:
      israndom = 0;
      bs.var = (bs.var + (rawkey == KEY_PGDN ? 1 : -1) + numvars(bs.genre, bs.sub)) % numvars(bs.genre, bs.sub);
      bs.variation = 0;
      changed = 1;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      {
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        if (sel >= S_PREVIEW)
        {
          sel += dir;
          if (sel < S_PREVIEW) sel = S_PREVIEW;
          if (sel >= S_NUM) sel = S_NUM - 1;
          break;
        }
        changed = 1;
        switch (sel)
        {
          case 0:
          israndom = 0;
          bs.genre = (bs.genre + dir + numgenres()) % numgenres();
          bs.sub = 0;
          bs.var = 0;
          bs.variation = 0;
          break;
          case 1:
          israndom = 0;
          bs.sub = (bs.sub + dir + numsubs(bs.genre)) % numsubs(bs.genre);
          bs.var = 0;
          bs.variation = 0;
          break;
          case 2:
          if (israndom) israndom = 0;
          else bs.var = (bs.var + dir + numvars(bs.genre, bs.sub)) % numvars(bs.genre, bs.sub);
          bs.variation = 0;
          break;
          case 3:
          bs.kit += dir;
          if (bs.kit < -1) bs.kit = KIT_NUM - 1;
          if (bs.kit >= KIT_NUM) bs.kit = -1;
          break;
          case 4:
          bs.rows16 = (bs.rows16 + dir + 5) % 5;
          break;
          case 5:
          {
            int i;
            for (i = 0; i < NUMSWING; i++)
              if (swingvalues[i] == bs.swing) break;
            i = (i + dir + NUMSWING) % NUMSWING;
            bs.swing = swingvalues[i];
          }
          break;
          case 6: bs.fill ^= 1; break;
          case 7:
          bs.variation += dir;
          if (bs.variation < 0) bs.variation = 0;
          break;
          case 8: bs.beatonly ^= 1; break;
          case 9:
          if (previewing) stoppreview();
          bs_whole ^= 1;
          if (bs_whole) { bs.fill = 1; bs.beatonly = 0; }
          break;
          default:
          if ((sel >= S_OPTIONS) && (sel < S_ROLES))
          {
            int r = sel - S_OPTIONS, n = numlib[r] + 1, step = shiftpressed ? 10 : 1;
            // -1 = kit sound, 0 ... n-2 = library instruments
            assign[r] = ((assign[r] + 1 + dir * step) % n + n) % n - 1;
          }
          break;
        }
      }
      break;

      case KEY_ENTER:
      if (sel == S_CANCEL)
      {
        stoppreview();
        goto done;
      }
      if (sel == S_PREVIEW)
      {
        if (previewing) stoppreview();
        else if (!preview(tgt, err, sizeof err)) { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
        break;
      }
      // insert (one undo step)
      {
        void *ed;
        int r;
        stoppreview();
        ed = undoinstrbegin();
        undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
        if (bs_whole)
        {
          int c2;
          for (c2 = 0; c2 < numchannels; c2++)
            undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c2 + editorInfo.esnum * MAX_CHN, UNDO_AREA_DIRTY_CHECK);
          undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
        }
        snd_lock();
        r = bs_whole ? writesongbeat(tgt, err, sizeof err) : writebeat(ch, err, sizeof err);
        snd_unlock();
        undoinstrend(ed);
        if (!r)
        {
          snprintf(msg, sizeof msg, "%s", err);
          msgcol = 10;
          break;
        }
        songmodified = 1;
        if (bs_whole) snprintf(buf, sizeof buf, "Beat \"%s / %s\" for the whole song written to channel %d.%d (CTRL+D)", cursub()->name,
                               curvar()->name, tgt / 3 + 1, tgt % 3 + 1);
        else snprintf(buf, sizeof buf, "Beat \"%s / %s\" written to channel %d.%d, pattern %02X (CTRL+D)", cursub()->name, curvar()->name,
                 ch / 3 + 1, ch % 3 + 1, patt);
        setstatusmessage(buf);
        goto done;
      }
    }
    if ((changed) && (previewing))
    {
      if (!preview(tgt, err, sizeof err)) { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
    }
  }
done:
  key = 0;
  rawkey = 0;
  printmainscreen();
}

// C64 GrooveGoat - SIDGen timbre: similarity of instruments from measured
// spectra, alternatives, placing six voices on two SIDs (port of SIDGen
// timbre.py by IT-DUMMKOPFEN)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gsgcomp.h"
#include "gsgtimbre.h"

typedef struct { char name[48]; double v[32]; int n; } SPEC;
static SPEC *spec = NULL;
static int numspec = 0;
static int refbass = 33, refother = 57;

static int cmpspec(const void *a, const void *b) { return strcmp(((const SPEC *)a)->name, ((const SPEC *)b)->name); }

static void tbinit(void)
{
  int len = 0, k, cap = 0;
  char *t, *line, *next;
  if (spec) return;
  for (k = 0; sg_spectratext[k]; k++) len += strlen(sg_spectratext[k]);
  t = (char *)malloc(len + 1);
  t[0] = 0;
  for (k = 0; sg_spectratext[k]; k++) strcat(t, sg_spectratext[k]);
  for (line = t; *line; line = next)
  {
    char *tab, *p;
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    else next = line + strlen(line);
    tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = 0;
    if (numspec >= cap) spec = (SPEC *)realloc(spec, sizeof(SPEC) * (cap = cap * 2 + 128));
    snprintf(spec[numspec].name, 48, "%s", line);
    spec[numspec].n = 0;
    for (p = tab + 1; *p && spec[numspec].n < 32;)
    {
      spec[numspec].v[spec[numspec].n++] = strtod(p, &p);
      if (*p == ',') p++;
      else break;
    }
    numspec++;
  }
  free(t);
  if (!spec) spec = (SPEC *)calloc(1, sizeof(SPEC));
  qsort(spec, numspec, sizeof(SPEC), cmpspec);
  {
    const char *b = strstr(sg_refpitch, "bass="), *o = strstr(sg_refpitch, "other=");
    if (b) refbass = atoi(b + 5);
    if (o) refother = atoi(o + 6);
  }
}

static const SPEC *findspec(const char *name)
{
  SPEC key;
  snprintf(key.name, 48, "%s", name);
  return (const SPEC *)bsearch(&key, spec, numspec, sizeof(SPEC), cmpspec);
}

int tb_isdrum(const char *cat)
{
  static const char *d[] = {"kick", "snare", "clap", "hat", "ohat", "ride", "crash", "tom", "perc", "fx"};
  int k;
  for (k = 0; k < 10; k++)
    if (!strcmp(cat, d[k])) return 1;
  return 0;
}

static const char *basename_(const char *n)
{
  return !strncmp(n, "Echo ", 5) ? n + 5 : n;
}

static double pyround(double x)
{
  double f = floor(x), d = x - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return fmod(f, 2.0) == 0 ? f : f + 1;          // half to even, as Python's round()
}

static int profile(const char *name, double shift, double *out)
{
  const SPEC *s = findspec(name);
  int n, i;
  double top, m = 0, norm = 0;
  if (!s) return 0;
  n = s->n;
  for (i = 0; i < n; i++)
  {
    double x = i - shift, f;
    int k = (int)floor(x), ka, kb;
    double a, b;
    f = x - k;
    ka = k < 0 ? 0 : k > n - 1 ? n - 1 : k;
    kb = k + 1 < 0 ? 0 : k + 1 > n - 1 ? n - 1 : k + 1;
    a = s->v[ka];
    b = s->v[kb];
    out[i] = a + (b - a) * f;
  }
  top = out[0];
  for (i = 1; i < n; i++)
    if (out[i] > top) top = out[i];
  for (i = 0; i < n; i++)
  {
    if (out[i] < top - 40) out[i] = top - 40;
    m += out[i];
  }
  m /= n;
  for (i = 0; i < n; i++)
  {
    out[i] -= m;
    norm += out[i] * out[i];
  }
  norm = sqrt(norm);
  if (norm == 0) norm = 1.0;
  for (i = 0; i < n; i++) out[i] /= norm;
  return n;
}

static int refpitch(const SGINS *i, int *ref)
{
  if (tb_isdrum(i->cat)) return 0;
  *ref = ((!strcmp(i->cat, "bass")) || (!strcmp(i->cat, "kickbass"))) ? refbass : refother;
  return 1;
}

static int spectral(const SGINS *a, const SGINS *b, int pa, int pb, double *out)
{
  static const double BAND = 3.7;
  double band = 12 * log2(pow(10000 / 60.0, 1 / 24.0));
  double sa = 0, sb = 0, A[32], B[32], s = 0;
  int ra, rb, na, nb, k;
  (void)BAND;
  if (refpitch(a, &ra)) sa = pyround((pa - ra) / band * 2) / 2.0;
  if (refpitch(b, &rb)) sb = pyround((pb - rb) / band * 2) / 2.0;
  na = profile(a->name, sa, A);
  nb = profile(b->name, sb, B);
  if ((!na) || (!nb)) return 0;
  for (k = 0; (k < na) && (k < nb); k++) s += A[k] * B[k];
  *out = s;
  return 1;
}

double tb_similarity(const SGINS *a, const SGINS *b, int pa, int pb)
{
  int reg = abs(pa - pb);
  double c, rf;
  tbinit();
  if (((!strcmp(basename_(a->name), basename_(b->name))) || (!strcmp(a->name, b->name))) && (!tb_isdrum(a->cat)))
    return reg <= 7 ? 1.0 : (reg <= 14 ? 0.75 : 0.4);
  if (!spectral(a, b, pa, pb, &c)) c = sg_timbrewave(a) == sg_timbrewave(b) ? 0.5 : 0.2;
  if (c < 0) c = 0;
  rf = reg <= 7 ? 1.0 : (reg <= 14 ? 0.6 : 0.2);
  if ((tb_isdrum(a->cat)) && (tb_isdrum(b->cat))) rf = 1.0;
  return c * c * rf;
}

double tb_similarity_names(const char *a, const char *b)
{
  const SGINS *ia = sg_findins(a), *ib = sg_findins(b);
  if ((!ia) || (!ib)) return 0;
  return tb_similarity(ia, ib, 60, 60);
}

static int inlist(const char *s, const char **l, int n)
{
  int k;
  for (k = 0; k < n; k++)
    if (!strcmp(s, l[k])) return 1;
  return 0;
}

int tb_alternatives(const char *name, const char **exclude, int nex, const char **out, int max)
{
  const SGINS *b = sg_findins(name), *i;
  int n = 0, k, wave, nh = sg_handcount();
  static const char *pref[] = {"Synth", "Piano", "Mallet", "Zupf", "Flaeche", "Chor"};
  if (!b) return 0;
  wave = sg_timbrewave(b);
  for (k = 0; (k < nh) && (n < max); k++)
  {
    i = sg_hand(k);
    if ((strcmp(i->cat, b->cat)) || (strcmp(i->fam, b->fam)) || (!strcmp(i->name, b->name)) || (inlist(i->name, exclude, nex))) continue;
    out[n++] = i->name;
  }
  if (n < 3)
  {
    for (k = 0; (k < nh) && (n < max); k++)
    {
      i = sg_hand(k);
      if ((strcmp(i->cat, b->cat)) || (!strcmp(i->name, b->name)) || (inlist(i->name, exclude, nex))) continue;
      if (inlist(i->name, out, n)) continue;
      if (!inlist(i->fam, pref, 6)) continue;
      out[n++] = i->name;
    }
  }
  // sorted by (same waveform, name): stable insertion sort
  for (k = 1; k < n; k++)
  {
    const char *x = out[k];
    int wx = sg_timbrewave(sg_findins(x)) == wave, j = k - 1;
    while (j >= 0)
    {
      int wj = sg_timbrewave(sg_findins(out[j])) == wave;
      if ((wj > wx) || ((wj == wx) && (strcmp(out[j], x) > 0))) { out[j + 1] = out[j]; j--; }
      else break;
    }
    out[j + 1] = x;
  }
  return n;
}

const char *tb_distinct(const char *name, const char **used, int nused, int k)
{
  static const char *alts[512];
  double sim[512];
  int n, a, z;
  if (!inlist(name, used, nused)) return name;
  n = tb_alternatives(name, used, nused, alts, 512);
  if (!n) return name;
  for (a = 0; a < n; a++)
  {
    double m = 0;
    int u;
    const SGINS *ia = sg_findins(alts[a]);
    for (u = 0; u < nused; u++)
    {
      const SGINS *iu = sg_findins(used[u]);
      if ((ia) && (iu))
      {
        double s = tb_similarity(ia, iu, 60, 60);
        if (s > m) m = s;
      }
    }
    sim[a] = m;
  }
  // stable sort by similarity
  for (a = 1; a < n; a++)
  {
    const char *x = alts[a];
    double sx = sim[a];
    z = a - 1;
    while ((z >= 0) && (sim[z] > sx)) { alts[z + 1] = alts[z]; sim[z + 1] = sim[z]; z--; }
    alts[z + 1] = x;
    sim[z + 1] = sx;
  }
  return alts[k < n - 1 ? k : n - 1];
}

// ---------------------------------------------------------------------------
// Placing six voices on two SIDs
// ---------------------------------------------------------------------------

static void sounding(const SGTRACK *t, int total, short *ins, signed char *pitch)
{
  int r, k;
  for (r = 0; r < total; r++) { ins[r] = -1; pitch[r] = 0; }
  for (r = 0; r < t->rows && r < total; r++)
  {
    const SGEV *e = &t->ev[r];
    int d, nxt;
    if (e->type != EV_NOTE) continue;
    d = e->dur > 0 ? e->dur : 2;
    nxt = r + 1;
    while ((nxt < t->rows) && (t->ev[nxt].type == EV_NONE)) nxt++;
    if (nxt >= t->rows) nxt = total;
    for (k = r; (k < r + d) && (k < nxt) && (k < total); k++)
    {
      ins[k] = e->ins;
      pitch[k] = e->note;
    }
  }
}

static int hasfilter(COMP *c, const SGTRACK *t)
{
  int r;
  for (r = 0; r < t->rows; r++)
    if (t->ev[r].type == EV_NOTE)
    {
      const char *f = c->ins[t->ev[r].ins].filt;
      if ((f[0]) && (strcmp(f, "off"))) return 1;
    }
  return 0;
}

void tb_place_tracks(COMP *c, SGTRACK *tracks, int total, int fixed_empty, int *order, double *before, double *after)
{
  double M[6][6], best = 0;
  int filt[6], a, b, n = 6, havebest = 0, g1best[3] = {0, 1, 2};
  short *ins[6];
  signed char *pitch[6];
  static double simcache[SG_MAXINS][SG_MAXINS][1];
  (void)simcache;
  tbinit();
  for (a = 0; a < n; a++)
  {
    ins[a] = (short *)malloc(sizeof(short) * (total + 1));
    pitch[a] = (signed char *)malloc(total + 1);
    sounding(&tracks[a], total, ins[a], pitch[a]);
    filt[a] = hasfilter(c, &tracks[a]);
  }
  for (a = 0; a < n; a++)
    for (b = 0; b < n; b++) M[a][b] = 0;
  for (a = 0; a < n; a++)
    for (b = a + 1; b < n; b++)
    {
      double s = 0;
      int r;
      for (r = 0; r < total; r++)
        if ((ins[a][r] >= 0) && (ins[b][r] >= 0))
          s += tb_similarity(&c->ins[ins[a][r]], &c->ins[ins[b][r]], pitch[a][r], pitch[b][r]);
      M[a][b] = M[b][a] = s / (total > 1 ? total : 1);
    }
  {
#define COST3(x, y, z) (M[x][y] + M[x][z] + M[y][z] + ((filt[x] + filt[y] + filt[z]) > 1 ? 10.0 : 0.0))
    int i, j, k;
    *before = COST3(0, 1, 2) + COST3(3, 4, 5);
    // combinations(range(6), 3) in Python's order
    for (i = 0; i < n; i++)
      for (j = i + 1; j < n; j++)
        for (k = j + 1; k < n; k++)
        {
          int g2[3], m = 0, x;
          double cc;
          if (i != 0) continue;                      // mirror images left out
          if ((fixed_empty >= 0) && (i != fixed_empty) && (j != fixed_empty) && (k != fixed_empty)) continue;
          for (x = 0; x < n; x++)
            if ((x != i) && (x != j) && (x != k)) g2[m++] = x;
          cc = COST3(i, j, k) + COST3(g2[0], g2[1], g2[2]);
          cc += 0.0001 * ((i >= 3) + (j >= 3) + (k >= 3));
          if ((i != 1) && (j != 1) && (k != 1)) cc += 0.02;
          if ((!havebest) || (cc < best - 1e-9))
          {
            havebest = 1;
            best = cc;
            g1best[0] = i; g1best[1] = j; g1best[2] = k;
          }
        }
#undef COST3
  }
  {
    int g2[3], m = 0, x;
    for (x = 0; x < n; x++)
      if ((x != g1best[0]) && (x != g1best[1]) && (x != g1best[2])) g2[m++] = x;
    if (fixed_empty >= 0)
    {
      int g[3], gn = 0;
      for (x = 0; x < 3; x++)
        if (g1best[x] != fixed_empty) g[gn++] = g1best[x];
      g[2] = fixed_empty;
      order[0] = g[0]; order[1] = g[1]; order[2] = g[2];
    }
    else
    {
      order[0] = g1best[0]; order[1] = g1best[1]; order[2] = g1best[2];
    }
    order[3] = g2[0]; order[4] = g2[1]; order[5] = g2[2];
  }
  *after = best;
  for (a = 0; a < n; a++)
  {
    free(ins[a]);
    free(pitch[a]);
  }
}

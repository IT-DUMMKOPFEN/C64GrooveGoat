// C64 GrooveGoat - playing technique: register arc, glides, acid
// slides, scoops, fall-offs, delayed vibrato, filter sweeps, and the
// arrangement ideas (key change, stop before a chorus)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gsgcomp.h"
#include "gsgtimbre.h"
#include "gsgexpr.h"

static const unsigned char FREQ_LO[96] = {
  0x17, 0x27, 0x39, 0x4b, 0x5f, 0x74, 0x8a, 0xa1, 0xba, 0xd4, 0xf0, 0x0e, 0x2d, 0x4e, 0x71, 0x96, 0xbe, 0xe8, 0x14, 0x43, 0x74, 0xa9, 0xe1, 0x1c,
  0x5a, 0x9c, 0xe2, 0x2d, 0x7c, 0xcf, 0x28, 0x85, 0xe8, 0x52, 0xc1, 0x37, 0xb4, 0x39, 0xc5, 0x5a, 0xf7, 0x9e, 0x4f, 0x0a, 0xd1, 0xa3, 0x82, 0x6e,
  0x68, 0x71, 0x8a, 0xb3, 0xee, 0x3c, 0x9e, 0x15, 0xa2, 0x46, 0x04, 0xdc, 0xd0, 0xe2, 0x14, 0x67, 0xdd, 0x79, 0x3c, 0x29, 0x44, 0x8d, 0x08, 0xb8,
  0xa1, 0xc5, 0x28, 0xcd, 0xba, 0xf1, 0x78, 0x53, 0x87, 0x1a, 0x10, 0x71, 0x42, 0x89, 0x4f, 0x9b, 0x74, 0xe2, 0xf0, 0xa6, 0x0e, 0x33, 0x20, 0xff};
static const unsigned char FREQ_HI[96] = {
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04,
  0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x1f, 0x20, 0x22, 0x24, 0x27, 0x29, 0x2b, 0x2e, 0x31, 0x34, 0x37, 0x3a, 0x3e, 0x41,
  0x45, 0x49, 0x4e, 0x52, 0x57, 0x5c, 0x62, 0x68, 0x6e, 0x75, 0x7c, 0x83, 0x8b, 0x93, 0x9c, 0xa5, 0xaf, 0xb9, 0xc4, 0xd0, 0xdd, 0xea, 0xf8, 0xff};

int sg_freq(int n)
{
  if (n < 0) n = 0;
  if (n > 95) n = 95;
  return FREQ_LO[n] | (FREQ_HI[n] << 8);
}

int sg_quant_speed(double vv)
{
  int v = (int)(vv + 0.999), e, m, bl = 0;
  if (v < 1) v = 1;
  if (v <= 8) return v;
  while ((v >> bl) != 0) bl++;
  e = bl - 3;
  m = (v + (1 << e) - 1) >> e;
  v = m << e;
  return v > 0x7fff ? 0x7fff : v;
}

static SGCMD spdcmd(int cmd, double v)
{
  SGCMD c;
  int q = sg_quant_speed(v);
  memset(&c, 0, sizeof c);
  c.cmd = cmd;
  c.kind = CD_SPD;
  c.a = (q >> 8) & 0x7f;
  c.b = q & 0xff;
  return c;
}

static const char *MELODIC[] = {"lead", "pad", "echo"};

static int ismelodic(const char *cat)
{
  return (!strcmp(cat, "lead")) || (!strcmp(cat, "pad")) || (!strcmp(cat, "echo"));
}

int sg_slide_ok(const SGINS *i)
{
  int k, last = -1, havelast = 0;
  (void)MELODIC;
  if (!i) return 0;
  if ((!ismelodic(i->cat)) && (strcmp(i->cat, "bass"))) return 0;
  for (k = 0; k < i->nw; k++)
  {
    int l = i->wl[k], r = i->wr[k];
    if (l == 0xff)
    {
      if (r != 0)
      {
        int a = r - 1 > 0 ? r - 1 : 0, j;
        for (j = a; j < k; j++)
          if (!((i->wr[j] == 0x80) || (i->wl[j] >= 0xf0))) return 0;
      }
      break;
    }
    if ((l < 0xf0) && (r != 0x80)) { last = r; havelast = 1; }
  }
  return (!havelast) || (last == 0);
}

// ---------------------------------------------------------------------------
// Filter sweeps: programs registered by name
// ---------------------------------------------------------------------------

void sg_sweep_name(const char *kind, double framesd, int model, char *out, int size)
{
  int frames = (int)framesd / 64 * 64, l[64], r[64], n = 0, typ, c0, c1;
  int lp[2], hp[2], bp[2];
  if (frames < 64) frames = 64;
  if (frames > 4096) frames = 4096;
  snprintf(out, size, "x_%s_%d_%d", kind, frames, model);
  if (sg_findfilter(out)) return;
  if (model == 8580) { lp[0] = 0x06; lp[1] = 0x90; hp[0] = 0x04; hp[1] = 0x70; bp[0] = 0x08; bp[1] = 0xa0; }
  else { lp[0] = 0x0c; lp[1] = 0x58; hp[0] = 0x04; hp[1] = 0x40; bp[0] = 0x10; bp[1] = 0x60; }
  typ = !strcmp(kind, "hp_up") ? 0xc0 : ((!strcmp(kind, "bp_up")) || (!strcmp(kind, "bp_breathe"))) ? 0xa0 : 0x90;
  if (strstr(kind, "breathe"))
  {
    int half, span, sp, t;
    if (!strncmp(kind, "lp", 2)) { c0 = lp[0]; c1 = lp[1]; }
    else { c0 = bp[0]; c1 = bp[1]; }
    half = frames / 8;
    if (half < 16) half = 16;
    if (half > 127) half = 127;
    span = (c1 - c0) / 2;
    if (span < 1) span = 1;
    sp = span >= half ? span / half : 1;
    if (sp < 1) sp = 1;
    t = span / sp;
    if (t > 127) t = 127;
    l[n] = typ; r[n++] = 0x100 | 0xb;
    l[n] = 0x00; r[n++] = c0 + 2;
    l[n] = t; r[n++] = sp & 0xff;
    l[n] = t; r[n++] = (-sp) & 0xff;
    l[n] = 0xff; r[n++] = 3;
  }
  else
  {
    int up;
    if (!strcmp(kind, "lp_up")) { c0 = lp[0]; c1 = lp[1]; }
    else if (!strcmp(kind, "hp_up")) { c0 = hp[0]; c1 = hp[1]; }
    else { c0 = bp[0]; c1 = bp[1]; }
    up = c1 - c0;
    l[n] = typ; r[n++] = 0x100 | (strcmp(kind, "hp_up") ? 0xc : 0x8);
    l[n] = 0x00; r[n++] = c0;
    if (up >= frames)
    {
      int sp = (up + frames - 1) / frames, left = up;
      while ((left > 0) && (n < 60))
      {
        int t = (left + sp - 1) / sp;
        if (t > 127) t = 127;
        l[n] = t; r[n++] = sp;
        left -= t * sp;
      }
    }
    else
    {
      int hold = frames - up, pairs, k;
      pairs = (up + 126) / 127;
      if ((hold + 126) / 127 > pairs) pairs = (hold + 126) / 127;
      if (pairs < 1) pairs = 1;
      if (pairs > 12) pairs = 12;
      for (k = 0; k < pairs; k++)
      {
        int u = up * (k + 1) / pairs - up * k / pairs, h = hold * (k + 1) / pairs - hold * k / pairs;
        if (u) { l[n] = u > 127 ? 127 : u; r[n++] = 1; }
        if (h) { l[n] = h > 127 ? 127 : h; r[n++] = 0; }
      }
    }
    l[n] = 0xff; r[n++] = 0;
  }
  sg_addfilter(out, l, r, n);
}

// a lowpass from cutoff c0 to c1 in 'frames' frames (c0 == c1: it stays there)
static void sg_ramp_name(int c0, int c1, double framesd, char *out, int size)
{
  int frames = (int)framesd / 64 * 64, l[64], r[64], n = 0, up = c1 - c0;
  if (frames < 64) frames = 64;
  if (frames > 4096) frames = 4096;
  if (up < 0) up = 0;
  snprintf(out, size, "x_rmp_%02x_%02x_%d", c0, c0 + up, up ? frames : 0);
  if (sg_findfilter(out)) return;
  l[n] = 0x90; r[n++] = 0x100 | 0x4;
  l[n] = 0x00; r[n++] = c0;
  if (up)
  {
    // 'up' steps of +1 spread over the frames (pairs of step and hold)
    int hold = frames - up, pairs, k;
    if (hold < 0) hold = 0;
    pairs = (up + 126) / 127;
    if ((hold + 126) / 127 > pairs) pairs = (hold + 126) / 127;
    if (pairs < 1) pairs = 1;
    if (pairs > 12) pairs = 12;
    for (k = 0; k < pairs; k++)
    {
      int u = up * (k + 1) / pairs - up * k / pairs, h = hold * (k + 1) / pairs - hold * k / pairs;
      while ((u > 0) && (n < 58)) { int t = u > 127 ? 127 : u; l[n] = t; r[n++] = 1; u -= t; }
      while ((h > 0) && (n < 58)) { int t = h > 127 ? 127 : h; l[n] = t; r[n++] = 0; h -= t; }
    }
  }
  l[n] = 0xff; r[n++] = 0;
  sg_addfilter(out, l, r, n);
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

typedef struct
{
  double glide, scoop, fall, vib, bslide, sweep;
  double arc[3];
  const char *sweeps[4];
  int nsweeps;
  double glide_rows[3];
  int nglide;
} PROF;

typedef struct { const char *key; PROF p; int set[16]; } PROFDEF;

static void profile(const char *style, const JV *extra, PROF *p)
{
  // BASE, then the style's profile, then the style's own 'expr' settings
  static const struct
  {
    const char *k;
    double glide, scoop, fall, vib, bslide, sweep;   // -1 = from BASE
    double a0, a1, a2;
    const char *sw[4];
    double gr[3];
  } T[] = {
    {"acid", .25, -1, .05, .2, .40, .65, -1, -1, -1, {"lp_up", "hp_up", "lp_up", 0}, {-1}},
    {"techno", .18, -1, .05, .2, .18, .70, -1, -1, -1, {"lp_up", "hp_up", "bp_up", 0}, {-1}},
    {"deeptechno", .15, -1, .04, .2, .12, .75, -1, -1, -1, {"lp_up", "lp_breathe", "bp_up", 0}, {-1}},
    {"trance", .30, .12, -1, .45, .06, .80, 4, 4, 2, {"lp_up", "hp_up", "lp_up", "bp_up"}, {.5, 1.0, 1.5}},
    {"deeptrance", .30, .10, -1, .45, .06, .80, 4, 5, 1, {"lp_up", "lp_breathe", "hp_up", 0}, {-1}},
    {"house", .18, .08, -1, .3, .10, .65, -1, -1, -1, {"lp_up", "lp_up", "hp_up", 0}, {-1}},
    {"eurodance", .20, .10, -1, .4, .06, .55, 5, 4, 2, {0}, {-1}},
    {"synthwave", .30, .10, .15, .55, -1, .45, 4, 5, 1, {"lp_up", "lp_breathe", 0}, {1.0, 1.5, -1}},
    {"synthpop", .22, .08, .12, .45, -1, .40, -1, -1, -1, {0}, {-1}},
    {"kraftwerk", .20, -1, .05, .25, .10, .45, -1, -1, -1, {"lp_up", "bp_up", "lp_breathe", 0}, {-1}},
    {"eighties", .20, .10, .15, .45, -1, .35, -1, -1, -1, {0}, {-1}},
    {"nineties", .18, .08, .10, .4, .05, .45, -1, -1, -1, {0}, {-1}},
    {"noughties", .18, .08, .10, .4, -1, .45, -1, -1, -1, {0}, {-1}},
    {"c64game", .18, .10, .20, .55, .05, .30, 5, 4, 2, {0}, {.5, 1.0, -1}},
    {"rock", .08, .35, .25, .55, -1, .10, 6, 3, 1, {0}, {-1}},
    {"metal", .06, .30, .30, .60, -1, .08, -1, -1, -1, {0}, {-1}},
    {"blues", .06, .40, .30, .55, -1, .05, -1, -1, -1, {0}, {-1}},
    {"boogie", .05, .30, .25, .45, -1, .05, -1, -1, -1, {0}, {-1}},
    {"jazz", .08, .25, .30, .45, -1, .05, -1, -1, -1, {0}, {-1}},
    {"pop", .14, .10, .10, .45, -1, .25, -1, -1, -1, {0}, {-1}},
    {"ballade", .12, .12, .08, .60, -1, .20, 4, 5, 1, {0}, {1.0, 1.5, -1}},
    {"schlager", .10, .08, .06, .50, -1, .15, -1, -1, -1, {0}, {-1}},
    {"disco", .10, .10, .20, .40, .05, .40, -1, -1, -1, {0}, {-1}},
    {"seventies", .10, .15, .20, .45, -1, .25, -1, -1, -1, {0}, {-1}},
    {"sixties", .06, .15, .15, .45, -1, .10, -1, -1, -1, {0}, {-1}},
    {"dnb", .15, -1, .10, .35, .15, .55, -1, -1, -1, {"lp_up", "hp_up", "bp_up", 0}, {-1}},
    {"dubstep", .15, -1, .15, .30, .20, .50, -1, -1, -1, {"lp_up", "bp_up", 0}, {-1}},
    {"hiphop", .10, .10, .20, .35, .08, .20, -1, -1, -1, {0}, {-1}},
    {"reggae", .08, .12, .15, .35, -1, .15, -1, -1, -1, {0}, {-1}},
    {"ambient", .30, .05, .05, .50, -1, .80, 3, 6, 1, {"lp_breathe", "lp_up", "bp_breathe", 0}, {1.0, 1.5, 2.0}},
    {"klassik", .04, .02, .03, .55, -1, .05, 5, 4, 1, {0}, {-1}},
    {"film", .10, .06, .06, .55, -1, .30, 3, 6, 1, {0}, {-1}},
    {"viking", .10, .20, .12, .50, -1, .20, 4, 5, 1, {0}, {-1}},
  };
  int k, j;
  p->glide = .12; p->scoop = .05; p->fall = .10; p->vib = .35; p->bslide = .03; p->sweep = .25;
  p->arc[0] = 6; p->arc[1] = 3; p->arc[2] = 1;
  p->sweeps[0] = "lp_up"; p->sweeps[1] = "lp_up"; p->sweeps[2] = "bp_up"; p->nsweeps = 3;
  p->glide_rows[0] = .5; p->glide_rows[1] = 1.0; p->nglide = 2;
  for (k = 0; k < (int)(sizeof T / sizeof T[0]); k++)
  {
    if (strcmp(T[k].k, style)) continue;
    if (T[k].glide >= 0) p->glide = T[k].glide;
    if (T[k].scoop >= 0) p->scoop = T[k].scoop;
    if (T[k].fall >= 0) p->fall = T[k].fall;
    if (T[k].vib >= 0) p->vib = T[k].vib;
    if (T[k].bslide >= 0) p->bslide = T[k].bslide;
    if (T[k].sweep >= 0) p->sweep = T[k].sweep;
    if (T[k].a0 >= 0) { p->arc[0] = T[k].a0; p->arc[1] = T[k].a1; p->arc[2] = T[k].a2; }
    if (T[k].sw[0])
    {
      p->nsweeps = 0;
      for (j = 0; (j < 4) && (T[k].sw[j]); j++) p->sweeps[p->nsweeps++] = T[k].sw[j];
    }
    if (T[k].gr[0] >= 0)
    {
      p->nglide = 0;
      for (j = 0; (j < 3) && (T[k].gr[j] >= 0); j++) p->glide_rows[p->nglide++] = T[k].gr[j];
    }
  }
  // the style's own settings ('expr') and the compact arc
  if (extra)
  {
    const JV *v;
    if ((v = jv_get(extra, "glide"))) p->glide = jv_num(v, p->glide);
    if ((v = jv_get(extra, "scoop"))) p->scoop = jv_num(v, p->scoop);
    if ((v = jv_get(extra, "fall"))) p->fall = jv_num(v, p->fall);
    if ((v = jv_get(extra, "vib"))) p->vib = jv_num(v, p->vib);
    if ((v = jv_get(extra, "bslide"))) p->bslide = jv_num(v, p->bslide);
    if ((v = jv_get(extra, "sweep"))) p->sweep = jv_num(v, p->sweep);
    if ((v = jv_get(extra, "arc")) && (jv_len(v) == 3))
      for (j = 0; j < 3; j++) p->arc[j] = jv_num(jv_at(v, j), p->arc[j]);
    if ((v = jv_get(extra, "sweeps")))
    {
      static char sbuf[4][16];
      p->nsweeps = 0;
      for (j = 0; (j < jv_len(v)) && (j < 4); j++)
      {
        snprintf(sbuf[j], 16, "%s", jv_str(jv_at(v, j), "lp_up"));
        p->sweeps[p->nsweeps++] = sbuf[j];
      }
    }
    if ((v = jv_get(extra, "glide_rows")))
    {
      p->nglide = 0;
      for (j = 0; (j < jv_len(v)) && (j < 3); j++) p->glide_rows[p->nglide++] = jv_num(jv_at(v, j), 1);
    }
  }
}

// ---------------------------------------------------------------------------
// Python repr() of the decision keys
// ---------------------------------------------------------------------------

static void reprstr(char *out, int size, const char *s)
{
  // Python: single quotes unless the text contains one (and no double quote)
  if ((strchr(s, '\'')) && (!strchr(s, '"'))) snprintf(out, size, "\"%s\"", s);
  else snprintf(out, size, "'%s'", s);
}

typedef struct { char *key; int val; } DENT;
static DENT *dtab = NULL;
static int dcap = 0, dnum = 0;

static unsigned dhash(const char *s)
{
  unsigned h = 2166136261u;
  while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
  return h;
}

static int dget(const char *key, int *val)
{
  unsigned h;
  if (!dcap) return 0;
  h = dhash(key) & (dcap - 1);
  while (dtab[h].key)
  {
    if (!strcmp(dtab[h].key, key)) { *val = dtab[h].val; return 1; }
    h = (h + 1) & (dcap - 1);
  }
  return 0;
}

static void dput(const char *key, int val)
{
  unsigned h;
  if (dnum * 2 >= dcap)
  {
    DENT *old = dtab;
    int oc = dcap, k;
    dcap = dcap ? dcap * 2 : 4096;
    dtab = (DENT *)calloc(dcap, sizeof(DENT));
    dnum = 0;
    for (k = 0; k < oc; k++)
      if (old[k].key)
      {
        h = dhash(old[k].key) & (dcap - 1);
        while (dtab[h].key) h = (h + 1) & (dcap - 1);
        dtab[h] = old[k];
        dnum++;
      }
    free(old);
  }
  h = dhash(key) & (dcap - 1);
  while (dtab[h].key) h = (h + 1) & (dcap - 1);
  dtab[h].key = strdup(key);
  dtab[h].val = val;
  dnum++;
}

static void dclear(void)
{
  int k;
  for (k = 0; k < dcap; k++) free(dtab[k].key);
  free(dtab);
  dtab = NULL;
  dcap = dnum = 0;
}

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

typedef struct
{
  COMP *c;
  PROF p;
  double fpr;
  SGRNG rng;
  char seed_s[64];
  int groups[8][3], ngroups, glen[8];
  int secname[SG_MAXSECS], secr0[SG_MAXSECS], secn[SG_MAXSECS], nsec;
  int stats[6];
  int efilt[8];                 // 1.1: the group got the energy filter
} EXPR;

static const char *cat(EXPR *x, int ins) { return x->c->ins[ins].cat; }

static int isfree(SGTRACK *t, int r)
{
  if (r <= 0) return 0;
  if (r >= t->rows) tr_grow(t, r + 64);
  if (t->hascmds[r]) return 0;
  return !((t->ev[r].type == EV_NOTE) && (t->ev[r].hascmd));
}

static int evdur(const SGEV *e, int def) { return e->dur > 0 ? e->dur : def; }

static void setcmd(SGTRACK *t, int r, const SGCMD *c)
{
  if (r >= t->rows) tr_grow(t, r + 64);
  t->hascmds[r] = 1;
  t->cmds[r] = *c;
}

static void arc(EXPR *x)
{
  COMP *c = x->c;
  double tot = x->p.arc[0] + x->p.arc[1] + x->p.arc[2], v = rng_random(&x->rng) * tot;
  int kind, total, b1 = 0, b2 = 0, s, top_from = -1, ti;
  // random.choices(): bisect over the cumulative weights
  if (v < x->p.arc[0]) kind = 0;
  else if (v < x->p.arc[0] + x->p.arc[1]) kind = 1;
  else kind = 2;
  if ((kind == 0) || (!x->nsec)) return;
  total = x->secr0[x->nsec - 1] + x->secn[x->nsec - 1];
  for (s = 0; s < x->nsec; s++)
  {
    if (x->secr0[s] + x->secn[s] <= total * 0.30) b1 = x->secr0[s] + x->secn[s];
    if (x->secr0[s] + x->secn[s] <= total * 0.48) b2 = x->secr0[s] + x->secn[s];
  }
  if (b1 < x->secn[0]) b1 = x->secn[0];
  if (b2 < b1) b2 = b1;
  if (kind == 2)
    for (s = 0; s < x->nsec; s++)
    {
      int b = sec_base(x->secname[s]);
      if (((b == SEC_B) || (b == SEC_C)) && (x->secr0[s] >= total * 0.6)) top_from = x->secr0[s];
    }
  for (ti = 0; ti < c->ntracks; ti++)
  {
    SGTRACK *t = &c->tracks[ti];
    for (s = 0; s < x->nsec; s++)
    {
      int r0 = x->secr0[s], n = x->secn[s], r, g, mode;
      if (r0 < b1) mode = 0;                 // melodic + arp
      else if (r0 < b2) mode = 1;            // lead, echo
      else if ((top_from >= 0) && (r0 >= top_from)) mode = 2;
      else continue;
      if (mode == 2)
      {
        int mx = -999, any = 0;
        for (r = r0; (r < r0 + n) && (r < t->rows); r++)
          if ((t->ev[r].type == EV_NOTE) && ((!strcmp(cat(x, t->ev[r].ins), "lead")) || (!strcmp(cat(x, t->ev[r].ins), "echo"))))
          {
            any = 1;
            if (t->ev[r].note > mx) mx = t->ev[r].note;
          }
        if ((any) && (mx + 12 <= 84))
          for (r = r0; (r < r0 + n) && (r < t->rows); r++)
            if ((t->ev[r].type == EV_NOTE) && ((!strcmp(cat(x, t->ev[r].ins), "lead")) || (!strcmp(cat(x, t->ev[r].ins), "echo"))))
              t->ev[r].note += 12;
        continue;
      }
      for (g = 0; g < 3; g++)
      {
        int mn = 999, any = 0, floor_ = g == 0 ? 43 : 45;
        // groups: (lead, echo), (pad,), (arp,); mode 1 only the first
        if ((mode == 1) && (g != 0)) continue;
        for (r = r0; (r < r0 + n) && (r < t->rows); r++)
        {
          const char *ct;
          int in;
          if (t->ev[r].type != EV_NOTE) continue;
          ct = cat(x, t->ev[r].ins);
          in = g == 0 ? ((!strcmp(ct, "lead")) || (!strcmp(ct, "echo"))) : g == 1 ? !strcmp(ct, "pad") : !strcmp(ct, "arp");
          if (!in) continue;
          any = 1;
          if (t->ev[r].note < mn) mn = t->ev[r].note;
        }
        if ((any) && (mn - 12 >= floor_))
          for (r = r0; (r < r0 + n) && (r < t->rows); r++)
          {
            const char *ct;
            int in;
            if (t->ev[r].type != EV_NOTE) continue;
            ct = cat(x, t->ev[r].ins);
            in = g == 0 ? ((!strcmp(ct, "lead")) || (!strcmp(ct, "echo"))) : g == 1 ? !strcmp(ct, "pad") : !strcmp(ct, "arp");
            if (in) t->ev[r].note -= 12;
          }
      }
    }
  }
}

static double glide_speed(int a, int b, double frames)
{
  return abs(sg_freq(b) - sg_freq(a)) / (frames > 1.0 ? frames : 1.0);
}

static int decide(EXPR *x, const char *key, double prob)
{
  int v;
  if (dget(key, &v)) return v;
  v = rng_random(&x->rng) < prob;
  dput(key, v);
  return v;
}

static void prand(EXPR *x, SGRNG *r, const char *key)
{
  rng_seeds(r, "%s|%s", x->seed_s, key);
}

static void pitch(EXPR *x)
{
  COMP *c = x->c;
  double fpr = x->fpr;
  int span = 2 * c->spb, ti;
  for (ti = 0; ti < c->ntracks; ti++)
  {
    SGTRACK *t = &c->tracks[ti];
    int nrows = t->rows, *rows, nr = 0, i, prevr = -1, last_glide = -99;
    SGEV preve;
    rows = (int *)malloc(sizeof(int) * (nrows + 1));
    for (i = 0; i < nrows; i++)
      if (t->ev[i].type != EV_NONE) rows[nr++] = i;
    for (i = 0; i < nr; i++)
    {
      int r = rows[i], nxt = i + 1 < nr ? rows[i + 1] : -1, d, legato, ok, pos, end, phrase_end;
      SGEV e = t->ev[r];
      const SGINS *ins;
      const char *ct;
      char iname[80], key[200];
      if (e.type != EV_NOTE)
      {
        prevr = -1;
        continue;
      }
      ins = &c->ins[e.ins];
      ct = ins->cat;
      d = evdur(&e, 1);
      legato = (prevr >= 0) && (preve.ins == e.ins) && (prevr + evdur(&preve, 1) >= r);
      ok = sg_slide_ok(ins);
      pos = r % span;
      reprstr(iname, sizeof iname, ins->name);
      if ((ok) && (legato) && (ismelodic(ct)) && (abs(e.note - preve.note) >= 1) && (abs(e.note - preve.note) <= 7) && (d >= 2) &&
          (evdur(&preve, 1) >= 2) && (r - last_glide >= c->spb / 2) && (isfree(t, r)) &&
          (snprintf(key, sizeof key, "('g', %d, %s, %d, %d, %d)", ti, iname, pos, preve.note, e.note), decide(x, key, x->p.glide)))
      {
        SGRNG pr;
        double frames;
        SGCMD sp;
        int rows_needed, q;
        last_glide = r;
        snprintf(key, sizeof key, "('g', %d, %s, %d, %d)", ti, iname, pos, e.note);
        prand(x, &pr, key);
        frames = x->p.glide_rows[rng_below(&pr, x->p.nglide)] * fpr;
        sp = spdcmd(3, glide_speed(preve.note, e.note, frames));
        t->ev[r].hascmd = 1;
        t->ev[r].cmd = sp;
        e = t->ev[r];
        rows_needed = (int)(frames / fpr) + 1;
        for (q = r + 1; (q < r + d) && (q < r + rows_needed + 1); q++)
        {
          if ((q < t->rows) && (t->ev[q].type != EV_NONE)) break;
          if (!isfree(t, q)) break;
          setcmd(t, q, &sp);
        }
        x->stats[0]++;
      }
      else if ((ok) && (legato) && (!strcmp(ct, "bass")) && (abs(e.note - preve.note) >= 1) && (abs(e.note - preve.note) <= 12) &&
               (r - last_glide >= 2) && (isfree(t, r)) &&
               (snprintf(key, sizeof key, "('b', %d, %s, %d, %d)", ti, iname, r % c->spb, preve.note - e.note), decide(x, key, x->p.bslide)))
      {
        SGCMD sp;
        last_glide = r;
        sp = spdcmd(3, glide_speed(preve.note, e.note, fpr - 1.5 > 1.0 ? fpr - 1.5 : 1.0));
        t->ev[r].hascmd = 1;
        t->ev[r].cmd = sp;
        e = t->ev[r];
        x->stats[4]++;
      }
      else if ((ok) && (!strcmp(ct, "lead")) && (d >= 4) && (!legato) && (e.note >= 40) && (isfree(t, r)) && (isfree(t, r + 1)) &&
               (!((r + 1 < t->rows) && (t->ev[r + 1].type != EV_NONE))) &&
               (snprintf(key, sizeof key, "('s', %d, %s, %d, %d)", ti, iname, pos, e.note), decide(x, key, x->p.scoop)))
      {
        SGRNG pr;
        static const int ss[3] = {1, 2, 2};
        int s;
        SGCMD sp;
        prand(x, &pr, key);
        s = ss[rng_below(&pr, 3)];
        sp = spdcmd(3, glide_speed(e.note - s, e.note, fpr - 1.5 > 1.0 ? fpr - 1.5 : 1.0));
        tr_note(t, r, e.note - s, e.ins, 1);
        tr_notecmd(t, r + 1, e.note, e.ins, d - 1, &sp);
        x->stats[1]++;
        e = t->ev[r + 1];
        r = r + 1;
        d = d - 1;
      }
      end = r + d;
      phrase_end = (nxt < 0) || (nxt >= end + 2) || ((nxt < t->rows) && (t->ev[nxt].type == EV_OFF));
      if ((ok) && ((!strcmp(ct, "lead")) || (!strcmp(ct, "echo"))) && (d >= 3) && (phrase_end) && (e.note >= 45) &&
          (snprintf(key, sizeof key, "('f', %d, %s, %d, %d)", ti, iname, pos, e.note), decide(x, key, x->p.fall)))
      {
        int rowsq[3], nq = 0, q, m = 0;
        for (q = end - 1; q < end + 2; q++)
          if ((q > r) && (isfree(t, q)) && (!((q < t->rows) && (t->ev[q].type == EV_NOTE)))) rowsq[nq++] = q;
        if (nxt >= 0)
        {
          int k;
          for (k = 0; k < nq; k++)
            if ((rowsq[k] < nxt) || ((t->ev[nxt].type == EV_OFF) && (rowsq[k] <= nxt + 1))) rowsq[m++] = rowsq[k];
          nq = m;
        }
        if (nq >= 2)
        {
          SGRNG pr;
          static const int dr[3] = {3, 5, 7};
          int drop, k;
          SGCMD sp;
          prand(x, &pr, key);
          drop = dr[rng_below(&pr, 3)];
          sp = spdcmd(2, (sg_freq(e.note) - sg_freq(e.note - drop)) / (nq * (fpr - 1)));
          for (k = 0; k < nq; k++) setcmd(t, rowsq[k], &sp);
          x->stats[2]++;
        }
      }
      else if ((ok) && (ismelodic(ct)) && (d >= 6) && (!ins->hasvib) &&
               (snprintf(key, sizeof key, "('v', %d, %s, %d, %d)", ti, iname, pos, e.note), decide(x, key, x->p.vib)))
      {
        SGRNG vr;
        static const int k0s[3] = {2, 3, 4}, cmps[4] = {3, 4, 4, 5};
        int k0, semi, depth, cmp_, q, nrw = 0;
        prand(x, &vr, key);
        k0 = r + k0s[rng_below(&vr, 3)];
        semi = sg_freq(e.note + 1) - sg_freq(e.note);
        {
          double u = rng_uniform(&vr, 0.05, 0.10), v = semi * u;
          depth = sg_quant_speed(v > 1 ? v : 1);
        }
        cmp_ = cmps[rng_below(&vr, 4)];
        if (depth < 256)
        {
          SGCMD sp;
          memset(&sp, 0, sizeof sp);
          sp.cmd = 4;
          sp.kind = CD_SPD;
          sp.a = cmp_;
          sp.b = depth & 0xff;
          for (q = k0; q < end; q++)
          {
            if ((q < t->rows) && (t->ev[q].type != EV_NONE)) break;
            if (!isfree(t, q)) break;
            setcmd(t, q, &sp);
            nrw++;
          }
          if (nrw) x->stats[3]++;
        }
      }
      preve = e;
      prevr = r;
    }
    free(rows);
  }
}

static int filter_free(EXPR *x, int g)
{
  int k, r;
  for (k = 0; k < x->glen[g]; k++)
  {
    SGTRACK *t = &x->c->tracks[x->groups[g][k]];
    for (r = 0; r < t->rows; r++)
      if ((t->ev[r].type == EV_NOTE) && (x->c->ins[t->ev[r].ins].filt[0])) return 0;
  }
  return 1;
}

// 1.1: the filter follows the energy of the song. On every SID with a free
// filter one accompaniment voice (arpeggio, pad, chords) runs through a
// lowpass whose cutoff follows the energy arc: dark in the intro and the first
// verse, opening towards the chorus (a build-up), wide open in the chorus.
static double ecut(COMP *c, double e)
{
  // not too dark: a very low cutoff makes the 6581 filter muddy and a resonant
  // peak can sound out of tune
  int lo = c->model == 8580 ? 0x20 : 0x24, hi = c->model == 8580 ? 0x90 : 0x60;
  double f = (e - 0.25) / 0.6;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return lo + (hi - lo) * f;
}

static void energy_filter(EXPR *x)
{
  COMP *c = x->c;
  int g, s, i;
  if ((!c->engine) || (c->kind) || (c->jingle) || (!x->nsec)) return;
  for (g = 0; g < x->ngroups; g++)
  {
    int best = -1, filtered = 0, any = 0;
    double bests = 0;
    if (!filter_free(x, g)) continue;
    // the voice: the most accompaniment notes over the song (no lead, bass or drums)
    for (i = 0; i < x->glen[g]; i++)
    {
      SGTRACK *t = &c->tracks[x->groups[g][i]];
      int r;
      double score = 0;
      for (r = 0; r < t->rows; r++)
      {
        const char *ct;
        if (t->ev[r].type != EV_NOTE) continue;
        ct = cat(x, t->ev[r].ins);
        if ((tb_isdrum(ct)) || (!strcmp(ct, "kickbass")) || (!strcmp(ct, "lead")) || (!strcmp(ct, "bass"))) continue;
        score += ((!strcmp(ct, "arp")) || (!strcmp(ct, "pad"))) ? 2 : 1;
      }
      if ((score > 0) && ((best < 0) || (score > bests))) { bests = score; best = x->groups[g][i]; }
    }
    if (best < 0) continue;
    for (s = 0; s < x->nsec; s++)
    {
      SGTRACK *t = &c->tracks[best];
      double e = c->energy[s], en = s + 1 < x->nsec ? c->energy[s + 1] : e;
      int r0 = x->secr0[s], n = x->secn[s], start = -1, q;
      SGCMD cm;
      for (q = r0; (q < r0 + 4) && (q < t->rows); q++)
        if (isfree(t, q)) { start = q; break; }
      if (start < 0) continue;
      memset(&cm, 0, sizeof cm);
      if (e >= 0.85)
      {
        // the chorus: open
        if (!filtered) continue;
        cm.cmd = 0xb;
        cm.kind = CD_PLAIN;
        cm.a = 0;
        setcmd(t, start, &cm);
        filtered = 0;
        continue;
      }
      {
        int c0 = (int)ecut(c, e), c1 = c0;
        if (en > e) c1 = (int)ecut(c, en >= 0.85 ? 1.0 : en);
        sg_ramp_name(c0, c1, (n - (start - r0)) * x->fpr, cm.fname, sizeof cm.fname);
        cm.cmd = 0xa;
        cm.kind = CD_FPROG;
        cm.b = -1;
        setcmd(t, start, &cm);
        filtered = 1;
        any = 1;
        if (c1 > c0) x->stats[5]++;
      }
    }
    if (any) x->efilt[g] = 1;
  }
}

static void sweeps(EXPR *x)
{
  COMP *c = x->c;
  int g, s;
  if ((x->p.sweep <= 0) || (!x->nsec)) return;
  for (g = 0; g < x->ngroups; g++)
  {
    if (!filter_free(x, g)) continue;
    if (x->efilt[g]) continue;
    for (s = 0; s < x->nsec; s++)
    {
      int name = x->secname[s], nxt = s + 1 < x->nsec ? x->secname[s + 1] : -1, r0 = x->secr0[s], n = x->secn[s];
      int build, breathe = 0, k, nk = 0, best = -1, start = -1, stop = -1, q, i;
      double bests = 0;
      const char *kinds[4], *kind;
      SGRNG sr;
      char fname[48];
      build = (name == SEC_INTRO) || (sec_base(name) == SEC_C) || ((nxt >= 0) && (sec_base(nxt) == SEC_B) && (sec_base(name) != SEC_B));
      for (k = 0; k < x->p.nsweeps; k++)
        if (strstr(x->p.sweeps[k], "breathe")) breathe = 1;
      breathe = breathe && ((sec_base(name) == SEC_A) || (sec_base(name) == SEC_B));
      rng_seeds(&sr, "%s|sweep|%d|%s|%s", x->seed_s, x->groups[g][0], sg_secname[name], nxt >= 0 ? sg_secname[nxt] : "None");
      if ((!(build || breathe)) || (rng_random(&sr) >= x->p.sweep * (build ? 1.0 : 0.5))) continue;
      for (k = 0; k < x->p.nsweeps; k++)
        if ((strstr(x->p.sweeps[k], "breathe") != NULL) == (!build)) kinds[nk++] = x->p.sweeps[k];
      if (!nk) kinds[nk++] = "lp_up";
      kind = kinds[rng_below(&sr, nk)];
      if ((!strcmp(kind, "hp_up")) && (name == SEC_INTRO)) kind = "lp_up";
      for (i = 0; i < x->glen[g]; i++)
      {
        SGTRACK *t = &c->tracks[x->groups[g][i]];
        int r, any = 0, drum = 0, first = -1;
        double score = 0;
        for (r = r0; (r < r0 + n) && (r < t->rows); r++)
        {
          const char *ct;
          if (t->ev[r].type != EV_NOTE) continue;
          ct = cat(x, t->ev[r].ins);
          if (first < 0) first = t->ev[r].ins;
          any = 1;
          if ((tb_isdrum(ct)) || (!strcmp(ct, "kickbass"))) drum = 1;
          score += ((!strcmp(ct, "arp")) || (!strcmp(ct, "pad"))) ? 2 : 1;
        }
        if ((!any) || (drum)) continue;
        if ((!strcmp(cat(x, first), "bass")) && (name != SEC_INTRO)) score *= 0.3;
        if ((best < 0) || (score > bests))
        {
          bests = score;
          best = x->groups[g][i];
        }
      }
      if (best < 0) continue;
      {
        SGTRACK *t = &c->tracks[best];
        SGCMD cm;
        for (q = r0; q < r0 + 4; q++)
          if (isfree(t, q)) { start = q; break; }
        for (q = r0 + n; q < r0 + n + 4; q++)
          if (isfree(t, q)) { stop = q; break; }
        if (start < 0) continue;
        sg_sweep_name(kind, (n - (start - r0)) * x->fpr, c->model, fname, sizeof fname);
        memset(&cm, 0, sizeof cm);
        cm.cmd = 0xa;
        cm.kind = CD_FPROG;
        cm.b = -1;
        snprintf(cm.fname, sizeof cm.fname, "%s", fname);
        setcmd(t, start, &cm);
        if ((stop >= 0) && (stop < r0 + n + 4))
        {
          memset(&cm, 0, sizeof cm);
          cm.cmd = 0xb;
          cm.kind = CD_PLAIN;
          cm.a = 0;
          setcmd(t, stop, &cm);
        }
        x->stats[5]++;
      }
    }
  }
}

void sg_express(COMP *c)
{
  EXPR x;
  int s, row = 0;
  memset(&x, 0, sizeof x);
  x.c = c;
  {
    const JV *ex = comp_style(c, "expr");
    profile(c->stylekey, ex, &x.p);
    if (c->compact) { x.p.arc[0] = 1; x.p.arc[1] = 0; x.p.arc[2] = 0; }
  }
  x.fpr = (c->tempo[0] + c->tempo[1]) / 2.0;
  if (x.fpr < 2.0) x.fpr = 2.0;
  if (c->exprseed[0]) snprintf(x.seed_s, sizeof x.seed_s, "%s-expr", c->exprseed);
  else snprintf(x.seed_s, sizeof x.seed_s, "%u-%d-expr", c->seed, c->mel_var);
  rng_seeds(&x.rng, "%s", x.seed_s);
  for (s = 0; s < c->nsecs; s++)
  {
    x.secname[s] = c->secs[s];
    x.secr0[s] = row;
    x.secn[s] = c->secbars[s] * c->spb;
    row += x.secn[s];
  }
  x.nsec = c->nsecs;
  dclear();
  arc(&x);
  pitch(&x);
  // which voices share a SID (two SIDs: the placement of the song build, on the final notes)
  if (c->ntracks == 6)
  {
    int order[6];
    double b, a;
    tb_place_tracks(c, c->tracks, c->total_rows, c->voices == 2 ? 2 : -1, order, &b, &a);
    x.ngroups = 2;
    for (s = 0; s < 3; s++) { x.groups[0][s] = order[s]; x.groups[1][s] = order[3 + s]; }
    x.glen[0] = x.glen[1] = 3;
  }
  else
  {
    x.ngroups = 0;
    for (s = 0; s < c->ntracks; s += 3)
    {
      int k;
      x.glen[x.ngroups] = 0;
      for (k = s; (k < s + 3) && (k < c->ntracks); k++) x.groups[x.ngroups][x.glen[x.ngroups]++] = k;
      x.ngroups++;
    }
  }
  energy_filter(&x);
  sweeps(&x);
  dclear();
  memcpy(c->expr_stats, x.stats, sizeof x.stats);
}

// ---------------------------------------------------------------------------
// Arrangement ideas (after rendering)
// ---------------------------------------------------------------------------

void sg_song_ideas(COMP *c)
{
  static const struct { const char *k; double mod; int by[3], nby; double stop; } IDEAS[] = {
    {"pop", .30, {1, 2, 2}, 3, .35}, {"schlager", .45, {1, 2}, 2, .25}, {"eurodance", .30, {1, 2}, 2, .45},
    {"ballade", .35, {1, 2}, 2, .15}, {"eighties", .30, {1, 2}, 2, .35}, {"synthpop", .20, {1, 2}, 2, .30},
    {"disco", .15, {2}, 1, .35}, {"c64game", .15, {1, 2, 3}, 3, .25}, {"trance", .10, {2}, 1, .45},
    {"rock", .10, {2}, 1, .45}, {"metal", .05, {1}, 1, .45}, {"film", .15, {1, 2}, 2, .20},
    {"nineties", .20, {1, 2}, 2, .35}, {"noughties", .15, {1, 2}, 2, .35}, {"sixties", .20, {1, 2}, 2, .30},
    {"seventies", .10, {2}, 1, .30}, {"house", 0, {2}, 1, .40}, {"techno", 0, {2}, 1, .40}, {"acid", 0, {2}, 1, .40},
    {"deeptechno", 0, {2}, 1, .25}, {"deeptrance", 0, {2}, 1, .35}, {"dnb", 0, {2}, 1, .45},
    {"dubstep", 0, {2}, 1, .50}, {"synthwave", .15, {1, 2}, 2, .30}, {"kraftwerk", .05, {2}, 1, .20},
    {"hiphop", 0, {2}, 1, .40}, {"reggae", .05, {2}, 1, .25}, {"blues", 0, {2}, 1, .30}, {"boogie", 0, {2}, 1, .35},
    {"jazz", .10, {1}, 1, .20}, {"viking", .05, {2}, 1, .30}, {"klassik", .10, {5, 7}, 2, 0}, {"ambient", 0, {2}, 1, 0}};
  int k, id = -1, s, total = c->total_rows;
  int secr0[SG_MAXSECS], secn[SG_MAXSECS], ns = c->nsecs, row = 0;
  SGRNG rng;
  c->key_change_row = -1;
  c->key_change_by = 0;
  c->nstops = 0;
  for (k = 0; k < (int)(sizeof IDEAS / sizeof IDEAS[0]); k++)
    if (!strcmp(IDEAS[k].k, c->stylekey)) id = k;
  if ((c->jingle) || (!c->nsecs) || (id < 0)) return;
  rng_seeds(&rng, "%u-ideas-%d", c->seed, c->mel_var);
  for (s = 0; s < ns; s++)
  {
    secr0[s] = row;
    secn[s] = c->secbars[s] * c->spb;
    row += secn[s];
  }
#define ISB(i) (sec_base(c->secs[i]) == SEC_B)
  if (rng_random(&rng) < IDEAS[id].mod)
  {
    int kk = ns - 1, j;
    while ((kk >= 0) && (c->secs[kk] == SEC_OUTRO)) kk--;
    j = kk;
    while ((j > 0) && (ISB(j - 1))) j--;
    if (((kk >= 0) && ISB(kk) && (secr0[j] >= total * 0.45) && (j > 0) && (!ISB(j - 1))) ||
        ((kk >= 0) && ISB(kk) && (secr0[j] >= total * 0.55)))
    {
      int by = IDEAS[id].by[rng_below(&rng, IDEAS[id].nby)], r0 = secr0[j], t, r;
      for (t = 0; t < c->ntracks; t++)
        for (r = r0; r < c->tracks[t].rows; r++)
        {
          SGEV *e = &c->tracks[t].ev[r];
          if ((e->type == EV_NOTE) && (!tb_isdrum(c->ins[e->ins].cat)))
          {
            int n = e->note + by;
            while (n > 92) n -= 12;
            e->note = n;
          }
        }
      for (r = r0; r < c->nchordrows; r++) c->chord_rows[r].root = (c->chord_rows[r].root + by) % 12;
      for (r = 0; r < c->lead_rows.n; r++)
        if (c->lead_rows.v[r].row >= r0) c->lead_rows.v[r].note += by;
      for (r = 0; r < c->bass_rows.n; r++)
        if (c->bass_rows.v[r].row >= r0) c->bass_rows.v[r].note += by;
      c->key_change_row = r0;
      c->key_change_by = by;
    }
  }
  if (rng_random(&rng) < IDEAS[id].stop)
  {
    int cands[SG_MAXSECS], nc = 0, i;
    for (i = 1; i < ns; i++)
      if ((ISB(i)) && (!ISB(i - 1)) && (secn[i - 1] >= 4 * c->spb)) cands[nc++] = i;
    if (nc)
    {
      static const int ks[3] = {1, 1, 2};
      int kk = ks[rng_below(&rng, 3)], pick[2], np = 0, pool[SG_MAXSECS], n = nc;
      if (kk > nc) kk = nc;
      // random.sample (pool method)
      memcpy(pool, cands, sizeof(int) * nc);
      for (i = 0; i < kk; i++)
      {
        int j = rng_below(&rng, n - i);
        pick[np++] = pool[j];
        pool[j] = pool[n - i - 1];
      }
      if ((np == 2) && (pick[0] > pick[1])) { int t = pick[0]; pick[0] = pick[1]; pick[1] = t; }
      for (i = 0; i < np; i++)
      {
        int w1 = secr0[pick[i]], w0 = w1 - (rng_random(&rng) < 0.7 ? c->spb / 2 : c->spb / 4), t, r;
        for (t = 0; t < c->ntracks; t++)
        {
          SGTRACK *tr = &c->tracks[t];
          int sounding = 0;
          for (r = 0; (r < w0) && (r < tr->rows); r++)
          {
            SGEV *e = &tr->ev[r];
            if (e->type == EV_NONE) continue;
            sounding = (e->type == EV_NOTE) && (r + evdur(e, 2) > w0);
          }
          for (r = w0; (r < w1) && (r < tr->rows); r++)
          {
            memset(&tr->ev[r], 0, sizeof(SGEV));
            tr->hascmds[r] = 0;
          }
          if (sounding) tr->ev[w0].type = EV_OFF;
        }
        {
          int m = 0;
          for (r = 0; r < c->lead_rows.n; r++)
            if (!((c->lead_rows.v[r].row >= w0) && (c->lead_rows.v[r].row < w1))) c->lead_rows.v[m++] = c->lead_rows.v[r];
          c->lead_rows.n = m;
          m = 0;
          for (r = 0; r < c->bass_rows.n; r++)
            if (!((c->bass_rows.v[r].row >= w0) && (c->bass_rows.v[r].row < w1))) c->bass_rows.v[m++] = c->bass_rows.v[r];
          c->bass_rows.n = m;
          for (r = w0; r < w1; r++) c->drum_rows[r] = 0;
        }
        if (c->nstops < 4) c->stops[c->nstops++] = w1 / c->spb + 1;
      }
    }
  }
#undef ISB
}

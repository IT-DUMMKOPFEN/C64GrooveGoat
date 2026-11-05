// C64 GrooveGoat - SIDGen song build: filter roles, voice tracks -> patterns
// and orderlists, instrument programs -> the four GoatTracker tables, all
// written into the song in memory (port of SIDGen composer.build_song,
// songbuild.py and song.py by IT-DUMMKOPFEN)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "goattrk2.h"
#include "gsgcomp.h"
#include "gsgbuild.h"
#include "gsgexpr.h"
#include "gsgtimbre.h"

#define MAXSPEED 255

typedef struct { int l[MAX_TABLELEN + 64], r[MAX_TABLELEN + 64], n; } TBUF;

static TBUF tabs[3];                 // wave, pulse, filter
static int speedl[MAX_TABLELEN + 64], speedr[MAX_TABLELEN + 64], nspeed;

typedef struct { int kind, len, off; int l[64], r[64]; } PLACED;
static PLACED cache[512];
static int ncache;

static int place(int kind, const int *l, const int *r, int n)
{
  int k, off;
  TBUF *t = &tabs[kind];
  if (n <= 0) return 0;
  for (k = 0; k < ncache; k++)
    if ((cache[k].kind == kind) && (cache[k].len == n) && (!memcmp(cache[k].l, l, sizeof(int) * n)) &&
        (!memcmp(cache[k].r, r, sizeof(int) * n)))
      return cache[k].off;
  off = t->n;
  for (k = 0; k < n; k++)
  {
    int rr = r[k];
    if ((l[k] == 0xff) && (rr != 0)) rr += off;
    if (t->n < MAX_TABLELEN + 64)
    {
      t->l[t->n] = l[k];
      t->r[t->n] = rr;
      t->n++;
    }
  }
  if ((ncache < 512) && (n <= 64))
  {
    cache[ncache].kind = kind;
    cache[ncache].len = n;
    cache[ncache].off = off + 1;
    memcpy(cache[ncache].l, l, sizeof(int) * n);
    memcpy(cache[ncache].r, r, sizeof(int) * n);
    ncache++;
  }
  return off + 1;
}

static int spd(int l, int r)
{
  int k;
  for (k = 0; k < nspeed; k++)
    if ((speedl[k] == l) && (speedr[k] == r)) return k + 1;
  if (nspeed >= MAX_TABLELEN + 63) return 0;
  speedl[nspeed] = l;
  speedr[nspeed] = r;
  return ++nspeed;
}

static int fold_mask(int mask)
{
  int m = 0;
  while (mask)
  {
    m |= mask & 7;
    mask >>= 3;
  }
  return m;
}

// filter program of an instrument for a SID model (8580: hand-made 6581 programs rescaled)
static int filterprog(const char *name, int mask, int model, int insmodel, int *l, int *r)
{
  const SGFILTER *f = sg_findfilter(name);
  int k;
  double s = 1.0;
  if (!f) return 0;
  if ((model == 8580) && (insmodel != 8580) && (f->scale8580 > 0)) s = f->scale8580;
  for (k = 0; k < f->n; k++)
  {
    int ll = f->l[k], rr = f->r[k];
    if (rr & 0x100) rr = ((rr & 0x0f) << 4) | fold_mask(mask);
    else if ((s != 1.0) && (ll < 0x80))
    {
      if (ll == 0)
      {
        int v = (int)(rr * s + (rr * s >= 0 ? 0.5 : -0.5));
        rr = v < 1 ? 1 : v > 255 ? 255 : v;
      }
      else
      {
        int sp = rr > 127 ? rr - 256 : rr, sp2 = (int)(sp * s + (sp * s >= 0 ? 0.5 : -0.5));
        if (!sp2) sp2 = sp > 0 ? 1 : -1;
        rr = sp2 & 0xff;
      }
    }
    l[k] = ll;
    r[k] = rr;
  }
  return f->n;
}

static int isdrumcat(const char *c)
{
  static const char *d[] = {"snare", "clap", "hat", "ohat", "tom", "crash", "ride", "perc"};
  int k;
  for (k = 0; k < 8; k++)
    if (!strcmp(c, d[k])) return 1;
  return 0;
}

// voice masks of the instruments
static void usedchannels(COMP *c, int *mask)
{
  int t, r;
  for (t = 0; t < c->numins; t++) mask[t] = 0;
  for (t = 0; t < c->ntracks; t++)
    for (r = 0; r < c->tracks[t].rows; r++)
      if (c->tracks[t].ev[r].type == EV_NOTE) mask[c->tracks[t].ev[r].ins] |= 1 << t;
}

// filter roles (composer.build_song)
static void filterroles(COMP *c)
{
  int used[SG_MAXINS], k, filt_channels = 0, bassi = -1;
  usedchannels(c, used);
  for (k = 0; k < c->numins; k++)
    if (!strcmp(c->ins[k].name, sg_formodel(c->i_bass, c->model))) bassi = k;
  for (k = 0; k < c->numins; k++)
  {
    SGINS *i = &c->ins[k];
    const char *role = "other";
    if ((!strcmp(i->cat, "bass")) || (!strcmp(i->cat, "kickbass"))) role = "bass";
    else if ((!strcmp(i->cat, "lead")) && ((!strcmp(i->name, c->i_lead)) || (!strcmp(i->name, c->i_lead_b)) || (!strcmp(i->name, c->i_lead_v))))
      role = "lead";
    else if ((!strcmp(i->cat, "arp")) && (!strncmp(i->name, c->t_pad + 4, strlen(c->t_pad + 4)))) role = "pad";
    if ((c->filter_role[0]) && (!strcmp(role, c->filter_role)))
    {
      if ((c->filter_tpl[0]) && ((strcmp(i->cat, "bass")) || (!i->filt[0]))) snprintf(i->filt, sizeof i->filt, "%s", c->filter_tpl);
      if ((!strcmp(i->cat, "kickbass")) && (bassi >= 0)) snprintf(i->filt, sizeof i->filt, "%s", c->ins[bassi].filt);
      if (i->filt[0]) filt_channels |= used[k];
    }
    else i->filt[0] = 0;
  }
  for (k = 0; k < c->numins; k++)
  {
    SGINS *i = &c->ins[k];
    if ((!i->filt[0]) && (used[k] & filt_channels) && (isdrumcat(i->cat) || !strcmp(i->cat, "kick")))
      if (strcmp(i->cat, "kick")) snprintf(i->filt, sizeof i->filt, "off");
  }
}

static int cmddata(const SGCMD *cm, int ch, int model)
{
  if (cm->kind == CD_PLAIN) return cm->a & 0xff;
  if (cm->kind == CD_SPD) return spd(cm->a & 0xff, cm->b & 0xff);
  if (cm->kind == CD_FPROG)
  {
    int l[64], r[64], n = filterprog(cm->fname, cm->b >= 0 ? cm->b : (1 << ch), model, 0, l, r);
    return place(2, l, r, n);
  }
  return 0;
}

// ---------------------------------------------------------------------------

typedef struct { unsigned char note, ins, cmd; int sym; unsigned char rcmd, data; } PROW;

#define MAX_PACKED 248

// command data as in SIDGen: plain bytes, ('spd', left, right), ('fprog', name, mask)
typedef struct { int kind, a, b; char fname[24]; } SYM;
static SYM *syms = NULL;
static int nsyms = 0, capsyms = 0;

static int addsym(int kind, int a, int b, const char *fname)
{
  int k;
  for (k = 0; k < nsyms; k++)
    if ((syms[k].kind == kind) && (syms[k].a == a) && (syms[k].b == b) && ((kind != CD_FPROG) || (!strcmp(syms[k].fname, fname))))
      return k;
  if (nsyms >= capsyms) syms = (SYM *)realloc(syms, sizeof(SYM) * (capsyms = capsyms * 2 + 256));
  syms[nsyms].kind = kind;
  syms[nsyms].a = a;
  syms[nsyms].b = b;
  snprintf(syms[nsyms].fname, sizeof syms[nsyms].fname, "%s", fname ? fname : "");
  return nsyms++;
}

static int plainsym(int v) { return addsym(CD_PLAIN, v & 0xff, 0, NULL); }

static int cmdsym(const SGCMD *cm, int ch)
{
  if (cm->kind == CD_SPD) return addsym(CD_SPD, cm->a & 0xff, cm->b & 0xff, NULL);
  if (cm->kind == CD_FPROG) return addsym(CD_FPROG, 0, cm->b >= 0 ? cm->b : (1 << ch), cm->fname);
  return plainsym(cm->a);
}

static int symdata(const SYM *y, int model)
{
  if (y->kind == CD_PLAIN) return y->a;
  if (y->kind == CD_SPD) return spd(y->a, y->b);
  {
    int l[64], r[64], n = filterprog(y->fname, y->b, model, 0, l, r);
    return place(2, l, r, n);
  }
}

static PROW *patts[4096];
static int pattrows[4096];
static int npatts;

static int samerows(const PROW *a, const PROW *b, int n)
{
  int k;
  for (k = 0; k < n; k++)
    if ((a[k].note != b[k].note) || (a[k].ins != b[k].ins) || (a[k].cmd != b[k].cmd) || (a[k].sym != b[k].sym)) return 0;
  return 1;
}

static int addpattern(PROW *rows, int n)
{
  int k;
  for (k = 0; k < npatts; k++)
    if ((pattrows[k] == n) && (samerows(patts[k], rows, n))) return k;
  if (npatts >= 4096) return -1;
  patts[npatts] = (PROW *)malloc(sizeof(PROW) * n);
  memcpy(patts[npatts], rows, sizeof(PROW) * n);
  pattrows[npatts] = n;
  return npatts++;
}

static void freepatterns(void)
{
  int k;
  for (k = 0; k < npatts; k++) free(patts[k]);
  npatts = 0;
}

// size of a pattern packed by the relocator (greloc.c packpattern), as SIDGen's gt_packed_size
static int packedsize(const PROW *rows, int n)
{
  static char out[MAX_PATTROWS * 4 * 8 + 16];
  int no = 0, instr = 0, command = -1, data = -1, k, size = 0, cc = 0;
  for (k = 0; k < n; k++)
  {
    const PROW *w = &rows[k];
    if ((w->ins) && (!((k) && (w->ins == instr)))) { out[no++] = 'i'; instr = w->ins; }
    if ((w->cmd != command) || (w->sym != data))
    {
      command = w->cmd;
      data = w->sym;
      out[no++] = 'f';
      if (w->cmd) out[no++] = 'd';
    }
    else if (w->note == REST)
    {
      out[no++] = 'r';
      continue;
    }
    if (w->note != REST) out[no++] = 'n';
  }
  while (cc < no)
  {
    if ((out[cc] == 'r') && (cc))
    {
      int d = cc;
      while ((d < no) && (out[d] == 'r') && (d - cc < 64)) d++;
      size++;
      cc = d;
    }
    else
    {
      size++;
      cc++;
    }
  }
  return size;
}

// ---------------------------------------------------------------------------
// Song.relayout: cut all voices again into patterns of 1 to 8 bars (dynamic
// programming per voice) so that as many patterns as possible are shared
// ---------------------------------------------------------------------------

typedef struct { int *t, n; } TOKS;
typedef struct { int len; int b[8]; } GRP;

static PROW **bars_rows;
static int nbars_, capbars_;
static int barlen_;

static int grpkey_eq(const GRP *a, const GRP *b) { return (a->len == b->len) && (!memcmp(a->b, b->b, sizeof(int) * a->len)); }

// group -> counter (hash table)
typedef struct { GRP g; int v, used; } GENT;
typedef struct { GENT *e; int cap, n; } GMAP;

static unsigned ghash(const GRP *g)
{
  unsigned h = 2166136261u;
  int k;
  for (k = 0; k < g->len; k++) h = (h ^ (unsigned)g->b[k]) * 16777619u;
  return (h ^ g->len) * 2654435761u;
}

static GENT *gfind(GMAP *m, const GRP *g, int create)
{
  unsigned h;
  if (!m->cap)
  {
    if (!create) return NULL;
    m->cap = 4096;
    m->e = (GENT *)calloc(m->cap, sizeof(GENT));
  }
  if ((create) && (m->n * 2 >= m->cap))
  {
    GENT *old = m->e;
    int oc = m->cap, k;
    m->cap *= 2;
    m->e = (GENT *)calloc(m->cap, sizeof(GENT));
    m->n = 0;
    for (k = 0; k < oc; k++)
      if (old[k].used)
      {
        GENT *x = gfind(m, &old[k].g, 1);
        x->v = old[k].v;
      }
    free(old);
  }
  h = ghash(g) & (m->cap - 1);
  while (m->e[h].used)
  {
    if (grpkey_eq(&m->e[h].g, g)) return &m->e[h];
    h = (h + 1) & (m->cap - 1);
  }
  if (!create) return NULL;
  m->e[h].used = 1;
  m->e[h].g = *g;
  m->e[h].v = 0;
  m->n++;
  return &m->e[h];
}

static void gfree(GMAP *m)
{
  free(m->e);
  m->e = NULL;
  m->cap = m->n = 0;
}

static GMAP fitcache;

static int grp_fits(const GRP *g)
{
  GENT *e = gfind(&fitcache, g, 0);
  if (!e)
  {
    static PROW rows[MAX_PATTROWS * 8];
    int n = 0, k, r;
    for (k = 0; k < g->len; k++)
      for (r = 0; r < barlen_; r++) rows[n++] = bars_rows[g->b[k]][r];
    e = gfind(&fitcache, g, 1);
    e->v = (n <= MAX_PATTROWS) && (packedsize(rows, n) <= MAX_PACKED) ? 1 : 2;
  }
  return e->v == 1;
}

static int cutvoice(const TOKS *tk, GMAP *have, int maxl, GRP **outp)
{
  int n = tk->n, i, l, nout = 0;
  GMAP own;
  double *cost = (double *)calloc(n + 1, sizeof(double));
  int *ent = (int *)calloc(n + 1, sizeof(int)), *nxt = (int *)calloc(n + 1, sizeof(int));
  GRP *out = (GRP *)malloc(sizeof(GRP) * (n + 1));
  memset(&own, 0, sizeof own);
  for (i = 0; i < n; i++)
    for (l = 1; (l <= maxl) && (l <= n - i); l++)
    {
      GRP g;
      g.len = l;
      memcpy(g.b, tk->t + i, sizeof(int) * l);
      gfind(&own, &g, 1)->v++;
    }
  for (i = n - 1; i >= 0; i--)
  {
    double bc = 0;
    int be = 0, bl = 0, have_ = 0;
    for (l = 1; (l <= maxl) && (l <= n - i); l++)
    {
      GRP g;
      double cc;
      GENT *h;
      g.len = l;
      memcpy(g.b, tk->t + i, sizeof(int) * l);
      if ((l > 1) && (!grp_fits(&g))) break;
      h = gfind(have, &g, 0);
      cc = cost[i + l] + ((h && h->v > 0) ? 0 : 1.0 / gfind(&own, &g, 0)->v);
      if ((!have_) || (cc < bc) || ((cc == bc) && (ent[i + l] + 1 < be)))
      {
        have_ = 1;
        bc = cc;
        be = ent[i + l] + 1;
        bl = l;
      }
    }
    cost[i] = bc;
    ent[i] = be;
    nxt[i] = bl;
  }
  i = 0;
  while (i < n)
  {
    out[nout].len = nxt[i];
    memcpy(out[nout].b, tk->t + i, sizeof(int) * nxt[i]);
    nout++;
    i += nxt[i];
  }
  free(cost);
  free(ent);
  free(nxt);
  gfree(&own);
  *outp = out;
  return nout;
}

static void relayout(int nch, int *norders, int (*orders)[1024], int bar)
{
  int maxl = 128 / bar, ch, k, i, nseg = nch;
  TOKS toks[MAX_CHN];
  GRP *cuts[MAX_CHN];
  int ncuts[MAX_CHN], order[MAX_CHN], rnd;
  GMAP have, ids;
  if (maxl < 1) maxl = 1;
  if (maxl > 8) maxl = 8;
  barlen_ = bar;
  memset(&have, 0, sizeof have);
  memset(&ids, 0, sizeof ids);
  memset(&fitcache, 0, sizeof fitcache);
  nbars_ = 0;
  // bars of every voice (the orderlists restart at 0: one segment per voice)
  {
    // bar ids: rows compared in full
    for (ch = 0; ch < nseg; ch++)
    {
      int total = 0, d;
      static PROW rows[65536];
      for (d = 0; d < norders[ch]; d++)
      {
        int p = orders[ch][d], r;
        for (r = 0; (r < pattrows[p]) && (total < 65536); r++) rows[total++] = patts[p][r];
      }
      toks[ch].n = 0;
      toks[ch].t = (int *)malloc(sizeof(int) * (total / bar + 2));
      for (i = 0; i < total; i += bar)
      {
        int len = total - i < bar ? total - i : bar, b, found = -1;
        for (b = 0; b < nbars_; b++)
          if (samerows(bars_rows[b], rows + i, len) && ((len == bar) || (b >= 0 && 0)))
          {
            found = b;
            break;
          }
        if (found < 0)
        {
          if (nbars_ >= capbars_) bars_rows = (PROW **)realloc(bars_rows, sizeof(PROW *) * (capbars_ = capbars_ * 2 + 256));
          bars_rows[nbars_] = (PROW *)malloc(sizeof(PROW) * bar);
          memcpy(bars_rows[nbars_], rows + i, sizeof(PROW) * len);
          found = nbars_++;
        }
        toks[ch].t[toks[ch].n++] = found;
      }
    }
  }
  // voices with the most different bars first
  for (k = 0; k < nseg; k++) order[k] = k;
  for (k = 0; k < nseg; k++)
  {
    int a;
    for (a = k + 1; a < nseg; a++)
    {
      int da = 0, dk = 0, x, y;
      for (x = 0; x < toks[order[a]].n; x++)
      {
        int dup = 0;
        for (y = 0; y < x; y++)
          if (toks[order[a]].t[y] == toks[order[a]].t[x]) { dup = 1; break; }
        if (!dup) da++;
      }
      for (x = 0; x < toks[order[k]].n; x++)
      {
        int dup = 0;
        for (y = 0; y < x; y++)
          if (toks[order[k]].t[y] == toks[order[k]].t[x]) { dup = 1; break; }
        if (!dup) dk++;
      }
      if ((da > dk) || ((da == dk) && (order[a] < order[k])))
      {
        int t = order[k];
        order[k] = order[a];
        order[a] = t;
      }
    }
  }
  for (k = 0; k < nseg; k++) { cuts[k] = NULL; ncuts[k] = 0; }
  for (rnd = 0; rnd < 4; rnd++)
    for (k = 0; k < nseg; k++)
    {
      int v = order[k], j;
      if (cuts[v])
      {
        for (j = 0; j < ncuts[v]; j++)
        {
          GENT *h = gfind(&have, &cuts[v][j], 0);
          if (h) h->v--;
        }
        free(cuts[v]);
      }
      ncuts[v] = cutvoice(&toks[v], &have, maxl, &cuts[v]);
      for (j = 0; j < ncuts[v]; j++) gfind(&have, &cuts[v][j], 1)->v++;
    }
  // the plain cut into equal pieces is sometimes better
  {
    int Ls[2] = {4, maxl}, li;
    for (li = 0; li < 2; li++)
    {
      int L = Ls[li] < maxl ? Ls[li] : maxl, ok = 1, j, nplain = 0, ncut = 0;
      GMAP up, uc;
      GRP *plain[MAX_CHN];
      int np[MAX_CHN];
      memset(&up, 0, sizeof up);
      memset(&uc, 0, sizeof uc);
      for (k = 0; k < nseg; k++)
      {
        plain[k] = (GRP *)malloc(sizeof(GRP) * (toks[k].n / L + 2));
        np[k] = 0;
        for (i = 0; i < toks[k].n; i += L)
        {
          GRP g;
          g.len = toks[k].n - i < L ? toks[k].n - i : L;
          memcpy(g.b, toks[k].t + i, sizeof(int) * g.len);
          if (!grp_fits(&g)) ok = 0;
          plain[k][np[k]++] = g;
          if (!gfind(&up, &g, 0)) { gfind(&up, &g, 1); nplain++; }
        }
        for (j = 0; j < ncuts[k]; j++)
          if (!gfind(&uc, &cuts[k][j], 0)) { gfind(&uc, &cuts[k][j], 1); ncut++; }
      }
      if ((ok) && (nplain < ncut))
        for (k = 0; k < nseg; k++)
        {
          free(cuts[k]);
          cuts[k] = plain[k];
          ncuts[k] = np[k];
          plain[k] = NULL;
        }
      for (k = 0; k < nseg; k++) free(plain[k]);
      gfree(&up);
      gfree(&uc);
    }
  }
  // the new patterns and orderlists
  {
    int newn = 0;
    static PROW *np_[4096];
    static int nr_[4096];
    GMAP index;
    memset(&index, 0, sizeof index);
    for (ch = 0; ch < nseg; ch++)
    {
      int j;
      norders[ch] = 0;
      for (j = 0; j < ncuts[ch]; j++)
      {
        GENT *e = gfind(&index, &cuts[ch][j], 0);
        if (!e)
        {
          int r = 0, b2, rr;
          e = gfind(&index, &cuts[ch][j], 1);
          e->v = newn;
          np_[newn] = (PROW *)malloc(sizeof(PROW) * cuts[ch][j].len * bar);
          for (b2 = 0; b2 < cuts[ch][j].len; b2++)
          {
            int bl = bar;
            // the last bar of a voice may be shorter
            for (rr = 0; rr < bl; rr++) np_[newn][r++] = bars_rows[cuts[ch][j].b[b2]][rr];
          }
          nr_[newn] = r;
          newn++;
        }
        if (norders[ch] < 1024) orders[ch][norders[ch]++] = e->v;
      }
    }
    if (newn <= MAX_PATT)
    {
      freepatterns();
      for (k = 0; k < newn; k++)
      {
        patts[k] = np_[k];
        pattrows[k] = nr_[k];
      }
      npatts = newn;
    }
    else
      for (k = 0; k < newn; k++) free(np_[k]);
    gfree(&index);
  }
  for (k = 0; k < nseg; k++)
  {
    free(cuts[k]);
    free(toks[k].t);
  }
  for (k = 0; k < nbars_; k++) free(bars_rows[k]);
  nbars_ = 0;
  gfree(&have);
  gfree(&ids);
  gfree(&fitcache);
}

int sg_buildsong(COMP *c, int writeto, char *err, int errsize)
{
  int used[SG_MAXINS], order[SG_MAXINS], idx[SG_MAXINS], nins = 0, k, ch, plen, gt, mintempo;
  int spanlo[SG_MAXINS], spanhi[SG_MAXINS];
  int tcmd, tdata;
  static int orders[MAX_CHN][1024];
  int norders[MAX_CHN];
  int ptr[SG_MAXINS][4];
  int nch = c->ntracks, model = c->model;

  filterroles(c);
  // playing technique: glides, slides, fall-offs, vibrato, filter sweeps, register arc
  if ((c->expressive) && (!c->expressed))
  {
    c->expressed = 1;
    sg_express(c);
  }
  // two SIDs: similar sounding voices on different chips
  if (c->ntracks == 6)
  {
    int ord[6], k2;
    double b, a;
    SGTRACK tmp[6];
    tb_place_tracks(c, c->tracks, c->total_rows, c->voices == 2 ? 2 : -1, ord, &b, &a);
    for (k2 = 0; k2 < 6; k2++) tmp[k2] = c->tracks[ord[k2]];
    for (k2 = 0; k2 < 6; k2++)
    {
      c->tracks[k2] = tmp[k2];
      c->track_order[k2] = ord[k2];
    }
  }
  // tempo: F command, or funktempo (E) through speed table entry 1
  nspeed = 0;
  ncache = 0;
  memset(tabs, 0, sizeof tabs);
  if (c->tempo[0] == c->tempo[1])
  {
    tcmd = CMD_SETTEMPO;
    tdata = c->tempo[0];
  }
  else
  {
    spd(c->tempo[0], c->tempo[1]);
    tcmd = CMD_FUNKTEMPO;
    tdata = 1;
  }
  mintempo = c->tempo[0] < c->tempo[1] ? c->tempo[0] : c->tempo[1];
  usedchannels(c, used);
  gt = mintempo - 1;
  if (gt > 2) gt = 2;
  if (gt < 1) gt = 1;
  for (k = 0; k < c->numins; k++)
  {
    idx[k] = 0;
    if (!used[k]) continue;
    if (nins >= MAX_INSTR - 1)
    {
      snprintf(err, errsize, "More than 63 instruments");
      return 0;
    }
    order[nins] = k;
    idx[k] = ++nins;
    c->ins[k].gt = (c->ins[k].gt & 0xc0) | gt;
  }
  // relative wavetable notes: nothing may fall out of C-0 .. B-7
  for (k = 0; k < c->numins; k++)
  {
    int w, lo = 0, hi = 0;
    for (w = 0; w < c->ins[k].nw; w++)
    {
      int l = c->ins[k].wl[w], r = c->ins[k].wr[w], v;
      if ((l == 0xff) || (r >= 0x80)) continue;
      v = r < 0x60 ? r : r - 0x80;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    spanlo[k] = lo;
    spanhi[k] = hi;
  }
  // instrument tables first (as SIDGen: wave, pulse, filter, vibrato)
  for (k = 0; k < nins; k++)
  {
    SGINS *i = &c->ins[order[k]];
    int w = place(0, i->wl, i->wr, i->nw), p = i->np ? place(1, i->pl, i->pr, i->np) : 0, f = 0, s = 0;
    if (i->filt[0])
    {
      int l[64], r[64], n = filterprog(i->filt, used[order[k]], model, i->model, l, r);
      f = place(2, l, r, n);
    }
    if (i->hasvib) s = spd(i->vib[0] & 0xff, i->vib[1] & 0xff);
    ptr[k][0] = w;
    ptr[k][1] = p;
    ptr[k][2] = f;
    ptr[k][3] = s;
  }
  // patterns: cut the voices into patterns of 4 bars, the same ones only once
  // (rows keep symbolic command data until the final layout, as SIDGen)
  nsyms = 0;
  plen = 4 * c->spb;
  for (;;)
  {
    int fits, maxo = 0;
    freepatterns();
    for (ch = 0; ch < nch; ch++)
    {
      SGTRACK *t = &c->tracks[ch];
      int start;
      norders[ch] = 0;
      for (start = 0; start < c->total_rows; start += plen)
      {
        PROW rows[MAX_PATTROWS];
        int r, n = 0, p;
        for (r = start; (r < start + plen) && (r < c->total_rows); r++)
        {
          SGEV *e = r < t->rows ? &t->ev[r] : NULL;
          int cmd = 0, sym = plainsym(0);
          const SGCMD *cm = NULL;
          if ((ch % 6 == 0) && (r == 0)) { cmd = tcmd; sym = plainsym(tdata); }
          if ((!cmd) && (r < t->rows) && (t->hascmds[r]) && (!(e && e->type == EV_NOTE && e->hascmd))) cm = &t->cmds[r];
          if ((!cmd) && (e) && (e->type == EV_NOTE) && (e->hascmd)) cm = &e->cmd;
          if (cm)
          {
            cmd = cm->cmd;
            sym = cmdsym(cm, ch);
          }
          if ((!e) || (e->type == EV_NONE)) { rows[n].note = REST; rows[n].ins = 0; }
          else if (e->type == EV_OFF) { rows[n].note = KEYOFF; rows[n].ins = 0; }
          else
          {
            int note = e->note;
            while ((note + spanhi[e->ins] > 95) || (note > 92)) note -= 12;
            while (note + spanlo[e->ins] < 1) note += 12;
            rows[n].note = FIRSTNOTE + note;
            rows[n].ins = idx[e->ins] ? idx[e->ins] : 1;
          }
          rows[n].cmd = cmd;
          rows[n].sym = sym;
          n++;
        }
        p = addpattern(rows, n);
        if (p < 0) break;
        if (norders[ch] < 1024) orders[ch][norders[ch]++] = p;
      }
      if (norders[ch] > maxo) maxo = norders[ch];
    }
    if (nch > 6)
    {
      // 4-8 SIDs: SIDGen checks per SID pair here; the whole song is cut again below
      int pp, worst = 0;
      for (pp = 0; pp < nch; pp += 6)
      {
        static unsigned char u[1024];
        int q, d, cnt = 0;
        memset(u, 0, sizeof u);
        for (q = pp; (q < pp + 6) && (q < nch); q++)
          for (d = 0; d < norders[q]; d++)
            if (!u[orders[q][d]]) { u[orders[q][d]] = 1; cnt++; }
        if (cnt > worst) worst = cnt;
      }
      fits = worst <= MAX_PATT;
    }
    else fits = npatts <= MAX_PATT;
    if ((fits) && (maxo <= MAX_SONGLEN)) break;
    if (plen * 2 > 128)
    {
      if (nch > 6) break;
      snprintf(err, errsize, "The piece is too long or too varied (%d patterns, at most %d)", npatts, MAX_PATT);
      freepatterns();
      return 0;
    }
    plen *= 2;
  }
  // too many patterns for one song, or a pattern too big for the packer: cut the voices again
  {
    int big = 0, k3;
    for (k3 = 0; k3 < npatts; k3++)
      if (packedsize(patts[k3], pattrows[k3]) > MAX_PACKED) big = 1;
    if (((nch > 6) && (npatts > MAX_PATT)) || (big)) relayout(nch, norders, orders, c->spb);
  }
  {
    int k3, big = 0;
    for (k3 = 0; k3 < npatts; k3++)
      if (packedsize(patts[k3], pattrows[k3]) > MAX_PACKED) big = 1;
    if (big)
    {
      snprintf(err, errsize, "A pattern is too complex for GoatTracker (over 256 bytes packed)");
      freepatterns();
      return -1;
    }
  }
  if (npatts + ((nch < 3 * (c->sids == 6 ? 8 : c->sids)) ? 1 : 0) > MAX_PATT)
  {
    snprintf(err, errsize, "The piece is too long or too varied for one song (%d patterns, at most %d)", npatts, MAX_PATT);
    freepatterns();
    return -1;
  }
  // command data: speed table entries and filter programs, in the order of the patterns
  {
    int drop1 = 0, dropA = 0, tries;
    int savespeed = nspeed, savetab[3], savecache = ncache;
    for (k = 0; k < 3; k++) savetab[k] = tabs[k].n;
    for (tries = 0; tries < 3; tries++)
    {
      int p, r, usesA = 0, uses1 = 0;
      nspeed = savespeed;
      ncache = savecache;
      for (k = 0; k < 3; k++) tabs[k].n = savetab[k];
      for (p = 0; p < npatts; p++)
        for (r = 0; r < pattrows[p]; r++)
        {
          PROW *w = &patts[p][r];
          SYM *y = &syms[w->sym];
          if (y->kind != CD_PLAIN)
          {
            if (w->cmd == 0xa) usesA = 1;
            if ((w->cmd >= 1) && (w->cmd <= 4)) uses1 = 1;
          }
          if ((y->kind != CD_PLAIN) && (((dropA) && (w->cmd == 0xa)) || ((drop1) && (w->cmd >= 1) && (w->cmd <= 4))))
          {
            w->rcmd = 0;
            w->data = 0;
            continue;
          }
          w->rcmd = w->cmd;
          w->data = symdata(y, model);
        }
      if ((tabs[2].n > MAX_TABLELEN) && (!dropA) && (usesA)) { dropA = 1; continue; }
      if ((nspeed > MAX_TABLELEN) && (!drop1) && (uses1)) { drop1 = 1; continue; }
      break;
    }
  }
  // ---- write the song into memory
  {
    int s, nsids = c->sids == 6 ? 8 : c->sids;
    clearsong(1, 1, 1, 1, 1);
    setnumsids(nsids);
    for (k = 0; k < nins; k++)
    {
      SGINS *i = &c->ins[order[k]];
      INSTR *d = &instr[k + 1];
      memset(d, 0, sizeof *d);
      d->ad = i->ad;
      d->sr = i->sr;
      d->ptr[WTBL] = ptr[k][0];
      d->ptr[PTBL] = ptr[k][1];
      d->ptr[FTBL] = ptr[k][2];
      d->ptr[STBL] = ptr[k][3];
      d->vibdelay = i->vibdelay & 0xff;
      d->gatetimer = i->gt;
      d->firstwave = i->fw;
      snprintf(d->name, MAX_INSTRNAMELEN, "%.15s", i->en[0] ? i->en : i->name);
    }
    for (k = 0; k < 3; k++)
    {
      int r;
      for (r = 0; r < tabs[k].n; r++)
      {
        ltable[k][r] = tabs[k].l[r];
        rtable[k][r] = tabs[k].r[r];
      }
    }
    for (k = 0; k < nspeed; k++)
    {
      ltable[STBL][k] = speedl[k];
      rtable[STBL][k] = speedr[k];
    }
    for (k = 0; k < npatts; k++)
    {
      int r;
      for (r = 0; r < pattrows[k]; r++)
      {
        pattern[k][r * 4] = patts[k][r].note;
        pattern[k][r * 4 + 1] = patts[k][r].ins;
        pattern[k][r * 4 + 2] = patts[k][r].rcmd;
        pattern[k][r * 4 + 3] = patts[k][r].data;
      }
      pattern[k][pattrows[k] * 4] = ENDPATT;
      pattern[k][pattrows[k] * 4 + 1] = 0;
      pattern[k][pattrows[k] * 4 + 2] = 0;
      pattern[k][pattrows[k] * 4 + 3] = 0;
    }
    for (s = 0; s < MAX_SONGS; s++)
      for (ch = 0; ch < MAX_CHN; ch++)
      {
        memset(&songorder[s][ch][0], 0, MAX_SONGLEN + 2);
        songorder[s][ch][0] = LOOPSONG;
        songlen[s][ch] = 0;
      }
    for (ch = 0; ch < nsids * 3; ch++)
    {
      int n = ch < nch ? norders[ch] : 0, d;
      if (ch >= nch)
      {
        // 6 SIDs as an 8-SID song: SID 7 and 8 stay silent (one empty pattern)
        static PROW silent[16];
        int p;
        for (d = 0; d < 16; d++) { silent[d].note = REST; silent[d].ins = 0; silent[d].cmd = 0; silent[d].data = 0; }
        p = npatts < MAX_PATT ? npatts : MAX_PATT - 1;
        for (d = 0; d < 16; d++)
        {
          pattern[p][d * 4] = silent[d].note;
          pattern[p][d * 4 + 1] = pattern[p][d * 4 + 2] = pattern[p][d * 4 + 3] = 0;
        }
        pattern[p][64] = ENDPATT;
        songorder[0][ch][0] = p;
        songorder[0][ch][1] = LOOPSONG;
        songorder[0][ch][2] = 0;
        songlen[0][ch] = 1;
        continue;
      }
      for (d = 0; d < n; d++) songorder[0][ch][d] = orders[ch][d];
      songorder[0][ch][n] = LOOPSONG;
      songorder[0][ch][n + 1] = 0;
      songlen[0][ch] = n;
    }
    snprintf(songname, sizeof songname, "%.31s", c->title);
    snprintf(authorname, sizeof authorname, "%.31s", "C64 GrooveGoat");
    snprintf(copyrightname, sizeof copyrightname, "2026 SIDGen style (Seed %u)", c->seed);
    for (ch = 0; ch < MAX_CHN; ch++)
    {
      editorUndoInfo.editorInfo[ch].epnum = songorder[0][ch][0] < MAX_PATT ? songorder[0][ch][0] : 0;
      editorUndoInfo.editorInfo[ch].espos = 0;
      editorUndoInfo.editorInfo[ch].esend = 0;
    }
    editorInfo.sidmodel = model == 8580 ? 1 : 0;
    countpatternlengths();
  }
  freepatterns();
  return 1;
}

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

typedef struct { unsigned char note, ins, cmd, data; } PROW;

static PROW *patts[1024];
static int pattrows[1024];
static int npatts;

static int addpattern(PROW *rows, int n)
{
  int k;
  for (k = 0; k < npatts; k++)
    if ((pattrows[k] == n) && (!memcmp(patts[k], rows, sizeof(PROW) * n))) return k;
  if (npatts >= 1024) return -1;
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
          int cmd = 0, data = 0;
          const SGCMD *cm = NULL;
          if ((ch % 6 == 0) && (r == 0)) { cmd = tcmd; data = tdata; }
          if ((!cmd) && (r < t->rows) && (t->hascmds[r]) && (!(e && e->type == EV_NOTE && e->hascmd))) cm = &t->cmds[r];
          if ((!cmd) && (e) && (e->type == EV_NOTE) && (e->hascmd)) cm = &e->cmd;
          if (cm)
          {
            cmd = cm->cmd;
            data = cmddata(cm, ch, model);
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
          rows[n].data = data;
          n++;
        }
        p = addpattern(rows, n);
        if (p < 0) break;
        if (norders[ch] < 1024) orders[ch][norders[ch]++] = p;
      }
      if (norders[ch] > maxo) maxo = norders[ch];
    }
    fits = (npatts <= MAX_PATT) && (maxo <= MAX_SONGLEN - 2);
    if (fits) break;
    if (plen * 2 > MAX_PATTROWS)
    {
      snprintf(err, errsize, "The piece is too long or too varied (%d patterns, at most %d)", npatts, MAX_PATT);
      freepatterns();
      return 0;
    }
    plen *= 2;
  }
  for (k = 0; k < 3; k++)
    if (tabs[k].n > MAX_TABLELEN)
    {
      snprintf(err, errsize, "%s table too long (%d > 255)", k == 0 ? "Wave" : k == 1 ? "Pulse" : "Filter", tabs[k].n);
      freepatterns();
      return 0;
    }
  if (nspeed > MAX_TABLELEN)
  {
    snprintf(err, errsize, "Speed table too long");
    freepatterns();
    return 0;
  }
  if (!writeto)
  {
    freepatterns();
    return 1;
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
        pattern[k][r * 4 + 2] = patts[k][r].cmd;
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
        silent[0].note = KEYOFF;
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

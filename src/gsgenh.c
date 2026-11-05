// C64 GrooveGoat - Enhanced SID calculation (port of SIDGen enhanced.py by
// IT-DUMMKOPFEN): the piece is composed for 8 SIDs (24 voices), written as a
// multi-track MIDI score and then calculated down to 1 or 2 SIDs by the MIDI
// importer, with hints of the composer (grid, swing, roles, instruments);
// afterwards the style's playing technique is added to the reduced voices.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gsgcomp.h"
#include "gsgmidi.h"
#include "gsgtimbre.h"

typedef struct { int st, du, p, v; } ENOTE;
typedef struct { ENOTE *v; int n, cap; } ENLIST;

static void en_add(ENLIST *l, int st, int du, int p, int v)
{
  if (l->n >= l->cap) l->v = (ENOTE *)realloc(l->v, sizeof(ENOTE) * (l->cap = l->cap * 2 + 256));
  l->v[l->n].st = st;
  l->v[l->n].du = du;
  l->v[l->n].p = p;
  l->v[l->n].v = v;
  l->n++;
}

static int cmpen(const void *a, const void *b)
{
  const ENOTE *x = (const ENOTE *)a, *y = (const ENOTE *)b;
  if (x->st != y->st) return x->st < y->st ? -1 : 1;
  if (x->du != y->du) return x->du < y->du ? -1 : 1;
  if (x->p != y->p) return x->p < y->p ? -1 : 1;
  return (x->v > y->v) - (x->v < y->v);
}

// ---------------------------------------------------------------------------
// MIDI writer (midi.write_midi)
// ---------------------------------------------------------------------------

typedef struct { unsigned char *d; int n, cap; } BUF;

static void bput(BUF *b, const void *p, int n)
{
  if (b->n + n > b->cap) b->d = (unsigned char *)realloc(b->d, b->cap = (b->n + n) * 2 + 1024);
  memcpy(b->d + b->n, p, n);
  b->n += n;
}

static void bbyte(BUF *b, int v)
{
  unsigned char c = v;
  bput(b, &c, 1);
}

static void bvl(BUF *b, unsigned v)
{
  unsigned char out[5];
  int n = 0, k;
  out[n++] = v & 0x7f;
  v >>= 7;
  while (v)
  {
    out[n++] = 0x80 | (v & 0x7f);
    v >>= 7;
  }
  for (k = n - 1; k >= 0; k--) bbyte(b, out[k]);
}

static void b32(BUF *b, unsigned v)
{
  bbyte(b, v >> 24);
  bbyte(b, v >> 16);
  bbyte(b, v >> 8);
  bbyte(b, v);
}

typedef struct { char name[48]; int ch, prog; ENLIST notes; } ETRACK;
typedef struct { int t, kind, a, b, c, idx; } MEV;

static int cmpmev(const void *a, const void *b)
{
  const MEV *x = (const MEV *)a, *y = (const MEV *)b;
  if (x->t != y->t) return x->t < y->t ? -1 : 1;
  if (x->kind != y->kind) return x->kind - y->kind;
  return x->idx - y->idx;                    // stable
}

static unsigned char *write_midi(ETRACK *tr, int ntr, int ppq, double bpm, int *len)
{
  BUF out = {0}, c = {0};
  int us = (int)(60e6 / bpm), k;
  bput(&out, "MThd", 4);
  b32(&out, 6);
  bbyte(&out, 0);
  bbyte(&out, 1);
  bbyte(&out, (ntr + 1) >> 8);
  bbyte(&out, (ntr + 1) & 0xff);
  bbyte(&out, ppq >> 8);
  bbyte(&out, ppq & 0xff);
  {
    unsigned char meta[] = {0x00, 0xff, 0x51, 0x03, (us >> 16) & 255, (us >> 8) & 255, us & 255,
                            0x00, 0xff, 0x58, 0x04, 4, 2, 24, 8, 0x00, 0xff, 0x2f, 0x00};
    bput(&out, "MTrk", 4);
    b32(&out, sizeof meta);
    bput(&out, meta, sizeof meta);
  }
  for (k = 0; k < ntr; k++)
  {
    ETRACK *t = &tr[k];
    MEV *ev = (MEV *)malloc(sizeof(MEV) * (t->notes.n * 2 + 1));
    int n = 0, i, last = 0, ln = strlen(t->name);
    for (i = 0; i < t->notes.n; i++)
    {
      ENOTE *x = &t->notes.v[i];
      ev[n].t = x->st; ev[n].kind = 1; ev[n].a = 0x90 | t->ch; ev[n].b = x->p; ev[n].c = x->v; ev[n].idx = n; n++;
      ev[n].t = x->st + x->du; ev[n].kind = 0; ev[n].a = 0x80 | t->ch; ev[n].b = x->p; ev[n].c = 0; ev[n].idx = n; n++;
    }
    qsort(ev, n, sizeof(MEV), cmpmev);
    c.n = 0;
    bbyte(&c, 0);
    bbyte(&c, 0xff);
    bbyte(&c, 0x03);
    bvl(&c, ln);
    bput(&c, t->name, ln);
    if (t->ch != 9)
    {
      bbyte(&c, 0);
      bbyte(&c, 0xc0 | t->ch);
      bbyte(&c, t->prog);
    }
    for (i = 0; i < n; i++)
    {
      bvl(&c, ev[i].t - last);
      bbyte(&c, ev[i].a);
      bbyte(&c, ev[i].b & 0x7f);
      bbyte(&c, ev[i].c);
      last = ev[i].t;
    }
    bbyte(&c, 0);
    bbyte(&c, 0xff);
    bbyte(&c, 0x2f);
    bbyte(&c, 0);
    bput(&out, "MTrk", 4);
    b32(&out, c.n);
    bput(&out, c.d, c.n);
    free(ev);
  }
  free(c.d);
  *len = out.n;
  return out.d;
}

// ---------------------------------------------------------------------------
// Exporter (enhanced.Exporter)
// ---------------------------------------------------------------------------

static const struct { const char *g; int p; } GMFAM[] = {
  {"Piano", 0}, {"Keyboard", 4}, {"Mallet", 8}, {"Orgel", 16}, {"Akkordeon", 21}, {"Gitarre", 24}, {"Streicher", 40},
  {"Chor", 52}, {"Blaeser", 56}, {"Floete", 72}, {"Synth", 80}, {"Flaeche", 88}, {"Zupf", 104}};
static const int TOM_GM[6] = {50, 48, 47, 45, 43, 41};

typedef struct
{
  COMP *c;
  int prog_of[SG_MAXINS], has_prog[SG_MAXINS];
  unsigned char used[128];
  int nused;
  int gm_note[128], ngm;              // gm_ins in insertion order
  char gm_name[128][48];
} EXP;

static int isdrumcat(const char *cat) { return tb_isdrum(cat); }

static int ex_program(EXP *x, int ins)
{
  const SGINS *i = &x->c->ins[ins];
  int base = !strcmp(i->cat, "pad") ? 88 : 80, k, p = -1;
  if (x->has_prog[ins]) return x->prog_of[ins];
  for (k = 0; k < (int)(sizeof GMFAM / sizeof GMFAM[0]); k++)
    if ((i->group[0]) && (!strcmp(GMFAM[k].g, i->group))) base = GMFAM[k].p;
  for (k = 0; k < 8; k++)
    if ((base + k < 128) && (!x->used[base + k])) { p = base + k; break; }
  if (p < 0)
  {
    if (x->nused < 120)
    {
      for (k = 0; k < 128; k++)
        if ((!x->used[k]) && (!((k >= 32) && (k < 40)))) { p = k; break; }
    }
    else p = base;
  }
  if (p < 0) p = base;
  if (!x->used[p]) x->nused++;
  x->used[p] = 1;
  x->has_prog[ins] = 1;
  x->prog_of[ins] = p;
  return p;
}

static int gm_find(EXP *x, int gm)
{
  int k;
  for (k = 0; k < x->ngm; k++)
    if (x->gm_note[k] == gm) return k;
  return -1;
}

static void gm_setdefault(EXP *x, int gm, const char *name)
{
  if ((gm_find(x, gm) >= 0) || (x->ngm >= 128)) return;
  x->gm_note[x->ngm] = gm;
  snprintf(x->gm_name[x->ngm], 48, "%s", name);
  x->ngm++;
}

static const char *kitname(COMP *c, const char *cat)
{
  int k;
  for (k = 0; k < SK_NUM; k++)
    if (!strcmp(sg_kitcat[k], cat)) return c->kit[k];
  return "";
}

static int drum_pitch(EXP *x, const char *name, const char *cat)
{
  static const struct { const char *c; int gm; } DG[] = {
    {"kick", 36}, {"snare", 38}, {"clap", 39}, {"hat", 42}, {"ohat", 46}, {"crash", 49}, {"ride", 51}};
  int k, gm = -1;
  for (k = 0; k < 7; k++)
    if (!strcmp(DG[k].c, cat))
    {
      const char *kn = kitname(x->c, cat);
      gm_setdefault(x, DG[k].gm, kn[0] ? kn : name);
      return DG[k].gm;
    }
  for (k = 0; k < x->ngm; k++)
    if (!strcmp(x->gm_name[k], name)) return x->gm_note[k];
  if (!strcmp(cat, "tom"))
  {
    gm = 45;
    for (k = 0; k < 6; k++)
      if (gm_find(x, TOM_GM[k]) < 0) { gm = TOM_GM[k]; break; }
  }
  else
  {
    gm = 63;
    for (k = 60; k < 82; k++)
      if (gm_find(x, k) < 0) { gm = k; break; }
  }
  gm_setdefault(x, gm, name);
  return gm;
}

// rows of a composer track in the order the events were made (Python dict order)
static const SGTRACK *ordt;
static int cmpord(const void *a, const void *b)
{
  unsigned x = ordt->ev[*(const int *)a].ord, y = ordt->ev[*(const int *)b].ord;
  return x < y ? -1 : x > y;
}

static int ordrows(const SGTRACK *t, int *out)
{
  int r, n = 0;
  for (r = 0; r < t->rows; r++)
    if (t->ev[r].type != EV_NONE) out[n++] = r;
  ordt = t;
  qsort(out, n, sizeof(int), cmpord);
  return n;
}

// the instrument played most often in a voice (first one on a tie, Python's max)
static int most(COMP *c, const SGTRACK *t, const char *cat)
{
  int *rr = (int *)malloc(sizeof(int) * (t->rows + 1)), n = ordrows(t, rr), k, best = -1;
  int cnt[SG_MAXINS], order[SG_MAXINS], no = 0;
  memset(cnt, 0, sizeof cnt);
  for (k = 0; k < n; k++)
  {
    const SGEV *e = &t->ev[rr[k]];
    if ((e->type == EV_NOTE) && (!strcmp(c->ins[e->ins].cat, cat)))
    {
      if (!cnt[e->ins]) order[no++] = e->ins;
      cnt[e->ins]++;
    }
  }
  for (k = 0; k < no; k++)
    if ((best < 0) || (cnt[order[k]] > cnt[best])) best = order[k];
  free(rr);
  return best;
}

typedef struct { int ti, chord; ENLIST notes; int cnt[SG_MAXINS], order[SG_MAXINS], no; char role[32]; } EPART;

// the composed piece as MIDI tracks (Exporter.score) and the hints for the importer
static int score(COMP *c, ETRACK *tr, MIDIOPTS *o)
{
  EXP x;
  EPART *parts = (EPART *)calloc(SG_MAXTRACKS * 2, sizeof(EPART));
  ENLIST drums = {0};
  int *drumkey = (int *)calloc((c->total_rows + 64) * 128, sizeof(int));   // (row, gm) -> index + 1
  int tpr = 480 / c->bt, ti, k, ntr = 0, lead, ch = 2;
  memset(&x, 0, sizeof x);
  x.c = c;
  o->hints = 1;
  o->hint_q = c->bt;
  o->hint_tempo[0] = c->tempo[0];
  o->hint_tempo[1] = c->tempo[1];
  for (ti = 0; ti < c->ntracks; ti++)
  {
    const SGTRACK *t = &c->tracks[ti];
    int *rr = (int *)malloc(sizeof(int) * (t->rows + 1)), n = ordrows(t, rr), i;
    for (i = 0; i < n; i++)
    {
      int r = rr[i], d;
      const SGEV *e = &t->ev[r];
      const char *ct;
      if (e->type != EV_NOTE) continue;
      ct = c->ins[e->ins].cat;
      d = e->dur > 0 ? e->dur : 1;
      if ((isdrumcat(ct)) || (!strcmp(ct, "kickbass")))
      {
        int kb = !strcmp(ct, "kickbass"), gm, vel;
        const char *kn = kitname(c, "kick");
        gm = drum_pitch(&x, kb ? (kn[0] ? kn : c->ins[e->ins].name) : c->ins[e->ins].name, kb ? "kick" : ct);
        vel = ((!strcmp(ct, "kick")) || (!strcmp(ct, "snare")) || (kb)) ? 112 : 90;
        if ((r < c->total_rows + 64) && (gm >= 0) && (gm < 128))
        {
          int *slot = &drumkey[r * 128 + gm];
          if (*slot)
          {
            drums.v[*slot - 1].v = vel;
          }
          else
          {
            en_add(&drums, r * tpr, tpr, gm, vel);
            *slot = drums.n;
          }
        }
        continue;
      }
      if ((!strcmp(ct, "bass")) || (!strcmp(ct, "echo")) || ((ti == 1) && (!strcmp(ct, "lead")))) continue;
      {
        int isarp = !strcmp(ct, "arp");
        EPART *p = &parts[ti * 2 + (isarp ? 0 : 1)];
        int vel = isarp ? 78 : (!strcmp(ct, "pad") ? 72 : 88);
        p->ti = ti;
        p->chord = isarp;
        if ((isarp) && (c->arp_tpl[e->ins] >= 0))
        {
          int q = c->arp_q[e->ins], j;
          for (j = 0; (j < sg_qlen[q]) && (j < 4); j++) en_add(&p->notes, r * tpr, d * tpr, e->note + sg_qiv[q][j] + 12, vel);
        }
        else en_add(&p->notes, r * tpr, d * tpr, e->note + 12, vel);
        if (!p->cnt[e->ins]) p->order[p->no++] = e->ins;
        p->cnt[e->ins]++;
        snprintf(p->role, sizeof p->role, "%s", ti < SG_MAXTRACKS ? c->track_roles[ti] : "");
      }
    }
    free(rr);
  }
  // Drums, Bass, Lead Melody, then the parts sorted by (voice, chord < line)
  snprintf(tr[ntr].name, 48, "Drums");
  tr[ntr].ch = 9;
  tr[ntr].prog = 0;
  qsort(drums.v, drums.n, sizeof(ENOTE), cmpen);
  tr[ntr].notes = drums;
  ntr++;
  snprintf(tr[ntr].name, 48, "Bass");
  tr[ntr].ch = 1;
  tr[ntr].prog = jv_true(comp_style(c, "dance_kick")) ? 38 : 33;
  memset(&tr[ntr].notes, 0, sizeof(ENLIST));
  for (k = 0; k < c->bass_rows.n; k++)
    en_add(&tr[ntr].notes, c->bass_rows.v[k].row * tpr, (c->bass_rows.v[k].dur > 1 ? c->bass_rows.v[k].dur : 1) * tpr,
           c->bass_rows.v[k].note + 12, 100);
  ntr++;
  lead = most(c, &c->tracks[1], "lead");
  if (lead < 0) lead = comp_use(c, c->i_lead);
  snprintf(tr[ntr].name, 48, "Lead Melody");
  tr[ntr].ch = 0;
  tr[ntr].prog = ex_program(&x, lead);
  memset(&tr[ntr].notes, 0, sizeof(ENLIST));
  for (k = 0; k < c->lead_rows.n; k++)
    en_add(&tr[ntr].notes, c->lead_rows.v[k].row * tpr, c->lead_rows.v[k].dur * tpr, c->lead_rows.v[k].note + 12, 100);
  ntr++;
  o->nhint_roles = 0;
  snprintf(o->hint_role_name[o->nhint_roles], 48, "Lead Melody");
  o->hint_role[o->nhint_roles++] = 2;
  snprintf(o->hint_role_name[o->nhint_roles], 48, "Bass");
  o->hint_role[o->nhint_roles++] = 1;
  o->nhint_lead = 0;
  o->hint_lead_prog[o->nhint_lead] = tr[2].prog;
  snprintf(o->hint_lead_name[o->nhint_lead++], 48, "%s", c->ins[lead].name);
  for (k = 0; k < SG_MAXTRACKS * 2; k++)
  {
    EPART *p = &parts[k];
    int name = -1, j, prog;
    const char *ct;
    if (!p->notes.n) continue;
    for (j = 0; j < p->no; j++)
      if ((name < 0) || (p->cnt[p->order[j]] > p->cnt[name])) name = p->order[j];
    ct = c->ins[name].cat;
    snprintf(tr[ntr].name, 48, "%s %d %s", p->chord ? "Chords" : "Line", p->ti, p->role);
    if (!strcmp(ct, "arp"))
    {
      const char *n = c->ins[name].name;
      prog = (strstr(n, "Flaeche") || strstr(n, "Streicher") || strstr(n, "Langsam")) ? 88 : 4;
    }
    else
    {
      prog = ex_program(&x, name);
      for (j = 0; j < o->nhint_lead; j++)
        if (o->hint_lead_prog[j] == prog) break;
      if ((j >= o->nhint_lead) && (o->nhint_lead < 128)) o->nhint_lead++;
      o->hint_lead_prog[j] = prog;
      snprintf(o->hint_lead_name[j], 48, "%s", c->ins[name].name);
      if ((!strcmp(ct, "pad")) && (o->nhint_roles < 64))
      {
        snprintf(o->hint_role_name[o->nhint_roles], 48, "%s", tr[ntr].name);
        o->hint_role[o->nhint_roles++] = 0;
      }
    }
    tr[ntr].ch = ch;
    tr[ntr].prog = prog;
    qsort(p->notes.v, p->notes.n, sizeof(ENOTE), cmpen);
    tr[ntr].notes = p->notes;
    p->notes.v = NULL;
    ntr++;
    ch = ch < 15 ? ch + 1 : 2;
    if (ch == 9) ch = 10;
  }
  o->nhint_gm = x.ngm;
  for (k = 0; k < x.ngm; k++)
  {
    o->hint_gm_note[k] = x.gm_note[k];
    snprintf(o->hint_gm_ins[k], 48, "%s", x.gm_name[k]);
  }
  {
    static const int kidx[8] = {SK_KICK, SK_SNARE, SK_CLAP, SK_HAT, SK_OHAT, SK_CRASH, SK_RIDE, SK_PERC};
    for (k = 0; k < 8; k++) snprintf(o->hint_kit[k], 48, "%s", c->kit[kidx[k]]);
  }
  snprintf(o->hint_bass, 48, "%s", c->i_bass);
  snprintf(o->hint_kickbass, 48, "%s", c->i_kickbass);
  snprintf(o->hint_arp, 48, "%s", c->t_arp);
  snprintf(o->hint_pad, 48, "%s", c->t_pad);
  o->hint_dance = jv_true(comp_style(c, "dance_kick")) ? 1 : 0;
  for (k = 0; k < SG_MAXTRACKS * 2; k++) free(parts[k].notes.v);
  free(parts);
  free(drumkey);
  return ntr;
}

// big: composed for 8 SIDs (comp_compose, 3 voices). Returns the piece for
// 1 or 2 SIDs (kind 1, for sg_buildsongs); big keeps its tracks, its style
// data moves to the result.
COMP *sg_enhance(COMP *big, int sids, int voices, char *err, int errsize)
{
  ETRACK *tr = (ETRACK *)calloc(SG_MAXTRACKS * 2 + 4, sizeof(ETRACK));
  int ntr = 0, len, k;
  unsigned char *mid;
  MIDIOPTS o;
  COMP *c;
  midiopts_default(&o);
  o.sids = sids == 2 ? 2 : 1;
  o.voices = voices == 2 ? 2 : 3;
  o.model = big->model;
  snprintf(o.title, sizeof o.title, "%s", big->title);
  snprintf(o.basename, sizeof o.basename, "%s", big->title);
  ntr = score(big, tr, &o);
  mid = write_midi(tr, ntr, 480, big->bpm, &len);
  for (k = 0; k < ntr; k++) free(tr[k].notes.v);
  free(tr);
  if (getenv("GG_ENHMIDI"))
  {
    FILE *f = fopen(getenv("GG_ENHMIDI"), "wb");
    if (f)
    {
      fwrite(mid, 1, len, f);
      fclose(f);
    }
  }
  c = sg_import_midi(mid, len, &o, NULL, err, errsize);
  free(mid);
  if (!c) return NULL;
  // the style's playing technique on the reduced voices (expression.py)
  c->expressive = 1;
  c->compact = 0;
  c->st = big->st;                 // the style data moves over (big is freed by the caller)
  big->st = NULL;
  c->styleidx = big->styleidx;
  snprintf(c->stylekey, sizeof c->stylekey, "%s", big->stylekey);
  snprintf(c->exprseed, sizeof c->exprseed, "%u-%d-enh", big->seed, big->mel_var);
  c->seed = big->seed;
  c->mel_var = big->mel_var;
  c->nsecs = big->nsecs;
  memcpy(c->secs, big->secs, sizeof c->secs);
  memcpy(c->secbars, big->secbars, sizeof c->secbars);
  c->key = big->key;
  c->mode = big->mode;
  snprintf(c->layout, sizeof c->layout, "Enhanced: 8 SIDs (%d voices) -> %d SID%s", big->ntracks, o.sids, o.sids > 1 ? "s" : "");
  return c;
}

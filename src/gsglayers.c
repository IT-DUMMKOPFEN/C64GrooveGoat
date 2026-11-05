// C64 GrooveGoat - SIDGen layers for 4, 6 and 8 SIDs: extra voices on top of
// the 2-SID arrangement (port of SIDGen layers.py by IT-DUMMKOPFEN)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gsgcomp.h"
#include "gsgtimbre.h"

enum { L_DRUMS2 = 0, L_PAD1, L_COUNTER2, L_BASS2, L_LEAD2, L_PAD2, L_PERC, L_ECHO2, L_ARP2, L_PAD3, L_BROKEN2, L_CLAP,
       L_SUB, L_LEAD3, L_STRINGS, L_KICK2, L_HARM2, L_PAD4, L_NUM };
static const char *slotname[L_NUM] = {"drums2", "pad1", "counter2", "bass2", "lead2", "pad2", "perc", "echo2", "arp2", "pad3",
  "broken2", "clap", "sub", "lead3", "strings", "kick2", "harm2", "pad4"};
static const char *rolename[L_NUM] = {"Drums 2", "Pad", "Counter melody 2", "Bass 2", "Melody 2 (octave)", "Pad 2", "Percussion",
  "Echo 2", "Arpeggio 2", "Pad 3", "Offbeat chords", "Clap layer", "Sub bass", "Melody 3 (low)", "Strings", "Kick layer",
  "Harmony 2", "Pad 4"};

static int ispad(int s) { return (s == L_PAD1) || (s == L_PAD2) || (s == L_PAD3) || (s == L_PAD4); }

int layer_slots(int sids, int has_kit, int *out)
{
  static const int L4[6] = {L_DRUMS2, L_PAD1, L_COUNTER2, L_BASS2, L_LEAD2, L_PAD2};
  static const int L6[6] = {L_PERC, L_ECHO2, L_ARP2, L_PAD3, L_BROKEN2, L_CLAP};
  static const int L8[6] = {L_SUB, L_LEAD3, L_STRINGS, L_KICK2, L_HARM2, L_PAD4};
  int n = 0, k;
  if (sids >= 4) for (k = 0; k < 6; k++) out[n++] = L4[k];
  if (sids >= 6) for (k = 0; k < 6; k++) out[n++] = L6[k];
  if (sids >= 8) for (k = 0; k < 6; k++) out[n++] = L8[k];
  if (!has_kit)
    for (k = 0; k < n; k++)
    {
      if (out[k] == L_DRUMS2) out[k] = L_ARP2;
      else if (out[k] == L_PERC) out[k] = L_BROKEN2;
      else if (out[k] == L_CLAP) out[k] = L_STRINGS;
      else if (out[k] == L_KICK2) out[k] = L_COUNTER2;
    }
  return n;
}

static int slots[SG_MAXTRACKS], nslots;

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------

static int valid(const char **in, int n, const char **out)
{
  int k, m = 0, d;
  for (k = 0; k < n; k++)
  {
    int dup = 0;
    if ((!in[k]) || (!sg_findins(in[k]))) continue;
    for (d = 0; d < m; d++)
      if (!strcmp(out[d], in[k])) dup = 1;
    if (!dup) out[m++] = in[k];
  }
  return m;
}

static double pyround2(double x)
{
  double y = x * 100, f = floor(y), d = y - f;
  if (d > 0.5) f += 1;
  else if (d == 0.5) f = fmod(f, 2.0) == 0 ? f : f + 1;
  return f / 100;
}

static int far_from(SGRNG *rng, const char **cands0, int n0, const char **against, int na, char *out)
{
  static const char *v[2048], *cands[2048];
  int nv = valid(cands0, n0, v), n = 0, k, a, nag = 0, best = -1;
  const SGINS *ag[32];
  double bs = 0, br = 0;
  for (k = 0; k < nv; k++)
  {
    int in = 0;
    for (a = 0; a < na; a++)
      if ((against[a]) && (!strcmp(against[a], v[k]))) in = 1;
    if (!in) cands[n++] = v[k];
  }
  if (!n) return 0;
  for (a = 0; (a < na) && (nag < 32); a++)
  {
    const SGINS *i = against[a] ? sg_findins(against[a]) : NULL;
    if (i) ag[nag++] = i;
  }
  if (!nag)
  {
    snprintf(out, 48, "%s", cands[rng_below(rng, n)]);
    return 1;
  }
  for (k = 0; k < n; k++)
  {
    double m = -1, rr;
    const SGINS *ci = sg_findins(cands[k]);
    for (a = 0; a < nag; a++)
    {
      double s = tb_similarity(ci, ag[a], 60, 60);
      if (s > m) m = s;
    }
    m = pyround2(m);
    rr = rng_random(rng);
    if ((best < 0) || (m < bs) || ((m == bs) && (rr < br)))
    {
      best = k;
      bs = m;
      br = rr;
    }
  }
  snprintf(out, 48, "%s", cands[best]);
  return 1;
}

// random.sample(range(n), k) as Python (pool method for small n, set method otherwise)
static int pysample(SGRNG *r, int n, int k, int *out)
{
  int setsize = 21, i;
  if (k > n) k = n;
  if (k > 5)
  {
    double l = log(k * 3.0) / log(4.0);
    int e = (int)ceil(l), p = 1;
    while (e-- > 0) p *= 4;
    setsize += p;
  }
  if (n <= setsize)
  {
    int *pool = (int *)malloc(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) pool[i] = i;
    for (i = 0; i < k; i++)
    {
      int j = rng_below(r, n - i);
      out[i] = pool[j];
      pool[j] = pool[n - i - 1];
    }
    free(pool);
  }
  else
  {
    unsigned char *sel = (unsigned char *)calloc(n, 1);
    for (i = 0; i < k; i++)
    {
      int j = rng_below(r, n);
      while (sel[j]) j = rng_below(r, n);
      sel[j] = 1;
      out[i] = j;
    }
    free(sel);
  }
  return k;
}

// GL.pool(model, cat): generated instruments of a kind (no filter, no theme)
static int genpool(int model, const char *cat, int *idx)
{
  int k, n = 0, ng = sg_numgen(model);
  for (k = 0; k < ng; k++)
  {
    const SGINS *g = sg_gen(model, k);
    if ((strcmp(g->cat, cat)) || (g->filt[0]) || (g->theme[0])) continue;
    idx[n++] = k;
  }
  return n;
}

static int gen(COMP *c, SGRNG *rl, const char *cat, int k, const char **out)
{
  static int idx[4096], pick[512];
  int n = genpool(c->model, cat, idx), m, i;
  if (!n) return 0;
  m = pysample(rl, n, k < n ? k : n, pick);
  for (i = 0; i < m; i++) out[i] = sg_gen(c->model, idx[pick[i]])->name;
  return m;
}

static const char *PADMAP[][4] = {
  {"Arp Streicher", "Streicher", "Streicher warm", "Synth Str"}, {"Arp String Machine", "String Machine", "Synth Str", 0},
  {"Arp Orgel", "Orgel", "Orgel Jazz", 0}, {"Arp Kirche", "Orgel Kirche", "Chor", 0}, {"Arp Glas", "Pad Glas", "Pad Hell", 0},
  {"Arp Flaeche", "Weiche Flaeche", "PWM Flaeche", "Pad Warm PWM"}, {"Arp Akkordeon", "Akkordeon", 0, 0},
  {"Arp Brass", "Blech", "Horn", 0}, {"Arp E-Piano", "E-Piano", "Rhodes", 0}, {"Arp Gitarre", "Gitarre clean", 0, 0},
  {"Arp Harfe", "Harfe", "Streicher", 0}, {"Arp Bordun", "Pad Quinte", "Tagelharpa", 0}, {"Arp Leier", "Leier", "Pad Quinte", 0},
  {"Arp Trance", "Synth Str", "Juno Pad", 0}, {"Arp Rave", "Juno Pad", "Synth Str", 0}};
static const char *PADS[] = {"Weiche Flaeche", "PWM Flaeche", "Pad Warm PWM", "Pad Hell", "Pad Dunkel", "Juno Pad", "Synth Str",
  "Streicher", "Streicher warm", "Chor Ooh", "Pad Atmo", "String Machine"};
static const char *STRINGS[] = {"Streicher", "Streicher hell", "Synth Str", "String Machine", "Tremolo Str", "Streicher warm"};

static int padmap(const char *t, const char **out, int n)
{
  int k, j;
  for (k = 0; k < (int)(sizeof PADMAP / sizeof PADMAP[0]); k++)
    if (!strcmp(PADMAP[k][0], t))
      for (j = 1; (j < 4) && (PADMAP[k][j]); j++) out[n++] = PADMAP[k][j];
  return n;
}

static int jvlist(const JV *l, const char **out, int n)
{
  int k;
  for (k = 0; k < jv_len(l); k++) out[n++] = jv_str(jv_at(l, k), "");
  return n;
}

void comp_pick_layer_instruments(COMP *c)
{
  SGRNG rl;
  const char *cands[2048], *ag[16];
  int n, na, k;
  char tmp[48];
  const JV *bi = comp_style(c, "bass_inst"), *li = comp_style(c, "lead_inst"), *ci = comp_style(c, "counter_inst");
  rng_seeds(&rl, "%u-lay-%d", c->seed, c->ins_var);
  nslots = layer_slots(c->sids, jv_len(comp_style(c, "drums")) > 0, slots);
  na = 0;
  ag[na++] = c->i_lead;
  ag[na++] = c->i_lead_b;
  if (c->i_harm[0]) ag[na++] = c->i_harm;
  // bass 2
  n = jvlist(bi, cands, 0);
  n += gen(c, &rl, "bass", 10, cands + n);
  {
    const char *a1[1] = {c->i_bass};
    if (far_from(&rl, cands, n, a1, 1, tmp)) snprintf(c->i_bass2, 48, "%s", tmp);
    else snprintf(c->i_bass2, 48, "%s", c->i_bass);
  }
  // sub bass: triangle basses
  {
    static const char *all[2048], *v[2048];
    const char *tri[2048];
    int nall = jvlist(bi, all, 0), nv, nt = 0, nh = sg_handcount();
    for (k = 0; k < nh; k++)
      if (!strcmp(sg_hand(k)->cat, "bass")) all[nall++] = sg_hand(k)->name;
    nv = valid(all, nall, v);
    nv += gen(c, &rl, "bass", 60, v + nv);
    for (k = 0; k < nv; k++)
    {
      const SGINS *i = sg_findins(v[k]);
      if ((i) && (sg_mainwave(i) == 0x10)) tri[nt++] = v[k];
    }
    snprintf(c->i_sub, 48, "%s", nt ? tri[rng_below(&rl, nt)] : c->i_bass);
  }
  n = jvlist(li, cands, 0);
  n = jvlist(ci, cands, n);
  if (far_from(&rl, cands, n, ag, na, tmp)) snprintf(c->i_lead2, 48, "%s", tmp);
  else snprintf(c->i_lead2, 48, "%s", c->i_lead);
  ag[na] = c->i_lead2;
  if (far_from(&rl, cands, n, ag, na + 1, tmp)) snprintf(c->i_lead3, 48, "%s", tmp);
  else snprintf(c->i_lead3, 48, "%s", c->i_lead);
  {
    const char *pl[16], *pv[16];
    int np = padmap(c->t_pad, pl, 0), nv;
    np = padmap(c->t_arp, pl, np);
    nv = valid(pl, np, pv);
    if (nv) snprintf(c->i_padv, 48, "%s", pv[rng_below(&rl, nv)]);
    else
    {
      const char *pp[16];
      int npp = valid(PADS, 12, pp);
      snprintf(c->i_padv, 48, "%s", pp[rng_below(&rl, npp)]);
    }
  }
  n = 0;
  for (k = 0; k < 12; k++) cands[n++] = PADS[k];
  n += gen(c, &rl, "pad", 6, cands + n);
  {
    const char *a1[1] = {c->i_padv};
    if (far_from(&rl, cands, n, a1, 1, tmp)) snprintf(c->i_pad2v, 48, "%s", tmp);
    else snprintf(c->i_pad2v, 48, "%s", c->i_padv);
  }
  {
    const char *a2[2] = {c->i_padv, c->i_pad2v};
    if (far_from(&rl, STRINGS, 6, a2, 2, tmp)) snprintf(c->i_strings, 48, "%s", tmp);
    else snprintf(c->i_strings, 48, "%s", c->i_padv);
  }
  n = jvlist(ci, cands, 0);
  n = jvlist(li, cands, n);
  ag[na] = c->i_counter;
  ag[na + 1] = c->i_lead2;
  if (far_from(&rl, cands, n, ag, na + 2, tmp)) snprintf(c->i_counter2, 48, "%s", tmp);
  else snprintf(c->i_counter2, 48, "%s", c->i_counter);
  ag[na] = c->i_counter2;
  ag[na + 1] = c->i_lead2;
  if (far_from(&rl, cands, n, ag, na + 2, tmp)) snprintf(c->i_harm2, 48, "%s", tmp);
  else snprintf(c->i_harm2, 48, "%s", c->i_counter);
  n = jvlist(comp_style(c, "broken_inst"), cands, 0);
  n = jvlist(li, cands, n);
  {
    const char *a2[2] = {c->i_broken, c->i_lead};
    if (far_from(&rl, cands, n, a2, 2, tmp)) snprintf(c->i_broken2, 48, "%s", tmp);
    else snprintf(c->i_broken2, 48, "%s", c->i_broken);
  }
  {
    const JV *ai = comp_style(c, "arp_inst");
    const char *arps[64];
    int nar = 0;
    for (k = 0; k < jv_len(ai); k++)
    {
      const char *a = jv_str(jv_at(ai, k), "");
      if ((strcmp(a, c->t_arp)) && (strcmp(a, c->t_arp2))) arps[nar++] = a;
    }
    if (!nar)
      for (k = 0; k < sg_arpcount(); k++)
      {
        const char *a = sg_arpat(k)->name;
        if ((strcmp(a, c->t_arp)) && (strcmp(a, c->t_pad))) arps[nar++] = a;
      }
    snprintf(c->t_arp3, 48, "%s", arps[rng_below(&rl, nar)]);
  }
  {
    const JV *kk = jv_get(comp_style(c, "kit"), "kick");
    const char *kicks[64], *vk[64];
    int nk = 0, nv;
    for (k = 0; k < jv_len(kk); k++)
    {
      const char *s = jv_str(jv_at(kk, k), "");
      if (strcmp(s, c->kit[SK_KICK])) kicks[nk++] = s;
    }
    if (!jv_true(comp_style(c, "dance_kick"))) nk += gen(c, &rl, "kick", 4, kicks + nk);
    nv = valid(kicks, nk, vk);
    if (nv)
    {
      // _valid() is called twice in SIDGen: the choice is from the same list
      snprintf(c->i_kick2, 48, "%s", vk[rng_below(&rl, nv)]);
    }
    else snprintf(c->i_kick2, 48, "%s", c->kit[SK_KICK]);
  }
  snprintf(c->i_clap, 48, "%s", c->kit[SK_CLAP][0] ? c->kit[SK_CLAP] : c->kit[SK_SNARE]);
  c->layer_bass_shift = rng_random(&rl) < 0.65;
  if (c->model == 8580)
  {
    char *f[] = {c->i_bass2, c->i_sub, c->i_lead2, c->i_lead3, c->i_padv, c->i_pad2v, c->i_strings, c->i_counter2, c->i_harm2,
                 c->i_broken2, c->i_kick2, c->i_clap};
    for (k = 0; k < 12; k++)
      if (f[k][0]) snprintf(f[k], 48, "%s", sg_formodel(f[k], 8580));
  }
}

void comp_layer_roles(COMP *c)
{
  int k;
  for (k = 0; (k < nslots) && (6 + k < SG_MAXTRACKS); k++) snprintf(c->track_roles[6 + k], 32, "%s", rolename[slots[k]]);
}

// ---------------------------------------------------------------------------
// Arrangement
// ---------------------------------------------------------------------------

typedef struct
{
  RND *lead;
  int nlead, has_drums, dance, sec, last, hasbass;
  int *bn, *bd, *drums;
} LCTX;

static int active(int slot, int sec, ARR *a, LCTX *x)
{
  int b = sec_base(sec);
  switch (slot)
  {
    case L_DRUMS2: case L_KICK2: case L_PERC: return x->has_drums;
    case L_CLAP: return x->has_drums && (a->drums == 0) && ((b == SEC_B) || (b == SEC_OUTRO));
    case L_BASS2: return x->hasbass;
    case L_SUB: return x->hasbass && (b != SEC_INTRO);
    case L_LEAD2: case L_LEAD3: return (x->nlead > 0) && (b == SEC_B);
    case L_HARM2: return (x->nlead > 0) && ((b == SEC_B) || (b == SEC_C));
    case L_ECHO2: return (x->nlead > 0) && ((b == SEC_A) || (b == SEC_B));
    case L_COUNTER2: return (b == SEC_A) || (b == SEC_C) || (b == SEC_INTRO);
    case L_BROKEN2: return (b == SEC_A) || (b == SEC_C) || (b == SEC_OUTRO) || (b == SEC_B);
    case L_ARP2: return (b == SEC_INTRO) || (b == SEC_B) || (b == SEC_C);
    case L_PAD1: case L_PAD2: return 1;
    case L_PAD3: case L_PAD4: case L_STRINGS: return (b == SEC_INTRO) || (b == SEC_B) || (b == SEC_C) || (b == SEC_OUTRO);
  }
  return 1;
}

typedef struct { int b, ln; CHORD c; } SPAN;

static void render_drums_l(COMP *c, SGTRACK *t, int row0, const int *drums, int n)
{
  static const struct { int bit; int cat; } prio[9] = {{DL_X, SK_CRASH}, {DL_S, SK_SNARE}, {DL_C, SK_CLAP}, {DL_K, SK_KICK},
    {DL_T, -1}, {DL_O, SK_OHAT}, {DL_H, SK_HAT}, {DL_R, SK_RIDE}, {DL_P, SK_PERC}};
  int s, k;
  for (s = 0; s < n; s++)
    for (k = 0; k < 9; k++)
      if (drums[s] & prio[k].bit)
      {
        const char *name;
        if (prio[k].cat < 0)
        {
          const JV *toms = jv_get(comp_style(c, "kit"), "tom");
          int nt = jv_len(toms), pos;
          if (!nt) name = "Tom mittel";
          else
          {
            pos = (s % c->spb) * nt / c->spb;
            if (pos > nt - 1) pos = nt - 1;
            name = jv_str(jv_at(toms, pos), "Tom mittel");
          }
        }
        else name = c->kit[prio[k].cat][0] ? c->kit[prio[k].cat] : c->kit[SK_SNARE];
        tr_note(t, row0 + s, 48, comp_use(c, name), -1);
        break;
      }
}

static int perm_next(int *a, int n)
{
  int i = n - 2, j, t;
  while ((i >= 0) && (a[i] >= a[i + 1])) i--;
  if (i < 0) return 0;
  j = n - 1;
  while (a[j] <= a[i]) j--;
  t = a[i]; a[i] = a[j]; a[j] = t;
  for (i = i + 1, j = n - 1; i < j; i++, j--) { t = a[i]; a[i] = a[j]; a[j] = t; }
  return 1;
}

// chord tones for n pad voices per chord, moving as little as possible
static void pad_voicing(COMP *c, SPAN *spans, int nspans, int n, int voicing[][4])
{
  int s, *prev = c->npad_prev ? c->pad_prev : NULL, prevbuf[4];
  for (s = 0; s < nspans; s++)
  {
    int root = spans[s].c.root, q = spans[s].c.q, tones[4], nt = sg_qlen[q], order[4], want[4], i;
    int best[4], bestcost = -1, havebest = 0, perm[4];
    for (i = 0; i < nt; i++) tones[i] = (root + sg_qiv[q][i]) % 12;
    if (nt > 1)
    {
      for (i = 0; i < nt - 1; i++) order[i] = tones[i + 1];
      order[nt - 1] = tones[0];
    }
    else order[0] = tones[0];
    for (i = 0; i < n; i++) want[i] = order[i % nt];
    // all distinct permutations: sorted, then next_permutation (min() does not depend on the order)
    for (i = 0; i < n; i++) perm[i] = want[i];
    {
      int a, b;
      for (a = 0; a < n; a++)
        for (b = a + 1; b < n; b++)
          if (perm[b] < perm[a]) { int t = perm[a]; perm[a] = perm[b]; perm[b] = t; }
    }
    do
    {
      int notes[4], cost = 0, ok = 1, a, b;
      for (i = 0; i < n; i++)
      {
        int ref = prev ? prev[i] : 55 + 4 * i, m, bm = -1;
        for (m = 50; m < 74; m++)
          if ((m % 12 == perm[i]) && ((bm < 0) || (abs(m - ref) < abs(bm - ref)))) bm = m;
        notes[i] = bm;
      }
      for (a = 0; a < n; a++)
        for (b = a + 1; b < n; b++)
          if (notes[a] == notes[b]) ok = 0;
      if (!ok) continue;
      for (i = 0; i < n; i++) cost += abs(notes[i] - (prev ? prev[i] : 55 + 4 * i));
      // min over (cost, notes): smaller cost, then the smaller note list
      {
        int better = 0;
        if (!havebest) better = 1;
        else if (cost < bestcost) better = 1;
        else if (cost == bestcost)
        {
          for (i = 0; i < n; i++)
            if (notes[i] != best[i]) { better = notes[i] < best[i]; break; }
        }
        if (better)
        {
          havebest = 1;
          bestcost = cost;
          memcpy(best, notes, sizeof(int) * n);
        }
      }
    } while (perm_next(perm, n));
    if (!havebest)
      for (i = 0; i < n; i++) best[i] = 48 + root + 12 * (i / (nt > 1 ? nt : 1)) + sg_qiv[q][i % nt];
    memcpy(voicing[s], best, sizeof(int) * n);
    memcpy(prevbuf, best, sizeof(int) * n);
    prev = prevbuf;
  }
  if (prev)
  {
    memcpy(c->pad_prev, prev, sizeof(int) * n);
    c->npad_prev = n;
  }
}

void comp_render_layers(COMP *c, int sec, int nbars, CHORD *chords_in, int row0, int first, int last, RND *lead, int nlead,
                        int *bn, int *bd, int *drums)
{
  ARR *a = &c->arr[sec];
  int spb = c->spb, bt = c->bt, b, k, s, idx, pads[4], npads = 0, anypad = 0, seen[L_NUM] = {0};
  CHORD chords[256];
  SPAN spans[256];
  int nspans = 0;
  static int voicing[256][4];
  LCTX x;
  (void)first;
  if (nbars > 256) nbars = 256;
  memcpy(chords, chords_in, sizeof(CHORD) * nbars);
  if ((last) && (nbars)) chords[nbars - 1] = c->tonic;
  b = 0;
  while (b < nbars)
  {
    int e = b + 1;
    while ((e < nbars) && (chords[e].root == chords[b].root) && (chords[e].q == chords[b].q)) e++;
    spans[nspans].b = b;
    spans[nspans].ln = e - b;
    spans[nspans].c = chords[b];
    nspans++;
    b = e;
  }
  memset(&x, 0, sizeof x);
  x.lead = lead;
  x.nlead = nlead;
  x.dance = c->dance_kb;
  x.sec = sec;
  x.last = last;
  x.bn = bn;
  x.bd = bd;
  x.drums = drums;
  for (s = 0; s < nbars * spb; s++)
  {
    if (drums[s]) x.has_drums = 1;
    if (bn[s] >= 0) x.hasbass = 1;
  }
  x.has_drums = x.has_drums && (jv_len(comp_style(c, "drums")) > 0);
  {
    static const int ps[4] = {L_PAD1, L_PAD2, L_PAD3, L_PAD4};
    for (k = 0; k < 4; k++)
    {
      int in = 0, j;
      for (j = 0; j < nslots; j++)
        if (slots[j] == ps[k]) in = 1;
      if (in)
      {
        pads[npads++] = ps[k];
        if (active(ps[k], sec, a, &x)) anypad = 1;
      }
    }
  }
  if ((npads) && (anypad)) pad_voicing(c, spans, nspans, npads, voicing);
  for (idx = 0; idx < nslots; idx++)
  {
    int slot = slots[idx], octv;
    SGTRACK *t = &c->tracks[6 + idx];
    SGRNG lr;
    if (!active(slot, sec, a, &x)) continue;
    octv = seen[slot] ? 12 : 0;
    seen[slot] = 1;
    rng_seeds(&lr, "%u-lay-%d-%s", c->seed, row0, slotname[slot]);
    switch (slot)
    {
      case L_DRUMS2:
      {
        static int d2[64 * 16];
        int keep = x.dance ? (DL_S | DL_C | DL_X | DL_T) : (DL_K | DL_S | DL_C | DL_X | DL_T);
        for (s = 0; s < nbars * spb; s++) d2[s] = drums[s] & keep;
        render_drums_l(c, t, row0, d2, nbars * spb);
      }
      break;
      case L_KICK2:
      {
        int name = comp_use(c, c->i_kick2);
        for (s = 0; s < nbars * spb; s++)
          if (drums[s] & DL_K) tr_note(t, row0 + s, 48, name, -1);
      }
      break;
      case L_PERC:
      {
        static int st[64 * 16];
        for (s = 0; s < nbars * spb; s++) st[s] = drums[s] & (DL_H | DL_O | DL_P | DL_R);
        if ((spb == 16) && (sec_base(sec) == SEC_B))
          for (s = 0; s < nbars * spb; s++)
            if ((s % 2 == 1) && (!st[s]) && (!(drums[s] & (DL_K | DL_S | DL_C)))) st[s] = DL_H;
        render_drums_l(c, t, row0, st, nbars * spb);
      }
      break;
      case L_CLAP:
      {
        int name = comp_use(c, c->i_clap);
        for (s = 0; s < nbars * spb; s++)
          if (drums[s] & (DL_S | DL_C)) tr_note(t, row0 + s, 48, name, -1);
      }
      break;
      case L_BASS2:
      {
        int shift = c->layer_bass_shift ? 12 : 0, bi = comp_use(c, c->i_bass2);
        if ((x.dance) && (x.has_drums))
        {
          int kick = comp_use(c, c->kit[SK_KICK]), kick_end = -1;
          for (s = 0; s < nbars * spb; s++)
          {
            if (drums[s] & DL_K)
            {
              tr_note(t, row0 + s, 48, kick, -1);
              kick_end = s + 2;
              continue;
            }
            if ((bn[s] >= 0) && (s >= kick_end)) tr_note(t, row0 + s, bn[s] + shift, bi, bd[s]);
          }
          break;
        }
        for (s = 0; s < nbars * spb; s++)
          if (bn[s] >= 0) tr_note(t, row0 + s, bn[s] + shift, bi, bd[s]);
      }
      break;
      case L_SUB:
      {
        int name = comp_use(c, c->i_sub);
        for (k = 0; k < nspans; k++)
        {
          int n = comp_bass_root(spans[k].c.root) - 12;
          tr_note(t, row0 + spans[k].b * spb, n >= 16 ? n : n + 12, name, spans[k].ln * spb);
        }
      }
      break;
      case L_LEAD2:
      {
        int name = comp_use(c, c->i_lead2);
        for (k = 0; k < nlead; k++)
        {
          int n = lead[k].note, m = n + 12 <= 86 ? n + 12 : n;
          tr_note(t, row0 + lead[k].row, m, name, lead[k].dur);
        }
      }
      break;
      case L_LEAD3:
      {
        int name = comp_use(c, c->i_lead3);
        for (k = 0; k < nlead; k++)
        {
          int n = lead[k].note;
          tr_note(t, row0 + lead[k].row, n - 12 >= 43 ? n - 12 : n, name, lead[k].dur);
        }
      }
      break;
      case L_HARM2:
      {
        int name = comp_use(c, c->i_harm2);
        for (k = 0; k < nlead; k++)
        {
          int n = lead[k].note, m, best = -1, r = row0 + lead[k].row;
          CHORD ch = c->chord_rows[r < c->nchordrows ? r : c->nchordrows - 1];
          int pcs = comp_chord_pcs(ch);
          for (m = n - 12; m < n - 4; m++)
            if ((pcs >> (((m % 12) + 12) % 12)) & 1)
              if ((best < 0) || (abs((n - m) - 8) < abs((n - best) - 8)) || ((abs((n - m) - 8) == abs((n - best) - 8)) && (m < best)))
                best = m;
          if (best >= 0) tr_note(t, row0 + lead[k].row, best, name, lead[k].dur);
        }
      }
      break;
      case L_ECHO2:
      {
        int keep = c->echo_delay;
        c->echo_delay = 2 * bt + keep < spb ? 2 * bt + keep : spb;
        comp_render_mode2(c, t, row0, nbars, chords_in, MD_ECHO, lead, nlead, last, NULL, 0);
        c->echo_delay = keep;
      }
      break;
      case L_COUNTER2:
      {
        char keep[48];
        SGRNG *krng = c->cur;
        SGTRACK tmp;
        int r;
        snprintf(keep, 48, "%s", c->i_counter);
        snprintf(c->i_counter, 48, "%s", c->i_counter2);
        c->cur = &lr;
        tr_init(&tmp, c->total_rows + 64);
        comp_render_mode2(c, &tmp, row0, nbars, chords_in, MD_COUNTER, lead, nlead, last, NULL, 0);
        for (r = 0; r < tmp.rows; r++)
          if (tmp.ev[r].type == EV_NOTE) tr_note(t, r, tmp.ev[r].note + octv, tmp.ev[r].ins, tmp.ev[r].dur);
        tr_free(&tmp);
        snprintf(c->i_counter, 48, "%s", keep);
        c->cur = krng;
      }
      break;
      case L_BROKEN2:
      {
        int name = comp_use(c, c->i_broken2), e8 = bt == 4 ? 2 : 1;
        for (b = 0; b < nbars; b++)
        {
          int root = chords[b].root, q = chords[b].q, r = 60 + root < 67 ? 60 + root : 48 + root, seq[4];
          const int *iv = sg_qiv[q];
          seq[0] = r + iv[1];
          seq[1] = r + iv[2 % sg_qlen[q]];
          seq[2] = r + 12;
          seq[3] = r + iv[2 % sg_qlen[q]];
          for (k = 0; k < 4; k++) tr_note(t, row0 + b * spb + k * bt + e8, seq[k] + octv, name, bt - e8 > 1 ? bt - e8 : 1);
        }
      }
      break;
      case L_ARP2:
        for (k = 0; k < nspans; k++)
          tr_note(t, row0 + spans[k].b * spb, 60 + spans[k].c.root + octv, comp_use_arp(c, c->t_arp3, spans[k].c.q), spans[k].ln * spb);
      break;
      case L_STRINGS:
      {
        int name = comp_use(c, c->i_strings), prev = c->str_prev;
        for (k = 0; k < nspans; k++)
        {
          int pcs = comp_chord_pcs(spans[k].c), n, best = -1;
          for (n = 67; n < 80; n++)
            if (((pcs >> (n % 12)) & 1) && ((best < 0) || (abs(n - prev) < abs(best - prev)))) best = n;
          if (best < 0) best = prev;
          tr_note(t, row0 + spans[k].b * spb, best + octv, name, spans[k].ln * spb);
          prev = best;
        }
        c->str_prev = prev;
      }
      break;
      default:
      if (ispad(slot))
      {
        int kk = 0, name;
        for (k = 0; k < npads; k++)
          if (pads[k] == slot) kk = k;
        name = comp_use(c, ((slot == L_PAD1) || (slot == L_PAD2)) ? c->i_padv : c->i_pad2v);
        for (k = 0; k < nspans; k++) tr_note(t, row0 + spans[k].b * spb, voicing[k][kk], name, spans[k].ln * spb);
      }
      break;
    }
  }
}

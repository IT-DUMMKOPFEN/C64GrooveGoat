// C64 GrooveGoat - SIDGen composer, part 1: choices, structure, material,
// melody, bass and drum figures (port of SIDGen composer.py by IT-DUMMKOPFEN)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gsgcomp.h"
#include "gsgtimbre.h"
#include "gsgexpr.h"

const char *sg_secname[SEC_NUM] = {"intro", "A", "B", "C", "outro", "A2", "B2", "C2"};
const char *sg_kitcat[SK_NUM] = {"kick", "snare", "hat", "ohat", "clap", "crash", "tom", "ride", "perc"};
const char *sg_modes2[MD_NUM] = {"arp_hold", "arp_rhythm", "pad", "echo", "counter", "broken", "harmony", "fifths"};

int sec_base(int s) { return s == SEC_A2 ? SEC_A : s == SEC_B2 ? SEC_B : s == SEC_C2 ? SEC_C : s; }

static int sec_index(const char *s)
{
  int k;
  for (k = 0; k < SEC_NUM; k++)
    if (!strcmp(sg_secname[k], s)) return k;
  return SEC_A;
}

int md_index(const char *s)
{
  int k;
  if (!s) return MD_ARP_HOLD;
  for (k = 0; k < MD_NUM; k++)
    if (!strcmp(sg_modes2[k], s)) return k;
  return MD_ARP_HOLD;
}

int dl_bit(char c)
{
  switch (c)
  {
    case 'k': return DL_K;
    case 's': return DL_S;
    case 'c': return DL_C;
    case 'o': return DL_O;
    case 'h': return DL_H;
    case 'r': return DL_R;
    case 't': return DL_T;
    case 'p': return DL_P;
    case 'x': return DL_X;
  }
  return 0;
}

void rl_add(RNDLIST *l, int row, int note, int dur)
{
  if (l->n >= l->cap) l->v = (RND *)realloc(l->v, sizeof(RND) * (l->cap = l->cap * 2 + 256));
  l->v[l->n].row = row;
  l->v[l->n].note = note;
  l->v[l->n].dur = dur;
  l->n++;
}

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------

JV *style_parse(int i)
{
  if ((i < 0) || (i >= sg_numstyles)) return NULL;
  return jv_parse(sg_styles[i].json);
}

int style_count(void) { return sg_numstyles; }

int style_numgenres(void)
{
  int i, n = 0;
  for (i = 0; i < sg_numstyles; i++)
    if ((!i) || (strcmp(sg_styles[i].key, sg_styles[i - 1].key))) n++;
  return n;
}

int style_genre_first(int g)
{
  int i, n = -1;
  for (i = 0; i < sg_numstyles; i++)
  {
    if ((!i) || (strcmp(sg_styles[i].key, sg_styles[i - 1].key))) n++;
    if (n == g) return i;
  }
  return 0;
}

int style_genre_count(int g)
{
  int i = style_genre_first(g), n = 0;
  while ((i + n < sg_numstyles) && (!strcmp(sg_styles[i + n].key, sg_styles[i].key))) n++;
  return n;
}

// ---------------------------------------------------------------------------
// Instruments of the piece
// ---------------------------------------------------------------------------

static int findused(COMP *c, const char *name)
{
  int k;
  for (k = 0; k < c->numins; k++)
    if (!strcmp(c->ins[k].name, name)) return k;
  return -1;
}

int comp_use(COMP *c, const char *name)
{
  const SGINS *s;
  int k;
  name = sg_formodel(sg_removed_name(name), c->model);
  k = findused(c, name);
  if (k >= 0) return k;
  s = sg_findins(name);
  if ((!s) || (c->numins >= SG_MAXINS))
  {
    // unknown instrument: the first one (never fails)
    if (c->numins) return 0;
    s = sg_findins("Lead Puls");
    if (!s) return 0;
  }
  c->ins[c->numins] = *s;
  c->arp_tpl[c->numins] = -1;
  return c->numins++;
}

int comp_use_arp(COMP *c, const char *tpl, int q)
{
  const SGARP *t = sg_findarp(tpl);
  SGINS tmp;
  int k;
  if (!t) t = sg_findarp("Arp Puls");
  if (!t) return comp_use(c, "Lead Puls");
  sg_makearp(t, q, &tmp);
  k = findused(c, tmp.name);
  if (k >= 0) return k;
  if (c->numins >= SG_MAXINS) return 0;
  c->ins[c->numins] = tmp;
  c->arp_tpl[c->numins] = sg_arpindex(t);
  c->arp_q[c->numins] = q;
  snprintf(c->ins[c->numins].theme, sizeof c->ins[c->numins].theme, "%s", "");
  // the template name (for swaps / filter roles)
  snprintf(c->ins[c->numins].group, sizeof c->ins[c->numins].group, "%.15s", t->name + 4);
  return c->numins++;
}

// ---------------------------------------------------------------------------
// Helpers like Python's
// ---------------------------------------------------------------------------

static const JV *S(COMP *c, const char *key) { return jv_get(c->st, key); }

static int wchoice_jv(SGRNG *r, const JV *list) { return jv_wchoice(r, list); }

static void cpyname(char *dst, const char *src)
{
  char tmp[48];
  snprintf(tmp, 48, "%s", src ? src : "");     // src may be dst itself
  memcpy(dst, tmp, 48);
}

static const char *TITLE_A[] = {"Neon", "Cyber", "Silicon", "Pixel", "Chrome", "Laser", "Midnight", "Turbo", "Galaxy",
  "Digital", "Retro", "Electric", "Crystal", "Shadow", "Solar", "Lunar", "Binary", "Vector",
  "Analog", "Phantom", "Cosmic", "Raster", "Sprite", "Jupiter", "Polar", "Golden", "Hyper"};
static const char *TITLE_B[] = {"Dreams", "Drive", "Runner", "Horizon", "Odyssey", "Pulse", "Voyage", "Machine", "Fever",
  "Nights", "Rider", "Echoes", "Paradise", "Quest", "Storm", "Signal", "Memories", "Waves",
  "Escape", "Legend", "Rhapsody", "Groove", "Overdrive", "Interlude", "Anthem", "Orbit"};

void sg_make_title(SGRNG *rng, const JV *st, char *out, int size)
{
  const JV *sa = jv_get(st, "title_a"), *sb = jv_get(st, "title_b");
  int na = jv_len(sa), nb = jv_len(sb), num, la, lb;
  double own;
  const char *a, *b;
  static const int nums[] = {64, 128, 2000, 3000, 85, 86, 89};
  int n, useA, useB;
  num = rng_random(rng) < 0.2;
  own = rng_random(rng);
  useA = (na && own < 0.35);
  useB = (nb && (own > 0.2) && (own < 0.55));
  la = useA ? na : 27 + na;
  lb = useB ? nb : 26 + nb;
  {
    int i = rng_below(rng, la);
    a = useA ? jv_str(jv_at(sa, i), "Neon") : (i < 27 ? TITLE_A[i] : jv_str(jv_at(sa, i - 27), "Neon"));
  }
  {
    int i = rng_below(rng, lb);
    b = useB ? jv_str(jv_at(sb, i), "Dreams") : (i < 26 ? TITLE_B[i] : jv_str(jv_at(sb, i - 26), "Dreams"));
  }
  n = nums[rng_below(rng, 7)];
  if (num) snprintf(out, size, "%s %s %d", a, b, n);
  else snprintf(out, size, "%s %s", a, b);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

COMP *comp_new(int styleidx, unsigned seed, double seconds, int sids, int voices, int model)
{
  COMP *c = (COMP *)calloc(1, sizeof(COMP));
  sg_init();
  c->styleidx = styleidx;
  c->st = style_parse(styleidx);
  snprintf(c->stylekey, sizeof c->stylekey, "%s", jv_str(jv_get(c->st, "key"), sg_styles[styleidx].key));
  c->seed = seed ? seed : 1;
  rng_seed(&c->rng, c->seed);
  c->cur = &c->rng;
  c->target = seconds > 0 ? seconds : rng_uniform(&c->rng, 60, 180);
  if (c->target < 60) c->target = 60;
  if (c->target > 600) c->target = 600;
  sg_make_title(&c->rng, c->st, c->title, sizeof c->title);
  c->model = model == 8580 ? 8580 : 6581;
  c->voices = voices == 2 ? 2 : 3;
  c->sids = (sids == 2 || sids == 4 || sids == 6 || sids == 8) ? sids : 1;
  c->want_key = -1;
  c->want_mode = -1;
  rng_seeds(&c->rng_mel, "%u-mel-%d", c->seed, c->mel_var);
  rng_seeds(&c->rng_ins, "%u-ins-%d", c->seed, c->ins_var);
  c->variants = 1;
  c->expressive = 1;
  c->last_lead_note = -1;
  c->str_prev = 72;
  return c;
}

void comp_free(COMP *c)
{
  int k;
  if (!c) return;
  for (k = 0; k < SG_MAXTRACKS; k++) tr_free(&c->tracks[k]);
  free(c->chord_rows);
  free(c->lead_rows.v);
  free(c->bass_rows.v);
  free(c->echo_rows.v);
  free(c->drum_rows);
  jv_free(c->st);
  free(c);
}

// variations: "new melody" / "new instruments" keep the rest
void comp_setvariations(COMP *c, int mel_var, int ins_var)
{
  c->mel_var = mel_var;
  c->ins_var = ins_var;
  rng_seeds(&c->rng_mel, "%u-mel-%d", c->seed, c->mel_var);
  rng_seeds(&c->rng_ins, "%u-ins-%d", c->seed, c->ins_var);
}

static void tempo_for_bpm(COMP *c, double bpm)
{
  double f = 3000.0 / (bpm * c->bt), best = 1e9;
  int a, b;
  for (a = 2; a < 16; a++)
    for (b = a; b >= a - 1; b--)
    {
      double err;
      if (b < 2) continue;
      err = fabs((a + b) / 2.0 - f);
      if (err < best - 1e-9)
      {
        best = err;
        c->tempo[0] = a;
        c->tempo[1] = b;
      }
    }
}

static const char *prog_family(const JV *prog)
{
  int c, hasmin = 0, hasmaj = 0;
  for (c = 0; c < jv_len(prog); c++)
  {
    const char *sym = jv_str(jv_at(prog, c), "i");
    CHORD ch;
    if (!sg_parsechord(sym, &ch)) continue;
    if (ch.q == Q_POW) continue;
    if (ch.root == 0)
    {
      if ((ch.q == Q_MIN) || (ch.q == Q_MIN7) || (ch.q == Q_MIN6) || (ch.q == Q_DIM)) hasmin = 1;
      else hasmaj = 1;
    }
    if ((sym[0] == 'b') && ((ch.root == 3) || (ch.root == 8) || (ch.root == 10))) hasmin = 1;
  }
  if ((!hasmin) && (!hasmaj)) return "neutral";
  if (hasmin && hasmaj) return "mixed";
  return hasmin ? "min" : "maj";
}

static void setprog(COMP *c, int sec, const JV *p)
{
  int k;
  c->nprog[sec] = 0;
  for (k = 0; (k < jv_len(p)) && (k < 16); k++)
  {
    CHORD ch;
    if (!sg_parsechord(jv_str(jv_at(p, k), "i"), &ch)) { ch.root = 0; ch.q = Q_MIN; }
    ch.root = (c->key + ch.root) % 12;
    c->progs[sec][c->nprog[sec]++] = ch;
  }
  if (!c->nprog[sec])
  {
    c->progs[sec][0].root = c->key;
    c->progs[sec][0].q = Q_MIN;
    c->nprog[sec] = 1;
  }
}

// ---------------------------------------------------------------------------
// Instrument choice
// ---------------------------------------------------------------------------

static int strin(const char *s, const char *const *list, int n)
{
  int k;
  for (k = 0; k < n; k++)
    if (!strcmp(s, list[k])) return 1;
  return 0;
}

// gen_alt: with probability p a generated instrument of the same kind and waveform
static void gen_alt(COMP *c, char *name, double p)
{
  SGRNG *ri = &c->rng_ins;
  double r = rng_random(ri), k = rng_random(ri);
  const char *theme = jv_str(S(c, "theme"), NULL);
  const SGINS *base;
  int ng = sg_numgen(c->model), i, n;
  static int cand[4096];
  if (getenv("GG_SGDEBUG")) fprintf(stderr, "gen_alt %s %.2f %.4f %.4f\n", name, p, r, k);
  if ((theme) && (r < p + 0.25) && (ng))
  {
    base = sg_findins(name);
    if (!base) return;
    n = 0;
    for (i = 0; i < ng; i++)
    {
      const SGINS *g = sg_gen(c->model, i);
      if (strcmp(g->theme, theme) || strcmp(g->cat, base->cat) || g->filt[0]) continue;
      cand[n++] = i;
    }
    if ((!strcmp(base->cat, "lead")) || (!strcmp(base->cat, "pad")))
    {
      int m = 0;
      for (i = 0; i < n; i++)
        if (!strcmp(sg_gen(c->model, cand[i])->group, base->group)) cand[m++] = cand[i];
      if (m) n = m;
      else
      {
        // like SIDGen: "or tp" - all of the theme again
        n = 0;
        for (i = 0; i < ng; i++)
        {
          const SGINS *g = sg_gen(c->model, i);
          if (strcmp(g->theme, theme) || strcmp(g->cat, base->cat) || g->filt[0]) continue;
          cand[n++] = i;
        }
      }
    }
    if (n)
    {
      cpyname(name, sg_gen(c->model, cand[(int)(k * n) % n])->name);
      return;
    }
  }
  if ((r >= p) || (!ng)) return;
  base = sg_findins(name);
  if (!base) return;
  {
    const char *grp = ((!strcmp(base->cat, "lead")) || (!strcmp(base->cat, "pad"))) && base->group[0] ? base->group : NULL;
    int wave = sg_mainwave(base), m = 0;
    n = 0;
    for (i = 0; i < ng; i++)
    {
      const SGINS *g = sg_gen(c->model, i);
      if (strcmp(g->cat, base->cat) || g->filt[0] || g->theme[0]) continue;
      if ((grp) && (strcmp(g->group, grp))) continue;
      cand[n++] = i;
    }
    for (i = 0; i < n; i++)
      if (sg_mainwave(sg_gen(c->model, cand[i])) == wave) cand[m++] = cand[i];
    if (m) n = m;
    else
    {
      // "same or out": all of them again
      n = 0;
      for (i = 0; i < ng; i++)
      {
        const SGINS *g = sg_gen(c->model, i);
        if (strcmp(g->cat, base->cat) || g->filt[0] || g->theme[0]) continue;
        if ((grp) && (strcmp(g->group, grp))) continue;
        cand[n++] = i;
      }
    }
    if (!n) return;
    cpyname(name, sg_gen(c->model, cand[(int)(k * n) % n])->name);
  }
}

static double punchscore(COMP *c, const char *n, int *found)
{
  return sg_punch(c->model, sg_formodel(sg_removed_name(n), c->model), found);
}

static void better(COMP *c, SGRNG *pr, char *cur, const JV *list, double margin)
{
  const char *cands[256];
  double sc[256], top;
  int n = 0, k, f, mine_f, m = 0;
  double mine;
  const char *pick[256];
  for (k = 0; (k < jv_len(list)) && (n < 255); k++)
  {
    const char *s = jv_str(jv_at(list, k), NULL);
    int d, dup = 0;
    if (!s) continue;
    for (d = 0; d < n; d++)
      if (!strcmp(cands[d], s)) dup = 1;
    if (dup) continue;
    sc[n] = punchscore(c, s, &f);
    if (!f) continue;
    cands[n++] = s;
  }
  {
    int dup = 0;
    for (k = 0; k < n; k++)
      if (!strcmp(cands[k], cur)) dup = 1;
    if (!dup)
    {
      sc[n] = punchscore(c, cur, &f);
      if (f) cands[n++] = cur;
    }
  }
  mine = punchscore(c, cur, &mine_f);
  if ((!n) || (!mine_f)) return;
  top = -1e9;
  for (k = 0; k < n; k++)
    if (sc[k] > top) top = sc[k];
  if (mine >= top - margin) return;
  // sorted descending like SIDGen: (score, name) pairs, highest first
  {
    int a, z;
    for (a = 0; a < n; a++)
      for (z = a + 1; z < n; z++)
        if ((sc[z] > sc[a]) || ((sc[z] == sc[a]) && (strcmp(cands[z], cands[a]) > 0)))
        {
          double t = sc[a];
          const char *tn = cands[a];
          sc[a] = sc[z]; cands[a] = cands[z];
          sc[z] = t; cands[z] = tn;
        }
  }
  for (k = 0; k < n; k++)
    if (sc[k] >= top - margin) pick[m++] = cands[k];
  if (m)
  {
    char tmp[48];
    cpyname(tmp, pick[rng_below(pr, m)]);
    cpyname(cur, tmp);
  }
}

static void more_punch(COMP *c)
{
  SGRNG pr;
  const JV *kit = S(c, "kit");
  rng_seeds(&pr, "%u-punch-%d", c->seed, c->ins_var);
  if (strcmp(c->filter_role, "bass")) better(c, &pr, c->i_bass, S(c, "bass_inst"), 9.0);
  if (c->kit[SK_KICK][0]) better(c, &pr, c->kit[SK_KICK], jv_get(kit, "kick"), 4.0);
  if (c->kit[SK_SNARE][0]) better(c, &pr, c->kit[SK_SNARE], jv_get(kit, "snare"), 4.0);
  if (c->i_kickbass[0]) better(c, &pr, c->i_kickbass, S(c, "kickbass"), 3.0);
}

static double similarity(const char *a, const char *b) { return tb_similarity_names(a, b); }

static int alternatives(const char *name, const char **out, int max) { return tb_alternatives(name, NULL, 0, out, max); }

// far(): the candidate least similar to all names in 'against'
static int far_pick(COMP *c, SGRNG *vr, const char **cands, int n, const char **against, int na, char *out)
{
  int k, a, best = -1;
  double bs = 1e9, br = 0;
  const char *uniq[512];
  int nu = 0;
  for (k = 0; (k < n) && (nu < 512); k++)
  {
    int dup = 0;
    for (a = 0; a < nu; a++)
      if (!strcmp(uniq[a], cands[k])) dup = 1;
    for (a = 0; a < na; a++)
      if (!strcmp(against[a], cands[k])) dup = 1;
    if (!dup) uniq[nu++] = cands[k];
  }
  for (k = 0; k < nu; k++)
  {
    double m = 0, rr = rng_random(vr);
    for (a = 0; a < na; a++)
    {
      double s = similarity(uniq[k], against[a]);
      if (s > m) m = s;
    }
    if ((m < bs) || ((m == bs) && (rr < br)))
    {
      bs = m;
      br = rr;
      best = k;
    }
  }
  (void)c;
  if (best < 0) return 0;
  cpyname(out, uniq[best]);
  return 1;
}

static void diversify(COMP *c, SGRNG *vr)
{
  const char *cands[512], *ag[8];
  int n, k;
  const JV *li = S(c, "lead_inst"), *ci = S(c, "counter_inst"), *ai = S(c, "arp_inst");
  char tmp[48];
  if ((!strcmp(c->i_lead_v, c->i_lead)) || (!strcmp(c->i_lead_v, c->i_lead_b)))
  {
    n = 0;
    for (k = 0; k < jv_len(li); k++) cands[n++] = jv_str(jv_at(li, k), "");
    n += alternatives(c->i_lead, cands + n, 400 - n);
    ag[0] = c->i_lead;
    ag[1] = c->i_lead_b;
    if (far_pick(c, vr, cands, n, ag, 2, tmp)) cpyname(c->i_lead_v, tmp);
  }
  // second voice: an instrument other than the melody
  n = 0;
  for (k = 0; k < jv_len(ci); k++) cands[n++] = jv_str(jv_at(ci, k), "");
  for (k = 0; k < jv_len(li); k++) cands[n++] = jv_str(jv_at(li, k), "");
  n += alternatives(c->i_lead, cands + n, 400 - n);
  ag[0] = c->i_lead; ag[1] = c->i_lead_b; ag[2] = c->i_lead_v;
  if (far_pick(c, vr, cands, n, ag, 3, tmp)) cpyname(c->i_harm, tmp);
  n = 0;
  cands[n++] = c->i_counter;
  for (k = 0; k < jv_len(ci); k++) cands[n++] = jv_str(jv_at(ci, k), "");
  n += alternatives(c->i_counter, cands + n, 400 - n);
  ag[3] = c->i_harm;
  {
    char cur[48];
    cpyname(cur, c->i_counter);
    if (far_pick(c, vr, cands, n, ag, 4, tmp)) cpyname(c->i_counter, tmp);
    (void)cur;
  }
  n = 0;
  cands[n++] = c->i_broken;
  n += alternatives(c->i_broken, cands + n, 400 - n);
  ag[4] = c->i_counter;
  if (far_pick(c, vr, cands, n, ag, 5, tmp)) cpyname(c->i_broken, tmp);
  // second arpeggio: other waveform than the first
  {
    const SGARP *a0 = sg_findarp(c->t_arp);
    int tw = a0 ? (a0->wavebyte & 0xf0) : 0, best = -1;
    double br = 2;
    int bsame = 2;
    const char *list[64];
    int nl = 0;
    for (k = 0; k < jv_len(ai); k++)
    {
      const char *a = jv_str(jv_at(ai, k), "");
      if (strcmp(a, c->t_arp) && strcmp(a, c->t_pad)) list[nl++] = a;
    }
    if (!nl)
    {
      extern int sg_arpcount(void);
      extern const SGARP *sg_arpat(int k);
      for (k = 0; (k < sg_arpcount()) && (nl < 64); k++)
      {
        const char *a = sg_arpat(k)->name;
        if (strcmp(a, c->t_arp) && strcmp(a, c->t_pad) && strcmp(a, c->t_stab)) list[nl++] = a;
      }
    }
    for (k = 0; k < nl; k++)
    {
      const SGARP *t = sg_findarp(list[k]);
      int same = t ? ((t->wavebyte & 0xf0) == tw) : 1;
      double rr = rng_random(vr);
      if ((same < bsame) || ((same == bsame) && (rr < br)))
      {
        bsame = same;
        br = rr;
        best = k;
      }
    }
    if (best >= 0) cpyname(c->t_arp2, list[best]);
  }
}

static void pick_instruments(COMP *c)
{
  SGRNG *rng = &c->rng, *ri = &c->rng_ins;
  const JV *kit = S(c, "kit"), *li = S(c, "lead_inst"), *ai = S(c, "arp_inst");
  int k;
  cpyname(c->i_bass, jv_choicestr(ri, S(c, "bass_inst")));
  cpyname(c->i_kickbass, jv_choicestr(ri, S(c, "kickbass")));
  cpyname(c->i_lead, jv_choicestr(ri, li));
  if (rng_random(ri) < 0.6) cpyname(c->i_lead_b, c->i_lead);
  else cpyname(c->i_lead_b, jv_choicestr(ri, li));
  if (c->want_lead[0])
  {
    cpyname(c->i_lead, c->want_lead);
    cpyname(c->i_lead_b, c->want_lead);
  }
  cpyname(c->t_arp, jv_choicestr(ri, ai));
  {
    const char *stabs[64];
    int n = 0;
    for (k = 0; k < jv_len(ai); k++)
    {
      const char *a = jv_str(jv_at(ai, k), "");
      if (strstr(a, "Stab") || strstr(a, "Orgel") || strstr(a, "Akkordeon")) stabs[n++] = a;
    }
    if (n) cpyname(c->t_stab, stabs[rng_below(ri, n)]);
    else cpyname(c->t_stab, c->t_arp);
  }
  cpyname(c->t_pad, jv_choicestr(ri, S(c, "pad_inst")));
  cpyname(c->i_counter, jv_choicestr(ri, S(c, "counter_inst")));
  cpyname(c->i_broken, jv_choicestr(ri, S(c, "broken_inst")));
  {
    const char *sb = jv_choicestr(rng, S(c, "stab"));
    snprintf(c->stab, sizeof c->stab, "%s", sb ? sb : "");
  }
  if ((int)strlen(c->stab) != c->spb) snprintf(c->stab, sizeof c->stab, "%s", c->spb == 12 ? "...x.....x.." : "..x...x...x...x.");
  for (k = 0; k < SK_NUM; k++) c->kit[k][0] = 0;
  for (k = 0; k < jv_len(kit); k++)
  {
    // in the order of the style's kit (as SIDGen's dict)
    int cat;
    for (cat = 0; cat < SK_NUM; cat++)
      if (!strcmp(kit->keys[k], sg_kitcat[cat])) break;
    if ((cat < SK_NUM) && (jv_len(jv_at(kit, k)))) cpyname(c->kit[cat], jv_choicestr(ri, jv_at(kit, k)));
    else if (jv_len(jv_at(kit, k))) jv_choicestr(ri, jv_at(kit, k));
  }
  {
    const JV *f = S(c, "filter");
    double w[64];
    int n = jv_len(f), i;
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) w[i] = jv_num(jv_at(jv_at(f, i), 2), 1);
    c->filter_role[0] = c->filter_tpl[0] = 0;
    if (n)
    {
      i = rng_wchoice(rng, w, n);
      snprintf(c->filter_role, sizeof c->filter_role, "%s", jv_str(jv_at(jv_at(f, i), 0), ""));
      snprintf(c->filter_tpl, sizeof c->filter_tpl, "%s", jv_str(jv_at(jv_at(f, i), 1), ""));
    }
  }
  {
    static const int order[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_C, SEC_OUTRO};
    for (k = 0; k < 5; k++)
    {
      const char *bp = jv_wstr(rng, S(c, "bass"));
      snprintf(c->bass_pat[order[k]], 16, "%s", bp ? bp : "root8");
    }
  }
  if (rng_random(rng) < 0.6) memcpy(c->bass_pat[SEC_B], c->bass_pat[SEC_A], 16);
  for (k = SEC_A2; k <= SEC_C2; k++) memcpy(c->bass_pat[k], c->bass_pat[sec_base(k)], 16);
  cpyname(c->i_lead_v, c->i_lead);
  cpyname(c->i_harm, c->i_lead);
  cpyname(c->t_arp2, c->t_arp);
  if ((c->islong) || (c->sids >= 2) || (c->fixed))
  {
    SGRNG vr;
    c->hasvar = 1;
    rng_seeds(&vr, "%u-v-%d", c->seed, c->ins_var);
    for (k = SEC_A2; k <= SEC_C2; k++)
      if (rng_random(&vr) < 0.5) snprintf(c->bass_pat[k], 16, "%s", jv_wstr(&vr, S(c, "bass")));
    cpyname(c->i_lead_v, jv_choicestr(&vr, li));
    if (rng_random(&vr) < 0.6) cpyname(c->i_harm, c->i_lead);
    else cpyname(c->i_harm, jv_choicestr(&vr, S(c, "counter_inst")));
    cpyname(c->t_arp2, jv_choicestr(&vr, ai));
    if (c->sids >= 2) diversify(c, &vr);
  }
  // more colours: now and then a generated sound of this SID model
  gen_alt(c, c->i_bass, 0.3);
  if (!c->want_lead[0])
  {
    gen_alt(c, c->i_lead, 0.2);
    if ((strcmp(c->i_lead_b, c->i_lead)) || (rng_random(ri) < 0.5)) gen_alt(c, c->i_lead_b, 0.2);
    else cpyname(c->i_lead_b, c->i_lead);
  }
  gen_alt(c, c->i_counter, 0.25);
  gen_alt(c, c->i_broken, 0.25);
  {
    // sorted kit categories like SIDGen (clap crash hat kick ohat perc ride snare tom)
    static const int sorted_kit[SK_NUM] = {SK_CLAP, SK_CRASH, SK_HAT, SK_KICK, SK_OHAT, SK_PERC, SK_RIDE, SK_SNARE, SK_TOM};
    int dance_kick = jv_true(S(c, "dance_kick"));
    for (k = 0; k < SK_NUM; k++)
    {
      int cat = sorted_kit[k];
      if (!c->kit[cat][0]) continue;
      if ((cat == SK_KICK) && (dance_kick)) continue;
      gen_alt(c, c->kit[cat], 0.25);
    }
    if ((jv_len(S(c, "drums"))) && ((c->sids == 1) || (dance_kick))) more_punch(c);
  }
  if (c->model == 8580)
  {
    char *f[] = {c->i_bass, c->i_kickbass, c->i_lead, c->i_lead_b, c->i_counter, c->i_broken, c->i_lead_v, c->i_harm};
    for (k = 0; k < 8; k++)
      if (f[k][0]) cpyname(f[k], sg_formodel(f[k], 8580));
    for (k = 0; k < SK_NUM; k++)
      if (c->kit[k][0]) cpyname(c->kit[k], sg_formodel(c->kit[k], 8580));
  }
  if (c->sids >= 4) comp_pick_layer_instruments(c);
  if (c->fixed)
  {
    // subtunes: the instruments of the title music
    COMP *f = c->fixed;
    cpyname(c->i_bass, f->i_bass); cpyname(c->i_kickbass, f->i_kickbass); cpyname(c->i_lead, f->i_lead);
    cpyname(c->i_lead_b, f->i_lead_b); cpyname(c->t_arp, f->t_arp); cpyname(c->t_stab, f->t_stab); cpyname(c->t_pad, f->t_pad);
    cpyname(c->i_counter, f->i_counter); cpyname(c->i_broken, f->i_broken);
    memcpy(c->kit, f->kit, sizeof c->kit);
    if (f->hasvar)
    {
      cpyname(c->i_lead_v, f->i_lead_v); cpyname(c->i_harm, f->i_harm); cpyname(c->t_arp2, f->t_arp2);
    }
    if (f->sids >= 4)
    {
      cpyname(c->i_bass2, f->i_bass2); cpyname(c->i_sub, f->i_sub); cpyname(c->i_lead2, f->i_lead2); cpyname(c->i_lead3, f->i_lead3);
      cpyname(c->i_padv, f->i_padv); cpyname(c->i_pad2v, f->i_pad2v); cpyname(c->i_strings, f->i_strings);
      cpyname(c->i_counter2, f->i_counter2); cpyname(c->i_harm2, f->i_harm2); cpyname(c->i_broken2, f->i_broken2);
      cpyname(c->t_arp3, f->t_arp3); cpyname(c->i_kick2, f->i_kick2); cpyname(c->i_clap, f->i_clap);
      c->layer_bass_shift = f->layer_bass_shift;
    }
  }
  {
    const JV *ed = S(c, "echo_delay");
    c->echo_delay = jv_len(ed) ? (int)jv_num(jv_at(ed, rng_below(rng, jv_len(ed))), 2) : 2;
  }
  {
    const JV *dr = S(c, "drums");
    int n = jv_len(dr);
    for (k = 0; k < 3; k++) c->drum_pat[k] = n ? jv_at(dr, rng_below(rng, n)) : NULL;
    if ((n) && (rng_random(rng) < 0.5)) c->drum_pat[1] = c->drum_pat[0];
  }
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

static int bars(COMP *c)
{
  int k, n = 0;
  for (k = 0; k < c->nsecs; k++) n += c->secbars[k];
  return n;
}

static void secinsert(COMP *c, int pos, int s, int b)
{
  int k;
  if (c->nsecs >= SG_MAXSECS) return;
  for (k = c->nsecs; k > pos; k--)
  {
    c->secs[k] = c->secs[k - 1];
    c->secbars[k] = c->secbars[k - 1];
  }
  c->secs[pos] = s;
  c->secbars[pos] = b;
  c->nsecs++;
}

static void secpop(COMP *c, int pos)
{
  int k;
  for (k = pos; k < c->nsecs - 1; k++)
  {
    c->secs[k] = c->secs[k + 1];
    c->secbars[k] = c->secbars[k + 1];
  }
  c->nsecs--;
}

static void plan_structure(COMP *c)
{
  SGRNG *rng = &c->rng;
  const JV *structs = S(c, "structures"), *tpl, *sb = S(c, "sec_bars");
  int lens[SEC_NUM], k, guard;
  double tb, limit;
  if (c->jingle)
  {
    c->nsecs = 2;
    c->secs[0] = SEC_C; c->secbars[0] = 2;
    c->secs[1] = SEC_OUTRO; c->secbars[1] = 2;
    c->total_bars = 4;
    c->total_rows = 4 * c->spb;
    c->seconds = 4 * c->bar_sec;
    return;
  }
  tpl = jv_at(structs, rng_below(rng, jv_len(structs)));
  for (k = 0; k < SEC_NUM; k++) lens[k] = 8;
  for (k = 0; k < jv_len(sb); k++)
  {
    const JV *v = jv_at(sb, k);
    int s = sec_index(sb->keys[k]);
    lens[s] = (int)jv_num(jv_at(v, rng_below(rng, jv_len(v))), 8);
  }
  for (k = SEC_A2; k <= SEC_C2; k++) lens[k] = lens[sec_base(k)];
  c->nsecs = 0;
  for (k = 0; k < jv_len(tpl); k++)
  {
    int s = sec_index(jv_str(jv_at(tpl, k), "A"));
    secinsert(c, c->nsecs, s, lens[s]);
  }
  tb = c->target / c->bar_sec;
  {
    static const int extra1[] = {SEC_A, SEC_B, SEC_B, SEC_C, SEC_A, SEC_B};
    static const int extra2[] = {SEC_A, SEC_B, SEC_B, SEC_C, SEC_A2, SEC_B2, SEC_B2, SEC_C2, SEC_A, SEC_B, SEC_A2, SEC_B2, SEC_C, SEC_B, SEC_B2};
    const int *extra = c->islong ? extra2 : extra1;
    int ne = c->islong ? 15 : 6;
    k = 0;
    while ((bars(c) + 2 < tb) && (k < 200))
    {
      int t = extra[k % ne], ln, pos = (c->nsecs && c->secs[c->nsecs - 1] == SEC_OUTRO) ? c->nsecs - 1 : c->nsecs;
      if (bars(c) + lens[t] > tb + lens[t] / 2.0) ln = 4;
      else ln = lens[t];
      secinsert(c, pos, t, ln);
      k++;
    }
  }
  guard = 0;
  while ((bars(c) - 2 > tb) && (guard < 60))
  {
    int cand[SG_MAXSECS], nc = 0, longs = -1, i;
    guard++;
    for (i = 0; i < c->nsecs; i++)
      if ((c->secs[i] == SEC_C) || (c->secs[i] == SEC_C2)) cand[nc++] = i;
    if (!nc)
    {
      for (i = 0; i < c->nsecs; i++)
      {
        int t = c->secs[i], cnt = 0, j;
        if ((t != SEC_A) && (t != SEC_B) && (t != SEC_A2) && (t != SEC_B2)) continue;
        for (j = 0; j < c->nsecs; j++)
          if (c->secs[j] == t) cnt++;
        if (cnt > 1) cand[nc++] = i;
      }
    }
    for (i = 0; i < c->nsecs; i++)
      if ((c->secbars[i] > 4) && ((c->secs[i] == SEC_INTRO) || (c->secs[i] == SEC_OUTRO))) { longs = i; break; }
    if (longs >= 0) c->secbars[longs] = 4;
    else if ((nc) && (bars(c) - c->secbars[cand[nc - 1]] >= tb - 2)) secpop(c, cand[nc - 1]);
    else
    {
      int big = -1;
      for (i = 0; i < c->nsecs; i++)
        if ((c->secbars[i] >= 8) && (c->spb == 16)) big = i;
      if (big < 0) break;
      c->secbars[big] = 4;
    }
  }
  limit = 180.0;
  if (c->target + 10 < 600) { if (c->target + 10 > limit) limit = c->target + 10; }
  else limit = 600;
  while ((bars(c) * c->bar_sec > limit) && (c->nsecs > 2))
  {
    // the operation that brings the piece closest under the limit
    int i, bestop = -1, besti = -1, fitany = 0;
    double bestv = 0;
    for (i = 0; i < c->nsecs; i++)
    {
      int b = c->secbars[i], t = c->secs[i], ops, op;
      for (ops = 0; ops < 2; ops++)
      {
        double v;
        if (ops == 0)
        {
          if (!((b >= 8) && (c->spb == 16))) continue;
          v = bars(c) - b / 2;
          op = 0;
        }
        else
        {
          if (!(((t != SEC_INTRO) && (t != SEC_OUTRO)) || (c->nsecs <= 3))) continue;
          v = bars(c) - b;
          op = 1;
        }
        {
          int fits = v * c->bar_sec <= limit;
          if ((fits && !fitany) || (fits && v > bestv) || (!fitany && !fits && (bestop < 0 || v < bestv)))
          {
            if (fits) fitany = 1;
            bestv = v;
            bestop = op;
            besti = i;
          }
        }
      }
    }
    if (besti < 0) break;
    if (bestop == 0) c->secbars[besti] /= 2;
    else secpop(c, besti);
  }
  while (bars(c) * c->bar_sec < 60)
  {
    int pos = (c->nsecs && c->secs[c->nsecs - 1] == SEC_OUTRO) ? c->nsecs - 1 : c->nsecs;
    if (c->nsecs >= SG_MAXSECS) break;
    secinsert(c, pos, SEC_B, c->spb == 16 ? 4 : lens[SEC_B]);
  }
  c->total_bars = bars(c);
  c->total_rows = c->total_bars * c->spb;
  c->seconds = c->total_bars * c->bar_sec;
}

// chords for nbars bars of a section
static void chords_for(COMP *c, int sec, int nbars, int last, CHORD *out)
{
  int cb = c->chord_bars[sec], n = c->nprog[sec], b;
  if (cb < 1) cb = 1;
  if ((n * cb > nbars) && (n > nbars)) cb = 1;
  for (b = 0; b < nbars; b++) out[b] = c->progs[sec][(b / cb) % n];
  if ((last) && (nbars)) out[nbars - 1] = c->tonic;
}

// ---------------------------------------------------------------------------
// Melody
// ---------------------------------------------------------------------------

int comp_chord_pcs(CHORD ch)
{
  int m = 0, k;
  for (k = 0; k < sg_qlen[ch.q]; k++) m |= 1 << ((ch.root + sg_qiv[ch.q][k]) % 12);
  return m;
}

int comp_chord_scale(COMP *c, CHORD ch)
{
  int cps = comp_chord_pcs(ch), out = cps, pc;
  for (pc = 0; pc < 12; pc++)
  {
    int k, rub = 0;
    if (!((c->scale.pcs >> pc) & 1)) continue;
    if ((cps >> pc) & 1) continue;
    for (k = 0; k < 12; k++)
      if (((cps >> k) & 1) && ((((pc - k) % 12 + 12) % 12 == 1) || (((pc - k) % 12 + 12) % 12 == 11))) rub = 1;
    if (!rub) out |= 1 << pc;
  }
  return out;
}

static const char *CELLS16[] = {"x---", "x-x-", "xxxx", "x-xx", "xx-x", "x--x", "--x-", "..x-", "x-..", "----", "....", "x..."};
static const double W16[] = {3.0, 4.0, 1.2, 1.6, 0.6, 1.2, 0.8, 1.0, 1.4, 1.6, 0.8, 1.0};
static const char *CELLS12[] = {"x--", "x-x", "xxx", "x.x", "..x", "x..", "---", "..."};
static const double W12[] = {3.0, 4.0, 1.0, 1.2, 0.8, 1.2, 1.6, 0.8};

static int lead_rhythm(COMP *c, double *dur, double *w)
{
  const JV *r = jv_get(c->L, "rhythm");
  int k, n = 0;
  for (k = 0; (k < jv_len(r)) && (n < 16); k++)
  {
    dur[n] = atof(r->keys[k]);
    w[n] = jv_num(jv_at(r, k), 1);
    n++;
  }
  return n;
}

static int cell_weights(COMP *c, double dense, int sparse, double *out)
{
  double dur[16], w[16], tot = 0, avg = 0, target;
  int n = lead_rhythm(c, dur, w), k, nc = c->bt == 4 ? 12 : 8;
  for (k = 0; k < n; k++) { tot += w[k]; avg += dur[k] * w[k]; }
  avg = tot > 0 ? avg / tot : 2;
  target = c->bt / avg;
  if (target < 0.6) target = 0.6;
  if (target > 3.5) target = 3.5;
  if (dense) target *= 1.0 + dense;
  if (sparse) target *= 0.55;
  for (k = 0; k < nc; k++)
  {
    const char *cell = c->bt == 4 ? CELLS16[k] : CELLS12[k];
    int x = 0, i;
    for (i = 0; cell[i]; i++)
      if (cell[i] == 'x') x++;
    out[k] = (c->bt == 4 ? W16[k] : W12[k]) * pow(2.7, -fabs(x - target));
    if (!x) out[k] *= 0.7;
  }
  return nc;
}

static int make_rhythm(COMP *c, int nb, double dense, int sparse, int cadence, int rest_ok, RCELL *out)
{
  SGRNG *rng = c->cur;
  double wts[12];
  int nc = cell_weights(c, dense, sparse, wts), b, n = 0, i, len;
  double rest_p = c->L_rest;
  char pattern[600];
  int bt = c->bt;
  const char *prev = NULL;
  len = 0;
  for (b = 0; b < nb * 4; b++)
  {
    int first = b % 4 == 0;
    const char *cell;
    if ((cadence) && (b == nb * 4 - 2))
    {
      pattern[len++] = 'x';
      for (i = 1; i < bt; i++) pattern[len++] = '-';
      prev = "x";
      continue;
    }
    if ((cadence) && (b == nb * 4 - 1))
    {
      char ch = rng_random(rng) < 0.6 ? '-' : '.';
      for (i = 0; i < bt; i++) pattern[len++] = ch;
      prev = ch == '-' ? "-" : ".";
      continue;
    }
    for (;;)
    {
      int k = rng_wchoice(rng, wts, nc), x = 0;
      cell = bt == 4 ? CELLS16[k] : CELLS12[k];
      for (i = 0; cell[i]; i++)
        if (cell[i] == 'x') x++;
      if ((first) && (cell[0] != 'x') && ((b == 0) || (rng_random(rng) < 0.85))) continue;
      if ((cell[0] == '-') && ((len == 0) || (pattern[len - 1] == '.'))) continue;
      if ((!x) && (!rest_ok)) continue;
      {
        int allrest = 1;
        for (i = 0; cell[i]; i++)
          if (cell[i] != '.') allrest = 0;
        if ((allrest) && (rng_random(rng) > rest_p * 2.5)) continue;
      }
      break;
    }
    for (i = 0; cell[i]; i++) pattern[len++] = cell[i];
    prev = cell;
  }
  (void)prev;
  i = 0;
  while (i < len)
  {
    int j = i + 1;
    if (pattern[i] == 'x')
    {
      while ((j < len) && (pattern[j] == '-')) j++;
      out[n].pos = i; out[n].d = j - i; out[n].isnote = 1;
    }
    else
    {
      while ((j < len) && (pattern[j] != 'x')) j++;
      out[n].pos = i; out[n].d = j - i; out[n].isnote = 0;
    }
    n++;
    i = j;
  }
  return n;
}

static void make_motif(COMP *c, const RCELL *rh, int nr, int up, MOTIF *m)
{
  SGRNG *rng = c->cur;
  int notes[200], nn = 0, i, last_leap = 0;
  for (i = 0; i < nr; i++)
    if (rh[i].isnote) notes[nn++] = i;
  m->n = 0;
  for (i = 0; (i < nn) && (m->n < 96); i++)
  {
    int pos = rh[notes[i]].pos, d = rh[notes[i]].d, k;
    int strong = (pos % (2 * c->bt) == 0) || (d >= c->bt);
    int half = i < nn / 2.0;
    int bias = (half ? 1 : -1) * (up ? 1 : -1);
    static const int keys[5] = {-2, -1, 0, 1, 2};
    double w[5];
    if (strong)
    {
      w[0] = 0.3 + c->L_leap; w[1] = 3.0; w[2] = 1.6 + c->L_repeat * 6; w[3] = 3.0; w[4] = 0.3 + c->L_leap;
    }
    else
    {
      w[0] = 0.5; w[1] = 4.0; w[2] = 0.3 + c->L_repeat * 4; w[3] = 4.0; w[4] = 0.5;
    }
    for (k = 0; k < 5; k++)
    {
      if (keys[k] * bias > 0) w[k] *= 1.6;
      if (last_leap) w[k] *= (keys[k] * last_leap < 0) ? 2.5 : 0.5;
    }
    // SIDGen's dict order: strong {-2,-1,0,1,2}, weak {-1,1,0,-2,2}
    {
      int dlt;
      if (strong) dlt = keys[rng_wchoice(rng, w, 5)];
      else
      {
        double w2[5] = {w[1], w[3], w[2], w[0], w[4]};
        static const int k2[5] = {-1, 1, 0, -2, 2};
        dlt = k2[rng_wchoice(rng, w2, 5)];
      }
      last_leap = abs(dlt) >= 2 ? dlt : 0;
      m->v[m->n].pos = pos;
      m->v[m->n].d = d;
      m->v[m->n].strong = strong;
      m->v[m->n].dlt = dlt;
      m->n++;
    }
  }
}

static CHORD chord_at(COMP *c, int row)
{
  if (row >= c->nchordrows) row = c->nchordrows - 1;
  if (row < 0) row = 0;
  return c->chord_rows[row];
}

// motif on the chords from row0 (each note fits the chord of its row)
static int realize(COMP *c, const MOTIF *m, int row0, int *prev, int lo, int hi, int cadence, RND *out)
{
  int i, n = 0;
  for (i = 0; i < m->n; i++)
  {
    const MNOTE *x = &m->v[i];
    CHORD ch = chord_at(c, row0 + x->pos);
    int mask = x->strong ? comp_chord_pcs(ch) : comp_chord_scale(c, ch);
    int pool[64], np = 0, k, j = 0, ref, note, dlt = x->dlt;
    for (k = lo - 2; (k < hi + 3) && (np < 64); k++)
      if ((mask >> (((k % 12) + 12) % 12)) & 1) pool[np++] = k;
    if (!np) continue;
    ref = *prev >= 0 ? *prev : (lo + hi) / 2;
    for (k = 1; k < np; k++)
      if (abs(pool[k] - ref) < abs(pool[j] - ref)) j = k;
    if ((cadence) && (i == m->n - 1))
    {
      int best = -1;
      for (k = 0; k < np; k++)
        if ((pool[k] % 12 == ch.root) && ((best < 0) || (abs(pool[k] - ref) < abs(best - ref)))) best = pool[k];
      if (best < 0)
      {
        best = pool[0];
        for (k = 0; k < np; k++)
          if (abs(pool[k] - ref) < abs(best - ref)) best = pool[k];
      }
      note = best;
    }
    else
    {
      if ((*prev >= 0) && (pool[j] != *prev) && (dlt))
      {
        if ((pool[j] - *prev) * dlt > 0) dlt -= dlt > 0 ? 1 : -1;
      }
      k = j + dlt;
      if ((k < 0) || (k >= np) || (pool[k] < lo) || (pool[k] > hi)) k = j - dlt;
      if (k < 0) k = 0;
      if (k > np - 1) k = np - 1;
      note = pool[k];
    }
    out[n].row = x->pos;
    out[n].note = note;
    out[n].dur = x->d;
    n++;
    *prev = note;
  }
  return n;
}

static void fix_lead(COMP *c, RND *notes, int n, int row0)
{
  int i;
  for (i = 0; i < n; i++)
  {
    int r = row0 + notes[i].row, k, d = notes[i].dur;
    CHORD c0 = chord_at(c, r);
    for (k = 1; k < d; k++)
    {
      int rr = r + k;
      CHORD ch;
      if (rr >= c->nchordrows) { d = k; break; }
      ch = c->chord_rows[rr];
      if ((ch.root != c0.root) || (ch.q != c0.q))
      {
        if (!((comp_chord_scale(c, ch) >> (((notes[i].note % 12) + 12) % 12)) & 1)) d = k;
        break;
      }
    }
    notes[i].dur = d;
  }
}

// phrase plan: 0 A, 1 A~, 2 B, 3 B!, 4 R
static int phrase_plan(COMP *c, int slots, int *plan)
{
  SGRNG *rng = c->cur;
  int i;
  if (slots <= 1) { plan[0] = 3; return 1; }
  if (slots == 2) { plan[0] = 0; plan[1] = 3; return 2; }
  if (slots == 3) { plan[0] = 0; plan[1] = 1; plan[2] = 3; return 3; }
  if (slots == 4)
  {
    static const int o[3][4] = {{0, 1, 0, 3}, {0, 2, 0, 3}, {0, 1, 2, 3}};
    int k = rng_below(rng, 3);
    for (i = 0; i < 4; i++) plan[i] = o[k][i];
    return 4;
  }
  if (slots == 6)
  {
    static const int cr[6] = {0, 4, 1, 4, 3, 4}, nr[6] = {0, 1, 2, 0, 1, 3};
    for (i = 0; i < 6; i++) plan[i] = c->L_callresp ? cr[i] : nr[i];
    return 6;
  }
  for (i = 0; (i < slots) && (i < 64); i++) plan[i] = i % 3 == 0 ? 0 : i % 3 == 1 ? 1 : 2;
  if (slots > 64) slots = 64;
  plan[slots - 1] = 3;
  return slots;
}

static int cmprnd(const void *a, const void *b)
{
  const RND *x = (const RND *)a, *y = (const RND *)b;
  if (x->row != y->row) return x->row - y->row;
  if (x->note != y->note) return x->note - y->note;
  return x->dur - y->dur;
}

// melody of one section (returns the number of notes in out; positions relative)
int comp_lead_section(COMP *c, int sec, int nbars, int row0, RND *out, int max)
{
  SGRNG *saved = c->cur;
  int lo = c->L_lo, hi = c->L_hi, mb = c->L_motif_bars, plan[64], np, i, n = 0, prev;
  double dense = 0;
  int sparse = 0;
  MOTIF mav;
  MOTIF *ma, *mbm, *mc;
  c->cur = &c->rng_mel;
  if (sec_base(sec) == SEC_B) { lo += 3; hi += 3; dense = 0.35; }
  else if (sec_base(sec) == SEC_C) sparse = 1;
  if (mb < 1) mb = 1;
  if (nbars % mb) mb = 1;
  if (mb > 8) mb = 8;
  if (!c->motif_have[sec][mb])
  {
    RCELL ra[400], rb[400], rc[400];
    int na = make_rhythm(c, mb, dense, sparse, 0, 1, ra);
    int nb2 = make_rhythm(c, mb, dense, sparse, 0, 1, rb);
    int nc = make_rhythm(c, mb, dense, sparse, 1, 1, rc);
    int up = rng_random(c->cur) < 0.6;
    make_motif(c, ra, na, up, &c->motifs[sec][mb][0]);
    make_motif(c, rb, nb2, !up, &c->motifs[sec][mb][1]);
    make_motif(c, rc, nc, !up, &c->motifs[sec][mb][2]);
    c->motif_have[sec][mb] = 1;
  }
  ma = &c->motifs[sec][mb][0];
  mbm = &c->motifs[sec][mb][1];
  mc = &c->motifs[sec][mb][2];
  mav = *ma;
  for (i = (mav.n - 2 > 0 ? mav.n - 2 : 0); i < mav.n; i++)
    mav.v[i].dlt = mav.v[i].dlt ? -mav.v[i].dlt : 1;
  np = phrase_plan(c, nbars / mb, plan);
  prev = c->last_lead_note;
  for (i = 0; i < np; i++)
  {
    int off = i * mb * c->spb, k, nn;
    RND tmp[128];
    const MOTIF *m;
    if (plan[i] == 4) continue;
    m = plan[i] == 0 ? ma : plan[i] == 1 ? &mav : plan[i] == 2 ? mbm : mc;
    nn = realize(c, m, row0 + off, &prev, lo, hi, plan[i] == 3, tmp);
    fix_lead(c, tmp, nn, row0 + off);
    for (k = 0; (k < nn) && (n < max); k++)
    {
      out[n] = tmp[k];
      out[n].row += off;
      prev = tmp[k].note;
      n++;
    }
  }
  // pick-up notes into the phrases
  {
    double pk = c->L_pickup;
    if ((pk > 0) && (n) && ((c->bt == 3) || (c->bt == 4)))
    {
      int e8 = c->bt == 4 ? 2 : 1, s;
      for (s = 0; s < np; s++)
      {
        int st0 = s * mb * c->spb, k, n0 = -1, kk, posv[2], sc[16], nsc = 0, m, ok = 1, notes[2];
        CHORD ch;
        if (plan[s] == 4) continue;
        for (k = 0; k < n; k++)
          if (out[k].row == st0) { n0 = out[k].note; break; }
        if ((n0 < 0) || (st0 < 2 * e8) || (rng_random(c->cur) >= pk)) continue;
        kk = rng_random(c->cur) < 0.45 ? 2 : 1;
        for (k = 0; k < kk; k++) posv[k] = st0 - e8 * (kk - k);
        for (k = 0; k < n; k++)
          if ((out[k].row < st0) && (st0 - 2 * e8 - 1 <= out[k].row + out[k].dur - 1)) ok = 0;
        if (!ok) continue;
        ch = chord_at(c, row0 + posv[0]);
        {
          int mask = comp_chord_scale(c, ch);
          for (m = n0 - 7; m < n0; m++)
            if ((mask >> (((m % 12) + 12) % 12)) & 1) sc[nsc++] = m;
          if (nsc < kk) continue;
          if (rng_random(c->cur) < 0.7)
          {
            for (k = 0; k < kk; k++) notes[k] = sc[nsc - kk + k];
          }
          else
          {
            int up[8], nu = 0;
            for (m = n0 + 1; m < n0 + 6; m++)
              if ((mask >> (((m % 12) + 12) % 12)) & 1) up[nu++] = m;
            if (nu < kk) continue;
            // [:k][::-1]
            for (k = 0; k < kk; k++) notes[k] = up[kk - 1 - k];
          }
        }
        for (k = 0; k < n; k++)
          if ((out[k].row == posv[0]) || ((kk > 1) && (out[k].row == posv[1]))) ok = 0;
        if (!ok) continue;
        for (k = 0; (k < kk) && (n < max); k++)
        {
          out[n].row = posv[k];
          out[n].note = notes[k];
          out[n].dur = e8;
          n++;
        }
      }
      qsort(out, n, sizeof(RND), cmprnd);
    }
  }
  // folk ornaments: a grace note from the upper neighbour
  if ((c->L_ornament > 0) && (n))
  {
    RND res[400];
    int nr = 0, k;
    for (k = 0; (k < n) && (nr < 398); k++)
    {
      if ((out[k].dur >= c->bt) && (rng_random(c->cur) < c->L_ornament))
      {
        int m, upn = -1;
        for (m = out[k].note + 1; m < out[k].note + 4; m++)
          if (scale_contains(&c->scale, m)) { upn = m; break; }
        if (upn >= 0)
        {
          res[nr].row = out[k].row; res[nr].note = upn; res[nr].dur = 1; nr++;
          res[nr].row = out[k].row + 1; res[nr].note = out[k].note; res[nr].dur = out[k].dur - 1; nr++;
          continue;
        }
      }
      res[nr++] = out[k];
    }
    if (nr > max) nr = max;
    memcpy(out, res, sizeof(RND) * nr);
    n = nr;
  }
  if (n) c->last_lead_note = out[n - 1].note;
  c->cur = saved;
  return n;
}

// ---------------------------------------------------------------------------
// Bass figures
// ---------------------------------------------------------------------------

int comp_bass_root(int pc)
{
  int n = 24 + pc;
  if (n < 28) n += 12;
  return n;
}

static int isstraight(const char *key)
{
  static const char *s[] = {"techno", "trance", "deeptrance", "deeptechno", "acid", "ambient", "house", "kraftwerk"};
  return strin(key, s, 8);
}

#define BADD(s_, n_, d_) do { if (nn < 64) { out[nn].row = (s_); out[nn].note = (n_); out[nn].dur = (d_); nn++; } } while (0)

void comp_bass_bar(COMP *c, const char *pat, CHORD chord, CHORD nxt, int bar_idx, const RIFFSTEP *riff, RND *out, int *nout)
{
  SGRNG *rng = c->cur;
  int bt = c->bt, spb = c->spb, nn = 0, b, s, i;
  int r = comp_bass_root(chord.root);
  int third = r + ((sg_qlen[chord.q] > 1 && sg_qiv[chord.q][1] < 7) ? sg_qiv[chord.q][1] : 4);
  int fifth = r + 7;
  int nr = comp_bass_root(nxt.root);
  int e8 = 2;
  static const char *BASS16[] = {"motown", "disco", "surf", "eighties", "gallop", "ostinato", "tresillo", "rnb", "sequencer",
                                 "electro", "robot"};
  if (!strcmp(pat, "root8"))
  {
    for (b = 0; b < 4; b++)
    {
      BADD(b * bt, r, e8);
      BADD(b * bt + e8, r + ((b == 3 && bar_idx % 2) ? 12 : 0), bt - e8);
    }
  }
  else if (!strcmp(pat, "root16"))
  {
    for (s = 0; s < spb; s++) BADD(s, r + (s % 8 == 6 ? 12 : 0), 1);
  }
  else if (!strcmp(pat, "offbeat"))
  {
    for (b = 0; b < 4; b++) BADD(b * bt + e8, r, bt - e8);
  }
  else if (!strcmp(pat, "rolling"))
  {
    for (s = 0; s < spb; s++)
      if (s % bt) BADD(s, r + ((s % bt == 3 && s > 8) ? 12 : 0), 1);
  }
  else if (!strcmp(pat, "octave"))
  {
    for (b = 0; b < 4; b++) { BADD(b * bt, r, e8); BADD(b * bt + e8, r + 12, bt - e8); }
  }
  else if (!strcmp(pat, "rock"))
  {
    for (b = 0; b < 4; b++)
    {
      int n = (b == 3 && bar_idx % 2) ? fifth : r;
      BADD(b * bt, n, e8);
      BADD(b * bt + e8, n, bt - e8);
    }
  }
  else if (!strcmp(pat, "pop"))
  {
    BADD(0, r, bt == 4 ? 3 : 2);
    BADD(bt == 4 ? bt - 1 : 2, r, bt + 1);
    BADD(2 * bt, fifth, bt + e8);
    BADD(3 * bt + e8, r, bt - e8);
  }
  else if (!strcmp(pat, "syncop"))
  {
    if (spb == 12)
    {
      BADD(0, r, 2); BADD(2, r + 12, 3); BADD(5, fifth, 3); BADD(8, r, 2); BADD(9, r + 10, 3);
    }
    else
    {
      BADD(0, r, 3); BADD(3, r + 12, 3); BADD(6, fifth, 4); BADD(10, r, 2); BADD(12, r + 10, 2); BADD(14, r + 12, 2);
    }
  }
  else if (!strcmp(pat, "walking"))
  {
    int t1[3] = {third, fifth, r + 12}, t2[3] = {fifth, third, r + 9};
    int tones[4], appr;
    tones[0] = r;
    tones[1] = t1[rng_below(rng, 3)];
    tones[2] = t2[rng_below(rng, 3)];
    {
      int a3 = scale_contains(&c->scale, nr - 2) ? nr - 2 : nr - 1;
      int opts[3] = {nr - 1, nr + 1, a3};
      appr = opts[rng_below(rng, 3)];
    }
    while (appr - tones[2] > 9) appr -= 12;
    while (tones[2] - appr > 9) appr += 12;
    tones[3] = appr;
    for (b = 0; b < 4; b++) BADD(b * bt, tones[b], bt);
  }
  else if (!strcmp(pat, "boogie"))
  {
    int seq[8] = {r, third, fifth, r + 9, r + 10, r + 9, fifth, third};
    for (i = 0; i < 8; i++)
    {
      b = i / 2;
      BADD(b * bt + (i % 2 ? e8 : 0), seq[i], i % 2 == 0 ? e8 : bt - e8);
    }
  }
  else if (!strcmp(pat, "reggae"))
  {
    BADD(e8, r, bt); BADD(bt + e8, r, e8); BADD(2 * bt, fifth, bt); BADD(3 * bt, r + 12, e8);
    BADD(3 * bt + e8, third < r + 7 ? third + 12 : fifth, bt - e8);
  }
  else if (!strcmp(pat, "sustain"))
  {
    if (rng_random(rng) < 0.5) BADD(0, r, spb);
    else
    {
      BADD(0, r, 2 * bt);
      BADD(2 * bt, rng_random(rng) < 0.5 ? fifth : r, 2 * bt);
    }
  }
  else if (!strcmp(pat, "acid"))
  {
    for (s = 0; s < spb; s++)
      if (riff[s].on) BADD(s, r + riff[s].offs, riff[s].acc ? 2 : 1);
  }
  else if (!strcmp(pat, "dnb"))
  {
    if (spb == 16) { BADD(0, r, 6); BADD(10, bar_idx % 2 == 0 ? r : fifth, 6); }
    else { BADD(0, r, 6); BADD(8, fifth, 4); }
  }
  else if (!strcmp(pat, "halftime"))
  {
    if (spb == 16) { BADD(0, r, 2 * bt + e8); BADD(2 * bt + e8 + 1, r + (bar_idx % 2 ? 12 : 0), bt + 1); }
    else BADD(0, r, spb);
  }
  else if (!strcmp(pat, "drone")) BADD(0, r, spb);
  else if (!strcmp(pat, "march"))
  {
    BADD(0, r, 2 * bt);
    BADD(2 * bt, bar_idx % 2 == 0 ? r : (fifth - 12 >= 24 ? fifth - 12 : r), 2 * bt);
  }
  else if (!strcmp(pat, "oompah"))
  {
    int low5 = fifth - 12 >= 24 ? fifth - 12 : fifth;
    BADD(0, r, bt);
    BADD(2 * bt, low5, bt);
  }
  else if (!strcmp(pat, "arpbass"))
  {
    int seq[4] = {r, r + 12, fifth, r + 12};
    for (s = 0; s < spb; s++) BADD(s, seq[s % 4], 1);
  }
  else if (!strcmp(pat, "alberti"))
  {
    int seq[4] = {r, fifth, third, fifth};
    for (i = 0; i < 8; i++) BADD((i / 2) * bt + (i % 2 ? e8 : 0), seq[i % 4], e8);
  }
  else if ((spb != 16) && (strin(pat, BASS16, 11)))
  {
    comp_bass_bar(c, (!strcmp(pat, "motown") || !strcmp(pat, "surf")) ? "walking" : "root8", chord, nxt, bar_idx, riff, out, nout);
    return;
  }
  else if (!strcmp(pat, "motown"))
  {
    int appr = nr - 1 > r ? nr - 1 : nr + 1;
    BADD(0, r, 3); BADD(3, r, 1); BADD(4, fifth, 2); BADD(6, r + 9, 2); BADD(8, r + 12, 3); BADD(11, fifth, 1);
    BADD(12, third, 2); BADD(14, appr, 2);
  }
  else if (!strcmp(pat, "disco"))
  {
    for (b = 0; b < 4; b++) { BADD(b * bt, r, 2); BADD(b * bt + 2, r + 12, 1); BADD(b * bt + 3, r + 12, 1); }
  }
  else if (!strcmp(pat, "surf"))
  {
    int appr = abs(nr - 1 - r) < 12 ? nr - 1 : nr + 1;
    for (b = 0; b < 3; b++) { BADD(b * bt, r, 2); BADD(b * bt + 2, r, 2); }
    BADD(12, fifth, 2);
    BADD(14, appr, 2);
  }
  else if (!strcmp(pat, "eighties"))
  {
    for (s = 0; s < spb; s++) BADD(s, r + (s % 4 == 2 ? 12 : 0), 1);
  }
  else if (!strcmp(pat, "gallop"))
  {
    for (b = 0; b < 4; b++) { BADD(b * bt, r, 2); BADD(b * bt + 2, r, 1); BADD(b * bt + 3, r, 1); }
  }
  else if (!strcmp(pat, "ostinato"))
  {
    for (b = 0; b < 4; b++)
    {
      int top = b == 3 ? r + 12 : (b % 2 ? fifth : r);
      BADD(b * bt, r, 2); BADD(b * bt + 2, top, 1); BADD(b * bt + 3, r, 1);
    }
  }
  else if (!strcmp(pat, "pedal"))
  {
    for (b = 0; b < 4; b++) BADD(b * bt, r, bt - 1);
  }
  else if (!strcmp(pat, "tresillo"))
  {
    BADD(0, r, 3); BADD(3, r, 3); BADD(6, fifth, 2); BADD(8, r, 3); BADD(11, r + 12, 3); BADD(14, fifth, 2);
  }
  else if (!strcmp(pat, "rnb"))
  {
    BADD(0, r, 3); BADD(6, r, 2); BADD(10, r + 12, 1); BADD(11, r, 3); BADD(14, fifth, 2);
  }
  else if (!strcmp(pat, "electro"))
  {
    BADD(0, r, 2); BADD(3, r, 1); BADD(4, r + 12, 2); BADD(7, r, 1); BADD(8, r, 2); BADD(10, fifth, 2); BADD(12, r + 12, 2);
    BADD(14, bar_idx % 2 ? r + 10 : fifth, 2);
  }
  else if (!strcmp(pat, "robot"))
  {
    int seq[8] = {r, r, r + 12, r, fifth, r, r + 12, r};
    for (i = 0; i < 8; i++) BADD(i * 2, seq[i], 2);
  }
  else if (!strcmp(pat, "sequencer"))
  {
    int seq[8] = {r, r, r + 12, r, r, r + 1, r + 12, r};
    for (s = 0; s < spb; s++) BADD(s, seq[s % 8], 1);
  }
  else BADD(0, r, spb);
  // only steps inside the bar
  {
    int k, m = 0;
    for (k = 0; k < nn; k++)
      if ((out[k].row >= 0) && (out[k].row < spb)) out[m++] = out[k];
    nn = m;
  }
  {
    static const char *APPROACH[] = {"root8", "rock", "pop", "octave", "march", "pedal", "syncop", "oompah", "reggae", "halftime"};
    if ((strin(pat, APPROACH, 10)) && ((nxt.root != chord.root) || (nxt.q != chord.q)) && (nn) && (!isstraight(c->stylekey)) &&
        (rng_random(rng) < jv_num(S(c, "approach"), 0.4)))
    {
      int k = 0, j;
      for (j = 1; j < nn; j++)
        if (out[j].row > out[k].row) k = j;
      if (out[k].row >= spb - bt)
      {
        int n = out[k].note, tgt = abs(nr + 12 - n) < abs(nr - n) ? nr + 12 : nr, cand[4], nc = 0, best;
        int opts[4] = {tgt - 1, tgt + 1, tgt - 2, tgt + 2};
        for (j = 0; j < 4; j++)
          if (scale_contains(&c->scale, opts[j])) cand[nc++] = opts[j];
        if (!nc) cand[nc++] = tgt - 1;
        best = cand[0];
        for (j = 1; j < nc; j++)
          if ((abs(cand[j] - n) < abs(best - n)) || ((abs(cand[j] - n) == abs(best - n)) && (cand[j] < best))) best = cand[j];
        out[k].note = best;
      }
    }
  }
  *nout = nn;
}

void comp_acid_riff(COMP *c, RIFFSTEP *riff)
{
  SGRNG *rng = c->cur;
  static const int offs[10] = {0, 0, 0, 12, 12, 7, 10, 3, 5, -2};
  int s;
  for (s = 0; s < c->spb; s++)
  {
    int on = rng_random(rng) < (s % 4 == 0 ? 0.85 : 0.55);
    int o = s ? offs[rng_below(rng, 10)] : 0;
    riff[s].on = on || s == 0;
    riff[s].offs = o;
    riff[s].acc = rng_random(rng) < 0.25;
  }
}

// ---------------------------------------------------------------------------
// Drums
// ---------------------------------------------------------------------------

static const char *S16_FILL[] = {"............ssss", "............s.ss", "........t.t.t.t.", "............tttt", "..........s.s.ss",
  "........s..s..s.", "..........ttt.ss", "............t.tt", "......s.s.s.ssss", "........tt..ttss", "..........s.sst."};
static const char *S12_FILL[] = {".........sss", "......t.t.tt", ".........s.s", "......s.ss.s", ".........tts"};

// mode: 0 full, 1 hats, 2 kick, 3 none
void comp_drum_bar(COMP *c, int sec, int mode, int bar, int nbars, int first_of_sec, int *steps)
{
  int b = sec_base(sec), spb = c->spb, i, k;
  const JV *pat = c->drum_pat[(b == SEC_A || b == SEC_B || b == SEC_C) ? b - SEC_A : 0];
  int allowed = mode == 0 ? 0x1ff : mode == 1 ? (DL_H | DL_O | DL_R) : mode == 2 ? (DL_K | DL_H) : 0;
  for (i = 0; i < spb; i++) steps[i] = 0;
  for (k = 0; k < jv_len(pat); k++)
  {
    int bit = dl_bit(pat->keys[k][0]);
    const char *s = jv_str(jv_at(pat, k), "");
    if (!(bit & allowed)) continue;
    for (i = 0; (s[i]) && (i < spb); i++)
      if (s[i] == 'x') steps[i] |= bit;
  }
  if ((mode == 0) && (jv_true(S(c, "fills"))) && (bar == nbars - 1) && (nbars >= 4))
  {
    const char *fill = spb == 16 ? S16_FILL[rng_below(c->cur, 11)] : S12_FILL[rng_below(c->cur, 5)];
    int start = 0;
    while ((fill[start]) && (fill[start] == '.')) start++;
    for (i = start; i < spb; i++)
    {
      int keep = steps[i] & DL_K;
      steps[i] = keep | (fill[i] != '.' ? dl_bit(fill[i]) : 0);
    }
  }
  else if ((mode == 0) && (spb == 16) && (bar % 4 == 3) && (rng_random(c->cur) < jv_num(S(c, "drum_var"), 0.65)))
  {
    int hats = 0, kinds[4], nk = 0;
    for (i = 0; i < spb; i++)
      if (steps[i] & (DL_H | DL_O)) hats = 1;
    if (hats) kinds[nk++] = 0;
    if (!isstraight(c->stylekey)) { kinds[nk++] = 1; kinds[nk++] = 2; kinds[nk++] = 3; }
    if (nk)
    {
      int kk = kinds[rng_below(c->cur, nk)];
      if (kk == 0) steps[spb - 2] = (steps[spb - 2] & ~DL_H) | DL_O;
      else if (kk == 1) steps[spb - 2] = (steps[spb - 2] & ~DL_H) | DL_K;
      else if (kk == 2) steps[spb - 1] = (steps[spb - 1] & ~DL_H) | DL_S;
      else
      {
        steps[spb - 3] |= DL_S;
        steps[spb - 1] = (steps[spb - 1] & ~DL_H) | DL_S;
      }
    }
  }
  if ((mode == 0) && (first_of_sec) && (bar == 0) && (b == SEC_B)) steps[0] |= DL_X;
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

static int drummode(const char *s)
{
  if (!s) return 3;
  if (!strcmp(s, "full")) return 0;
  if (!strcmp(s, "hats")) return 1;
  if (!strcmp(s, "kick")) return 2;
  return 3;
}

static int wmode(SGRNG *r, const JV *list)
{
  return md_index(jv_wstr(r, list));
}

static void make_material(COMP *c)
{
  SGRNG *rng = &c->rng;
  const JV *modes = S(c, "modes");
  int has_drums = jv_len(S(c, "drums")) > 0, s;
  static const int order[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_C, SEC_OUTRO};
  memset(c->motif_have, 0, sizeof c->motif_have);
  c->last_lead_note = -1;
  for (s = 0; s < 5; s++) comp_acid_riff(c, c->riffs[order[s]]);
  if (rng_random(rng) < 0.7) memcpy(c->riffs[SEC_B], c->riffs[SEC_A], sizeof c->riffs[SEC_A]);
  for (s = SEC_A2; s <= SEC_C2; s++) memcpy(c->riffs[s], c->riffs[sec_base(s)], sizeof c->riffs[s]);
  for (s = 0; s < SEC_NUM; s++)
  {
    int sec = s < 5 ? order[s] : s, b = sec_base(sec);
    SGRNG vr, *v = rng;
    ARR *a = &c->arr[sec];
    const JV *ml;
    if ((s >= 5) && (!c->islong) && (c->sids < 2))
    {
      c->arr[sec] = c->arr[b];
      continue;
    }
    if (s >= 5)
    {
      rng_seed(&vr, c->seed * 17 + sg_secname[sec][0]);
      v = &vr;
    }
    ml = jv_get(modes, sg_secname[b]);
    a->mode2 = wmode(v, ml);
    if (b == SEC_INTRO)
    {
      a->drums = has_drums ? drummode(jv_wstr(rng, S(c, "intro_drums"))) : 3;
      a->bass = rng_random(rng) < 0.5;
      a->lead = 0;
    }
    else if (b == SEC_C)
    {
      a->drums = has_drums ? drummode(jv_wstr(v, S(c, "break_drums"))) : 3;
      a->bass = rng_random(v) < 0.7;
      a->lead = rng_random(v) < 0.5;
    }
    else if (sec == SEC_OUTRO)
    {
      a->drums = (has_drums && (rng_random(rng) < 0.5)) ? 0 : (has_drums ? 1 : 3);
      a->bass = 1;
      a->lead = rng_random(rng) < 0.4;
    }
    else
    {
      a->drums = has_drums ? 0 : 3;
      a->bass = 1;
      a->lead = 1;
    }
    if ((c->jingle) && ((sec == SEC_C) || (sec == SEC_OUTRO)))
    {
      a->lead = 1;
      a->bass = 1;
      a->drums = has_drums ? 0 : 3;
      if ((a->mode2 == MD_ECHO) || (a->mode2 == MD_COUNTER)) a->mode2 = MD_ARP_HOLD;
    }
    if ((a->mode2 == MD_ECHO) && (!a->lead)) a->mode2 = MD_ARP_HOLD;
    if (c->sids >= 2)
    {
      double w[32];
      int m[32], n = 0, k;
      for (k = 0; (k < jv_len(ml)) && (n < 24); k++)
      {
        int md = md_index(jv_str(jv_wval(ml, k), ""));
        if ((md == a->mode2) || (md == MD_ECHO)) continue;
        m[n] = md;
        w[n] = jv_num(jv_at(jv_at(ml, k), 1), 1);
        n++;
      }
      if (a->mode2 != MD_PAD) { m[n] = MD_PAD; w[n++] = 1; }
      if (a->mode2 != MD_BROKEN) { m[n] = MD_BROKEN; w[n++] = 1; }
      if (a->mode2 != MD_COUNTER) { m[n] = MD_COUNTER; w[n++] = 1; }
      a->mode2b = m[rng_wchoice(v, w, n)];
      if (a->lead)
      {
        double w3[2] = {3, 2};
        a->mode3 = rng_wchoice(v, w3, 2) == 0 ? MD_HARMONY : MD_ECHO;
      }
      else
      {
        int m3[3], n3 = 0;
        double w3[3] = {1, 1, 1};
        static const int opts[3] = {MD_COUNTER, MD_PAD, MD_BROKEN};
        for (k = 0; k < 3; k++)
          if ((opts[k] != a->mode2) && (opts[k] != a->mode2b)) m3[n3++] = opts[k];
        a->mode3 = n3 ? m3[rng_wchoice(v, w3, n3)] : MD_PAD;
      }
      if (a->mode2 == MD_ECHO) a->mode2 = MD_ARP_HOLD;
    }
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void comp_render(COMP *c);          // gsgrender.c

int comp_compose(COMP *c)
{
  SGRNG *rng = &c->rng;
  const JV *st = c->st, *lays;
  int fam_any = 0, fam_min, k, has_kit = jv_len(S(c, "drums")) > 0;
  const char *fam;
  if (!st)
  {
    snprintf(c->err, sizeof c->err, "Style data missing");
    return 0;
  }
  c->spb = (int)jv_num(S(c, "spb"), 16);
  c->bt = c->spb / 4;
  c->L = jv_get(st, "lead");
  {
    const JV *alt = S(c, "lead_alt"), *L = c->L, *A = NULL;
    if (jv_len(alt)) A = jv_at(alt, rng_below(&c->rng_mel, jv_len(alt)));
#define LNUM(k, d) (jv_get(A, k) ? jv_num(jv_get(A, k), d) : jv_num(jv_get(L, k), d))
    c->L_rest = LNUM("rest", 0.2);
    c->L_leap = LNUM("leap", 0.2);
    c->L_repeat = LNUM("repeat", 0.15);
    c->L_pickup = jv_get(A, "pickup") || jv_get(L, "pickup") ? LNUM("pickup", 0.35) : 0.35;
    c->L_ornament = LNUM("ornament", 0);
    c->L_lo = (int)LNUM("lo", 55);
    c->L_hi = (int)LNUM("hi", 79);
    c->L_motif_bars = (int)LNUM("motif_bars", 2);
    c->L_callresp = jv_true(jv_get(A, "callresp") ? jv_get(A, "callresp") : jv_get(L, "callresp"));
    if ((A) && (jv_get(A, "rhythm"))) c->L = (JV *)A;     // the alternative rhythm (other fields: merged above)
    if (!jv_get(c->L, "rhythm")) c->L = (JV *)L;
#undef LNUM
  }
  {
    const JV *t = S(c, "tempos");
    const JV *x = jv_at(t, rng_below(rng, jv_len(t)));
    c->tempo[0] = (int)jv_num(jv_at(x, 0), 6);
    c->tempo[1] = (int)jv_num(jv_at(x, 1), c->tempo[0]);
    if (c->want_bpm > 0) tempo_for_bpm(c, c->want_bpm);
    if (c->fixed) { c->tempo[0] = c->fixed->tempo[0]; c->tempo[1] = c->fixed->tempo[1]; }
  }
  {
    double avg = (c->tempo[0] + c->tempo[1]) / 2.0;
    c->bar_sec = c->spb * avg / 50.0;
    c->bpm = 3000.0 / (avg * c->bt);
  }
  c->key = rng_below(rng, 12);
  c->mode = sg_modeindex(jv_wstr(rng, S(c, "scales")));
  if (c->want_key >= 0) c->key = c->want_key % 12;
  if (c->want_mode >= 0) c->mode = c->want_mode;
  if (c->fixed) { c->key = c->fixed->key; c->mode = c->fixed->mode; }
  scale_init(&c->scale, c->key, c->mode);
  fam_min = !((c->mode == M_MAJOR) || (c->mode == M_LYDIAN) || (c->mode == M_MIXOLYDIAN) || (c->mode == M_PENTMAJ));
  if (!strcmp(c->stylekey, "blues")) fam_any = 1;
  fam = fam_any ? "any" : fam_min ? "min" : "maj";
  lays = S(c, "layouts");
  {
    double w[32];
    int n = jv_len(lays), haskb = 0;
    if (n > 32) n = 32;
    for (k = 0; k < n; k++)
    {
      w[k] = jv_num(jv_at(jv_at(lays, k), 1), 1);
      if (!strcmp(jv_str(jv_wval(lays, k), ""), "KB+L+D")) haskb = 1;
    }
    if ((c->sids == 1) && (has_kit) && (haskb))
      for (k = 0; k < n; k++)
      {
        const char *l = jv_str(jv_wval(lays, k), "");
        w[k] *= !strcmp(l, "KB+L+D") ? 1.4 : !strcmp(l, "BD+L+C") ? 1.2 : 1.0;
      }
    k = n ? rng_wchoice(rng, w, n) : -1;
    snprintf(c->layout, sizeof c->layout, "%s", k >= 0 ? jv_str(jv_wval(lays, k), "BD+L+C") : "BD+L+C");
  }
  {
    char lay0[48];
    snprintf(lay0, sizeof lay0, "%s", c->layout);
    if (c->voices == 2) snprintf(c->layout, sizeof c->layout, "%s", has_kit ? "BD+L" : "B+L");
    if (c->sids >= 2)
    {
      int dance = !strcmp(lay0, "KB+L+D");
      snprintf(c->track_roles[0], 32, "%s", (dance && has_kit) ? "Kick+Bass" : "Bass");
      snprintf(c->track_roles[1], 32, "Melody");
      snprintf(c->track_roles[2], 32, "%s", c->voices == 3 ? "Chords" : "free (effects)");
      snprintf(c->track_roles[3], 32, "%s", has_kit ? "Drums" : "Accompaniment");
      snprintf(c->track_roles[4], 32, "Accompaniment");
      snprintf(c->track_roles[5], 32, "Harmony/Echo");
      if (c->voices == 2) snprintf(c->layout, sizeof c->layout, "%s%s", dance ? "KB+L" : "B+L", has_kit ? "|D+C+H" : "|C+P+H");
      else snprintf(c->layout, sizeof c->layout, "%s%s", dance ? "KB+L+C" : "B+L+C", has_kit ? "|D+P+H" : "|K+P+H");
      c->dance_kb = dance;
      if (c->sids >= 4) snprintf(c->layout + strlen(c->layout), sizeof c->layout - strlen(c->layout), "|layers%d", c->sids);
    }
  }
  // chord progressions per section
  {
    const JV *prog = S(c, "prog"), *chosenpool[3] = {NULL, NULL, NULL}, *chosen[3] = {NULL, NULL, NULL};
    static const char *secs[3] = {"A", "B", "C"};
    int okidx[64], okv[3][64], nokv[3];
    for (k = 0; k < 3; k++)
    {
      const JV *pool = jv_get(prog, secs[k]);
      int n = jv_len(pool), j, nok = 0;
      for (j = 0; (j < n) && (nok < 64); j++)
      {
        const char *f = prog_family(jv_at(pool, j));
        if ((fam_any) || (!strcmp(f, fam)) || (!strcmp(f, "neutral")) || (!strcmp(f, "mixed"))) okidx[nok++] = j;
      }
      if (nok) chosen[k] = jv_at(pool, okidx[rng_below(rng, nok)]);
      else chosen[k] = jv_at(pool, rng_below(rng, n));
      chosenpool[k] = pool;
      // the variants choose from the same list (ok or pool)
      nokv[k] = nok;
      memcpy(okv[k], okidx, sizeof okidx);
      setprog(c, SEC_A + k, chosen[k]);
    }
    k = rng_below(rng, 2);
    memcpy(c->progs[SEC_INTRO], c->progs[SEC_A + k], sizeof c->progs[0]);
    c->nprog[SEC_INTRO] = c->nprog[SEC_A + k];
    c->tonic.root = c->key;
    c->tonic.q = fam_min ? Q_MIN : Q_MAJ;
    if (fam_any) c->tonic.q = Q_MAJ;
    for (k = 0; k < c->nprog[SEC_A]; k++)
      if (c->progs[SEC_A][k].q == Q_POW) c->tonic.q = Q_POW;
    memcpy(c->progs[SEC_OUTRO], c->progs[SEC_A], sizeof c->progs[0]);
    c->nprog[SEC_OUTRO] = c->nprog[SEC_A];
    c->islong = (c->target > 200) && c->variants;
    for (k = SEC_A2; k <= SEC_C2; k++)
    {
      memcpy(c->progs[k], c->progs[sec_base(k)], sizeof c->progs[0]);
      c->nprog[k] = c->nprog[sec_base(k)];
    }
    if (c->islong)
    {
      SGRNG vr;
      rng_seed(&vr, c->seed * 7 + 3);
      for (k = 0; k < 3; k++)
      {
        const JV *pool = chosenpool[k], *other[64];
        int n = 0, j;
        int np = nokv[k] ? nokv[k] : jv_len(pool);
        for (j = 0; (j < np) && (n < 64); j++)
        {
          // compared by value, as Python's list comparison
          const JV *q = jv_at(pool, nokv[k] ? okv[k][j] : j);
          int m, same = jv_len(q) == jv_len(chosen[k]);
          for (m = 0; (same) && (m < jv_len(q)); m++)
            if (strcmp(jv_str(jv_at(q, m), ""), jv_str(jv_at(chosen[k], m), ""))) same = 0;
          if (!same) other[n++] = q;
        }
        if ((n) && (rng_random(&vr) < 0.7)) setprog(c, SEC_A2 + k, other[rng_below(&vr, n)]);
        else setprog(c, SEC_A2 + k, chosen[k]);
      }
    }
    {
      const JV *cbl = S(c, "chord_bars");
      static const int order[5] = {SEC_A, SEC_B, SEC_C, SEC_INTRO, SEC_OUTRO};
      int n = jv_len(cbl);
      for (k = 0; k < 5; k++) c->chord_bars[order[k]] = n ? (int)jv_num(jv_at(cbl, rng_below(rng, n)), 1) : 1;
      for (k = SEC_A2; k <= SEC_C2; k++) c->chord_bars[k] = c->chord_bars[sec_base(k)];
      if (c->islong)
      {
        SGRNG vr;
        rng_seed(&vr, c->seed * 11 + 5);
        for (k = SEC_A2; k <= SEC_C2; k++) c->chord_bars[k] = n ? (int)jv_num(jv_at(cbl, rng_below(&vr, n)), 1) : 1;
      }
    }
  }
  pick_instruments(c);
  if (c->sids >= 4) comp_layer_roles(c);
  plan_structure(c);
  make_material(c);
  comp_render(c);
  if (c->expressive) sg_song_ideas(c);
  return 1;
}

// SIDGen's chords_for() for gsgrender.c
void comp_chords_for(COMP *c, int sec, int nbars, int last, CHORD *out) { chords_for(c, sec, nbars, last, out); }
const JV *comp_style(COMP *c, const char *key) { return S(c, key); }
int comp_isstraight(COMP *c) { return isstraight(c->stylekey); }

// C64 GrooveGoat - composer, part 2: rendering the sections into the
// voices (1 SID: layouts, shared voices; 2 SIDs: six voices) and the
// accompaniment modes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gsgcomp.h"

void comp_chords_for(COMP *c, int sec, int nbars, int last, CHORD *out);
const JV *comp_style(COMP *c, const char *key);
int comp_lead_section(COMP *c, int sec, int nbars, int row0, RND *out, int max);
void comp_drum_bar(COMP *c, int sec, int mode, int bar, int nbars, int first_of_sec, int *steps);

#define MAXLEAD 1024
#define MAXSECROWS (64 * 16)

static int samechord(CHORD a, CHORD b) { return (a.root == b.root) && (a.q == b.q); }

static CHORD chord_at(COMP *c, int row)
{
  if (row >= c->nchordrows) row = c->nchordrows - 1;
  if (row < 0) row = 0;
  return c->chord_rows[row];
}

static const char *lead_for(COMP *c, int sec)
{
  if ((sec >= SEC_A2) && (c->i_lead_v[0])) return sec != SEC_B2 ? c->i_lead_v : c->i_lead_b;
  return sec_base(sec) == SEC_B ? c->i_lead_b : c->i_lead;
}

static const char *tom_for(COMP *c, int s)
{
  const JV *toms = jv_get(comp_style(c, "kit"), "tom");
  int n = jv_len(toms), pos;
  if (!n) return "Tom mittel";
  pos = (s % c->spb) * n / c->spb;
  if (pos > n - 1) pos = n - 1;
  return jv_str(jv_at(toms, pos), "Tom mittel");
}

static int inscat(COMP *c, int i, const char *cat) { return !strcmp(c->ins[i].cat, cat); }

// would this instrument carry the filter? (same rule as build_song)
static int filtered_name(COMP *c, int i)
{
  const char *role = c->filter_role;
  if (!role[0]) return 0;
  if (!strcmp(role, "bass")) return inscat(c, i, "bass") || inscat(c, i, "kickbass");
  if (!strcmp(role, "lead"))
    return (!strcmp(c->ins[i].name, c->i_lead)) || (!strcmp(c->ins[i].name, c->i_lead_b)) || (!strcmp(c->ins[i].name, c->i_lead_v));
  if (!strcmp(role, "pad")) return inscat(c, i, "arp") && (!strncmp(c->ins[i].name, c->t_pad + 4, strlen(c->t_pad + 4)));
  return 0;
}

static void render_drums(COMP *c, SGTRACK *t, int row0, const int *drums, int n, int skip)
{
  static const struct { int bit; int cat; } prio[9] = {{DL_X, SK_CRASH}, {DL_S, SK_SNARE}, {DL_C, SK_CLAP}, {DL_K, SK_KICK},
    {DL_T, -1}, {DL_O, SK_OHAT}, {DL_H, SK_HAT}, {DL_R, SK_RIDE}, {DL_P, SK_PERC}};
  int s, k;
  for (s = 0; s < n; s++)
    for (k = 0; k < 9; k++)
    {
      if ((drums[s] & prio[k].bit) && (!(prio[k].bit & skip)))
      {
        const char *name;
        if (prio[k].cat < 0) name = tom_for(c, s);
        else name = c->kit[prio[k].cat][0] ? c->kit[prio[k].cat] : c->kit[SK_SNARE];
        tr_note(t, row0 + s, 48, comp_use(c, name), -1);
        break;
      }
    }
}

static void render_kickbass(COMP *c, SGTRACK *t, int row0, int nbars, const int *bn, const int *bd, const int *drums)
{
  int spb = c->spb, s, kick = comp_use(c, c->kit[SK_KICK]), bi = comp_use(c, c->i_bass);
  int res_n = -1, res_end = 0, kick_end = -1;
  for (s = 0; s < nbars * spb; s++)
  {
    int row = row0 + s, on = bn[s] >= 0;
    if (drums[s] & DL_K)
    {
      tr_note(t, row, 48, kick, -1);
      kick_end = s + 2;
      if ((on) && (bd[s] > 2)) { res_n = bn[s]; res_end = s + bd[s]; }
      else if ((res_n >= 0) && (res_end <= s + 2)) res_n = -1;
      continue;
    }
    if (s < kick_end) continue;
    if (on)
    {
      tr_note(t, row, bn[s], bi, bd[s]);
      res_n = -1;
    }
    else if ((res_n >= 0) && (s < res_end))
    {
      tr_note(t, row, res_n, bi, res_end - s);
      res_n = -1;
    }
  }
}

static void render_bassdrum(COMP *c, SGTRACK *t, int row0, int nbars, const CHORD *chords, const int *bn, const int *bd, const int *drums)
{
  int spb = c->spb, s, bi = comp_use(c, c->i_bass);
  int cur = -1, sound_end = -1, res_n = -1, res_end = 0;
  for (s = 0; s < nbars * spb; s++)
  {
    int letters = drums[s], onset = bn[s] >= 0, row = row0 + s;
    CHORD ch = chords[s / spb];
    if (onset) cur = bn[s];
    else if ((s % spb == 0) && (cur < 0)) cur = comp_bass_root(ch.root);
    if (letters & (DL_S | DL_C))
    {
      if ((sound_end > s + 2) && (cur >= 0)) { res_n = cur; res_end = sound_end; }
      tr_note(t, row, 48, comp_use(c, c->kit[SK_SNARE]), -1);
      sound_end = s;
      continue;
    }
    if ((onset) || (letters & DL_K)) res_n = -1;
    if (letters & DL_K)
    {
      int n = onset ? bn[s] : (cur >= 0 ? cur : comp_bass_root(ch.root)), d;
      if (onset) d = bd[s];
      else if (sound_end > s) d = sound_end - s;
      else d = 2;
      tr_note(t, row, n, comp_use(c, c->i_kickbass), d);
      sound_end = s + d;
    }
    else if (letters & DL_T)
    {
      tr_note(t, row, 48, comp_use(c, tom_for(c, s)), -1);
      sound_end = s;
    }
    else if (onset)
    {
      tr_note(t, row, bn[s], bi, bd[s]);
      sound_end = s + bd[s];
    }
    else if ((res_n >= 0) && (s < res_end) && (!letters))
    {
      tr_note(t, row, res_n, bi, res_end - s);
      sound_end = res_end;
      res_n = -1;
    }
    else if ((jv_true(comp_style(c, "bd_hats"))) && (letters & (DL_H | DL_O)) && (sound_end <= s))
      tr_note(t, row, 48, comp_use(c, c->kit[SK_HAT]), -1);
  }
}

// ---------------------------------------------------------------------------
// Accompaniment
// ---------------------------------------------------------------------------

void comp_render_mode2(COMP *c, SGTRACK *t, int row0, int nbars, CHORD *chords_in, int mode, RND *lead, int nlead, int last,
                       const char *stab, int tpl2)
{
  int spb = c->spb, bt = c->bt, b, i, k;
  CHORD chords[256];
  struct { int b, ln; CHORD c; } spans[256];
  int nspans = 0;
  SGRNG *rng = c->cur;
  if (nbars > 256) nbars = 256;
  memcpy(chords, chords_in, sizeof(CHORD) * nbars);
  if ((last) && (nbars)) chords[nbars - 1] = c->tonic;
  b = 0;
  while (b < nbars)
  {
    int e = b + 1;
    while ((e < nbars) && (samechord(chords[e], chords[b]))) e++;
    spans[nspans].b = b;
    spans[nspans].ln = e - b;
    spans[nspans].c = chords[b];
    nspans++;
    b = e;
  }
  if (mode == MD_ECHO)
  {
    const SGINS *src = sg_findins(c->i_lead);          // I.by_name(self.i_lead)
    SGINS e;
    int name, d = c->echo_delay, total = nbars * spb;
    static signed char sound[MAXSECROWS + 64];
    if (!src) return;
    sg_echoof(src, &e);
    for (name = 0; name < c->numins; name++)
      if (!strcmp(c->ins[name].name, e.name)) break;
    if (name >= c->numins)
    {
      if (c->numins >= SG_MAXINS) return;
      c->ins[c->numins] = e;
      c->arp_tpl[c->numins] = -1;
      name = c->numins++;
    }
    for (i = 0; i < total + 64 && i < MAXSECROWS + 64; i++) sound[i] = -1;
    for (i = 0; i < nlead; i++)
      for (k = lead[i].row; (k < lead[i].row + lead[i].dur) && (k < MAXSECROWS + 64); k++) sound[k] = lead[i].note;
    for (i = 0; i < nlead; i++)
    {
      int s = lead[i].row, n = lead[i].note, du = lead[i].dur, ee = s + d, keep = 0;
      if (ee >= total) continue;
      for (k = ee; (k < ee + du) && (k < total); k++)
      {
        int m = k < MAXSECROWS + 64 ? sound[k] : -1;
        CHORD ch = chord_at(c, row0 + k);
        int iv = m >= 0 ? (((n - m) % 12) + 12) % 12 : -1;
        if (((m >= 0) && ((iv == 1) || (iv == 11) || (iv == 6))) || (!((comp_chord_scale(c, ch) >> (((n % 12) + 12) % 12)) & 1))) break;
        keep++;
      }
      if (keep)
      {
        tr_note(t, row0 + ee, n, name, keep);
        rl_add(&c->echo_rows, row0 + ee, n, keep);
      }
    }
    return;
  }
  if (mode == MD_FIFTHS)
  {
    int name = comp_use(c, c->i_counter);
    if (!nlead)
    {
      comp_render_mode2(c, t, row0, nbars, chords_in, MD_PAD, lead, nlead, last, stab, tpl2);
      return;
    }
    for (i = 0; i < nlead; i++)
    {
      int n = lead[i].note, m;
      CHORD ch = chord_at(c, row0 + lead[i].row);
      int pcs = comp_chord_scale(c, ch);
      if ((pcs >> ((((n - 7) % 12) + 12) % 12)) & 1) m = n - 7;
      else if ((pcs >> ((((n - 5) % 12) + 12) % 12)) & 1) m = n - 5;
      else m = n - 12;
      tr_note(t, row0 + lead[i].row, m, name, lead[i].dur);
    }
    return;
  }
  if (mode == MD_HARMONY)
  {
    int name = comp_use(c, c->i_harm);
    for (i = 0; i < nlead; i++)
    {
      int n = lead[i].note, m, best = -1;
      CHORD ch = chord_at(c, row0 + lead[i].row);
      int pcs = comp_chord_pcs(ch);
      for (m = n - 9; m < n - 2; m++)
        if ((pcs >> (((m % 12) + 12) % 12)) & 1)
          if ((best < 0) || (abs((n - m) - 4) < abs((n - best) - 4)) || ((abs((n - m) - 4) == abs((n - best) - 4)) && (m < best))) best = m;
      if (best < 0) continue;
      tr_note(t, row0 + lead[i].row, best, name, lead[i].dur);
    }
    return;
  }
  if ((mode == MD_ARP_HOLD) || (mode == MD_PAD))
  {
    const char *tpl = mode == MD_ARP_HOLD ? c->t_arp : c->t_pad;
    if ((tpl2) && (mode == MD_ARP_HOLD) && (c->t_arp2[0])) tpl = c->t_arp2;
    for (i = 0; i < nspans; i++)
      tr_note(t, row0 + spans[i].b * spb, 48 + spans[i].c.root, comp_use_arp(c, tpl, spans[i].c.q), spans[i].ln * spb);
  }
  else if (mode == MD_ARP_RHYTHM)
  {
    const char *pat = stab ? stab : c->stab;
    int hits[64], nh = 0;
    for (i = 0; pat[i] && nh < 64; i++)
      if (pat[i] == 'x') hits[nh++] = i;
    for (b = 0; b < nbars; b++)
    {
      int insn = comp_use_arp(c, c->t_stab, chords[b].q);
      for (k = 0; k < nh; k++)
      {
        int nxt = k + 1 < nh ? hits[k + 1] : spb, d = nxt - hits[k];
        tr_note(t, row0 + b * spb + hits[k], 48 + chords[b].root, insn, d < 2 ? d : 2);
      }
    }
  }
  else if (mode == MD_COUNTER)
  {
    int prev = 64, insn = comp_use(c, c->i_counter);
    for (b = 0; b < nbars; b++)
    {
      CHORD ch = chords[b];
      int mask = 0, n, best = -1;
      double bs = 1e9;
      for (k = 1; k < sg_qlen[ch.q]; k++) mask |= 1 << ((ch.root + sg_qiv[ch.q][k]) % 12);
      for (n = 55; n < 72; n++)
        if ((mask >> (n % 12)) & 1)
        {
          double v = abs(n - prev) + rng_random(rng) * 2;
          if (v < bs) { bs = v; best = n; }
        }
      if (best < 0) best = prev;
      tr_note(t, row0 + b * spb, best, insn, spb);
      prev = best;
    }
  }
  else if (mode == MD_BROKEN)
  {
    int insn = comp_use(c, c->i_broken);
    for (b = 0; b < nbars; b++)
    {
      CHORD ch = chords[b];
      const int *iv = sg_qiv[ch.q];
      int r = 48 + ch.root, seq[4];
      int third = sg_qlen[ch.q] > 2 ? r + iv[2] : r + 7;
      seq[0] = r; seq[1] = third; seq[2] = r + iv[1]; seq[3] = third;
      for (i = 0; i < 8; i++)
      {
        int pos = (i / 2) * bt + (i % 2 ? 2 : 0);
        tr_note(t, row0 + b * spb + pos, seq[i % 4], insn, i % 2 == 0 ? 2 : bt - 2);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Shared voices (3 voices, 1 SID)
// ---------------------------------------------------------------------------

typedef struct
{
  int lay;                   // 0 BD+L+C, 1 D+B+LC, 2 B+L+C, 3 KB+L+D, 4 BD+L, 5 B+L
  RND *lead;
  int nlead;
  int *drums;
  int has_drums;
  ARR *a;
  int ch_lead, ch_drum, ch_bass, chords_done;
  int *bn, *bd;
  int last;
} CTX;

static int drum_voice_bass(COMP *c, CTX *x, int row0, int nbars, CHORD *chords, SGRNG *sr)
{
  int spb = c->spb, bt = c->bt, s, e8 = bt == 4 ? 2 : 1, placed = 0, insn, k;
  const char *name = c->i_bass;
  static signed char sound[MAXSECROWS + 64];
  int main = DL_K | DL_S | DL_C | DL_X | DL_T, have = 0;
  SGINS ob;
  (void)sr;
  if (!strcmp(c->filter_role, "bass"))
  {
    // the bass carries the filter: an unfiltered copy (pulse or triangle bass sounds best raw)
    const JV *bl = comp_style(c, "bass_inst");
    const char *list[64];
    const SGINS *src = NULL;
    int n = 0;
    list[n++] = c->i_bass;
    for (k = 0; (k < jv_len(bl)) && (n < 60); k++) list[n++] = jv_str(jv_at(bl, k), "");
    list[n++] = "Bass Pluck";
    list[n++] = "Tiefbass Puls";
    for (k = 0; k < n; k++)
    {
      const SGINS *i = sg_findins(sg_formodel(list[k], c->model));
      int w;
      if (!i) continue;
      w = sg_mainwave(i);
      if ((w == 0x10) || (w == 0x40)) { src = i; break; }
    }
    if (!src) return 0;
    sg_octavebass(src, &ob);
    name = NULL;
  }
  for (s = 0; s < nbars * spb; s++)
    if (x->bn[s] >= 0) have = 1;
  if ((!have) || ((spb != 12) && (spb != 16))) return 0;
  {
    static const char *all[] = {"offbeat", "rolling", "root16", "arpbass", "acid", "eighties", "sequencer"};
    int sub = 1, j;
    for (k = 0; k < SEC_NUM; k++)
    {
      int in = 0;
      for (j = 0; j < 7; j++)
        if (!strcmp(c->bass_pat[k], all[j])) in = 1;
      if (!in) sub = 0;
    }
    if (sub) return 0;                 // the bass already plays every offbeat
  }
  for (s = 0; s < nbars * spb + 64 && s < MAXSECROWS + 64; s++) sound[s] = -1;
  for (s = 0; s < nbars * spb; s++)
    if (x->bn[s] >= 0)
      for (k = s; (k < s + (x->bd[s] > 0 ? x->bd[s] : 1)) && (k < MAXSECROWS + 64); k++) sound[k] = x->bn[s];
  if (name) insn = comp_use(c, name);
  else
  {
    for (insn = 0; insn < c->numins; insn++)
      if (!strcmp(c->ins[insn].name, ob.name)) break;
    if (insn >= c->numins)
    {
      if (c->numins >= SG_MAXINS) return 0;
      c->ins[c->numins] = ob;
      c->arp_tpl[c->numins] = -1;
      insn = c->numins++;
    }
  }
  {
    SGTRACK *t = &c->tracks[x->ch_drum];
    for (s = 0; s < nbars * spb; s++)
    {
      int xx = x->drums[s], beat, n, row, blocked = 0;
      if ((s % bt != bt - e8) || (s / spb >= nbars - (x->last ? 1 : 0))) continue;
      if (xx & main) continue;
      for (k = s + 1; (k < s + e8) && (k < nbars * spb); k++)
        if (x->drums[k] & main) blocked = 1;
      if (blocked) continue;
      beat = (s % spb) / bt;
      if ((xx) && (beat != 1) && (beat != 3)) continue;     // keep most hi-hats
      n = sound[s];
      if (n < 0) n = comp_bass_root(chords[(s / spb) < nbars - 1 ? s / spb : nbars - 1].root);
      row = row0 + s;
      if ((row < t->rows) && (t->ev[row].type == EV_NOTE))
      {
        const char *cat = c->ins[t->ev[row].ins].cat;
        if (strcmp(cat, "hat") && strcmp(cat, "ohat") && strcmp(cat, "ride") && strcmp(cat, "perc")) continue;
      }
      tr_note(t, row, n + 12, insn, e8);
      placed++;
    }
  }
  return placed > 0;
}

static void share_voices(COMP *c, int sec, int nbars, CHORD *chords, int row0, int last, CTX *x)
{
  int spb = c->spb, bt = c->bt, done_db = 0, done_any = 0;
  double p;
  SGRNG sr;
  if ((c->voices != 3) || (c->jingle)) return;
  rng_seeds(&sr, "%u-share-%d", c->seed, row0);
  if ((x->lay == 1) && (x->has_drums) && (rng_random(&sr) < jv_num(comp_style(c, "drum_share"), 0.9)))
  {
    if (drum_voice_bass(c, x, row0, nbars, chords, &sr)) done_db = done_any = 1;
  }
  p = jv_num(comp_style(c, "share"), 0.6);
  if (rng_random(&sr) >= (done_any ? (p > 0.8 ? p : 0.8) : p))
  {
    if (done_any) c->nshared++;
    return;
  }
  // 1) separate drum voice: chord stabs on the free offbeats
  if ((x->lay == 1) && ((x->nlead) || (x->a->mode2 != MD_ARP_RHYTHM)) && (x->has_drums) && (!done_db) &&
      (!filtered_name(c, comp_use_arp(c, c->t_stab, chords[0].q))))
  {
    SGTRACK tmp, *td = &c->tracks[x->ch_drum];
    char stab[40];
    int r, k;
    tr_init(&tmp, c->total_rows + 64);
    if (rng_random(&sr) < 0.7) snprintf(stab, sizeof stab, "%s", c->stab);
    else
    {
      stab[0] = 0;
      for (k = 0; k < 4; k++) strcat(stab, bt == 4 ? "..x." : ".x.");
    }
    comp_render_mode2(c, &tmp, row0, nbars, chords, MD_ARP_RHYTHM, x->lead, x->nlead, last, stab, 0);
    for (r = 0; r < tmp.rows; r++)
    {
      if ((tmp.ev[r].type == EV_NOTE) && (r < td->rows) && (td->ev[r].type == EV_NOTE))
      {
        const char *cat = c->ins[td->ev[r].ins].cat;
        if ((!strcmp(cat, "hat")) || (!strcmp(cat, "ride")) || (!strcmp(cat, "perc"))) memset(&td->ev[r], 0, sizeof(SGEV));
      }
    }
    tr_merge_low(td, &tmp, 1, 2);
    tr_free(&tmp);
    done_any = 1;
  }
  // 2) rests of the melody: a short broken-chord answer
  if ((x->nlead) && (!filtered_name(c, comp_use(c, lead_for(c, sec)))) && (rng_random(&sr) < 0.75))
  {
    int insn = comp_use(c, c->i_broken);
    if (!filtered_name(c, insn))
    {
      SGTRACK tmp;
      int gaps[256][2], ng = 0, prev_end = 0, i, e8 = (bt == 4 || bt == 3) ? 2 : 1;
      RND sortedl[MAXLEAD];
      tr_init(&tmp, c->total_rows + 64);
      memcpy(sortedl, x->lead, sizeof(RND) * x->nlead);
      for (i = 0; i < x->nlead; i++)
      {
        int j;
        for (j = i + 1; j < x->nlead; j++)
          if ((sortedl[j].row < sortedl[i].row) || ((sortedl[j].row == sortedl[i].row) && (sortedl[j].note < sortedl[i].note)))
          {
            RND t = sortedl[i];
            sortedl[i] = sortedl[j];
            sortedl[j] = t;
          }
      }
      for (i = 0; i < x->nlead; i++)
      {
        if ((sortedl[i].row - prev_end >= 2 * bt) && (ng < 255)) { gaps[ng][0] = prev_end; gaps[ng][1] = sortedl[i].row; ng++; }
        if (sortedl[i].row + sortedl[i].dur > prev_end) prev_end = sortedl[i].row + sortedl[i].dur;
      }
      if ((nbars * spb - prev_end >= 2 * bt) && (!last) && (ng < 255)) { gaps[ng][0] = prev_end; gaps[ng][1] = nbars * spb; ng++; }
      for (i = 0; i < ng; i++)
      {
        int pos = ((gaps[i][0] + e8 - 1) / e8) * e8, k = 0;
        while (pos + e8 <= gaps[i][1] - 1)
        {
          CHORD ch = chords[pos / spb < nbars - 1 ? pos / spb : nbars - 1];
          const int *iv = sg_qiv[ch.q];
          int base_n = 60 + ch.root < 67 ? 60 + ch.root : 48 + ch.root;
          int seq[4];
          seq[0] = base_n + iv[0];
          seq[1] = base_n + iv[1];
          seq[2] = base_n + iv[2 % sg_qlen[ch.q]];
          seq[3] = base_n + 12;
          tr_note(&tmp, row0 + pos, (k / 4) % 2 == 0 ? seq[k % 4] : seq[3 - k % 4], insn, e8);
          pos += e8;
          k++;
        }
      }
      tr_merge_low(&c->tracks[x->ch_lead], &tmp, 1, 2);
      tr_free(&tmp);
      done_any = 1;
    }
  }
  if (done_any) c->nshared++;
}

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

static void section_material(COMP *c, int sec, int nbars, CHORD *chords, int row0, int first, int last, int ch_drum_ok,
                             int kickx, RND *lead, int *nlead, int *bn, int *bd, int *drums)
{
  ARR *a = &c->arr[sec];
  int spb = c->spb, bt = c->bt, b, s;
  *nlead = a->lead ? comp_lead_section(c, sec, nbars, row0, lead, MAXLEAD - 4) : 0;
  if ((last) && (*nlead))
  {
    int n = 0, k, ton = 60;
    for (k = 0; k < *nlead; k++)
      if (lead[k].row < (nbars - 1) * spb) lead[n++] = lead[k];
    for (k = 60; k < 80; k++)
      if (k % 12 == c->key) { ton = k; break; }
    lead[n].row = (nbars - 1) * spb;
    lead[n].note = ton;
    lead[n].dur = spb - bt;
    *nlead = n + 1;
  }
  for (s = 0; s < nbars * spb; s++) { bn[s] = -1; bd[s] = 0; }
  if (a->bass)
  {
    for (b = 0; b < nbars; b++)
    {
      RND out[64];
      int n = 0, k;
      CHORD nxt = chords[(b + 1) % nbars];
      comp_bass_bar(c, c->bass_pat[sec], chords[b], nxt, b, c->riffs[sec], out, &n);
      for (k = 0; k < n; k++)
      {
        bn[b * spb + out[k].row] = out[k].note;
        bd[b * spb + out[k].row] = out[k].dur;
      }
    }
  }
  if (last)
  {
    for (s = (nbars - 1) * spb; s < nbars * spb; s++) bn[s] = -1;
    bn[(nbars - 1) * spb] = comp_bass_root(c->key);
    bd[(nbars - 1) * spb] = spb - bt;
  }
  for (s = 0; s < nbars * spb; s++)
    if (bn[s] >= 0) rl_add(&c->bass_rows, row0 + s, bn[s], bd[s]);
  for (b = 0; b < nbars; b++)
  {
    if ((last) && (b == nbars - 1))
    {
      for (s = 0; s < spb; s++) drums[b * spb + s] = 0;
      if ((a->drums != 3) && (ch_drum_ok)) drums[b * spb] = kickx ? (DL_K | DL_X) : DL_K;
    }
    else comp_drum_bar(c, sec, a->drums, b, nbars, first, drums + b * spb);
  }
  for (s = 0; s < nbars * spb; s++)
    if (drums[s]) c->drum_rows[row0 + s] = drums[s];
}

static int layindex(const char *l)
{
  static const char *n[6] = {"BD+L+C", "D+B+LC", "B+L+C", "KB+L+D", "BD+L", "B+L"};
  int k;
  for (k = 0; k < 6; k++)
    if (!strcmp(n[k], l)) return k;
  return 0;
}

static void render3(COMP *c, int sec, int nbars, CHORD *chords, int row0, int first, int last)
{
  static RND lead[MAXLEAD];
  static int bn[MAXSECROWS], bd[MAXSECROWS], drums[MAXSECROWS];
  static const int map[6][4] = {{0, 0, 1, 2}, {1, 0, 2, 2}, {0, -1, 1, 2}, {0, 2, 1, 2}, {0, 0, 1, -1}, {0, -1, 1, -1}};
  ARR *a = &c->arr[sec];
  int spb = c->spb, bt = c->bt, lay = layindex(c->layout), nlead, s, k;
  int ch_bass = map[lay][0], ch_drum = map[lay][1], ch_lead = map[lay][2], ch_chord = map[lay][3];
  int lead_ins, has_drums = 0;
  CTX x;
  section_material(c, sec, nbars, chords, row0, first, last, ch_drum >= 0, (lay == 1) || (lay == 3), lead, &nlead, bn, bd, drums);
  lead_ins = comp_use(c, lead_for(c, sec));
  for (s = 0; s < nbars * spb; s++)
    if (drums[s]) has_drums = 1;
  has_drums = has_drums && (ch_drum >= 0);
  memset(&x, 0, sizeof x);
  x.lay = lay; x.lead = lead; x.nlead = nlead; x.drums = drums; x.has_drums = has_drums; x.a = a;
  x.ch_lead = ch_lead; x.ch_drum = ch_drum; x.ch_bass = ch_bass; x.bn = bn; x.bd = bd;
  x.last = 0;                    // 3-voice context has no 'last' (the last bar gets the octaves too)

  if ((lay == 0) || (lay == 4)) render_bassdrum(c, &c->tracks[0], row0, nbars, chords, bn, bd, drums);
  else if (lay == 3)
  {
    render_kickbass(c, &c->tracks[0], row0, nbars, bn, bd, drums);
    if (has_drums)
    {
      static int d3[MAXSECROWS];
      for (s = 0; s < nbars * spb; s++)
      {
        d3[s] = drums[s];
        if ((s % bt) != bt / 2) d3[s] &= ~DL_H;
      }
      render_drums(c, &c->tracks[2], row0, d3, nbars * spb, DL_K);
    }
  }
  else
  {
    SGTRACK *tb = &c->tracks[ch_bass];
    int bi = comp_use(c, c->i_bass);
    for (s = 0; s < nbars * spb; s++)
      if (bn[s] >= 0) tr_note(tb, row0 + s, bn[s], bi, bd[s]);
    if (ch_drum >= 0) render_drums(c, &c->tracks[ch_drum], row0, drums, nbars * spb, 0);
  }
  for (k = 0; k < nlead; k++)
  {
    tr_note(&c->tracks[ch_lead], row0 + lead[k].row, lead[k].note, lead_ins, lead[k].dur);
    rl_add(&c->lead_rows, row0 + lead[k].row, lead[k].note, lead[k].dur);
  }
  // accompaniment
  if (lay == 1)
  {
    if (!nlead) comp_render_mode2(c, &c->tracks[2], row0, nbars, chords, a->mode2, lead, nlead, last, NULL, 0);
    share_voices(c, sec, nbars, chords, row0, last, &x);
    return;
  }
  if ((ch_chord < 0) || ((lay == 3) && (has_drums)))
  {
    int fill = ((a->mode2 != MD_ECHO) && (a->mode2 != MD_COUNTER) && (a->mode2 != MD_FIFTHS)) ? a->mode2 : MD_ARP_HOLD;
    SGTRACK tmp, *target;
    char beats[40] = "";
    if (lay == 3) fill = nlead ? MD_ARP_RHYTHM : fill;
    tr_init(&tmp, c->total_rows + 64);
    for (k = 0; k < 4; k++)
    {
      int j;
      strcat(beats, "x");
      for (j = 1; j < bt; j++) strcat(beats, ".");
    }
    comp_render_mode2(c, &tmp, row0, nbars, chords, fill, lead, nlead, last, lay == 3 ? beats : NULL, 0);
    target = ((lay == 3) && (nlead)) ? &c->tracks[2] : &c->tracks[1];
    tr_merge_low(target, &tmp, target == &c->tracks[2] ? 1 : 2, 2);
    tr_free(&tmp);
    share_voices(c, sec, nbars, chords, row0, last, &x);
    return;
  }
  comp_render_mode2(c, &c->tracks[ch_chord], row0, nbars, chords, a->mode2, lead, nlead, last, NULL, 0);
  share_voices(c, sec, nbars, chords, row0, last, &x);
}

static void render6(COMP *c, int sec, int nbars, CHORD *chords, int row0, int first, int last)
{
  static RND lead[MAXLEAD];
  static int bn[MAXSECROWS], bd[MAXSECROWS], drums[MAXSECROWS];
  ARR *a = &c->arr[sec];
  SGTRACK *T = c->tracks;
  int spb = c->spb, nlead, s, k, dance = c->dance_kb, has_kit = jv_len(comp_style(c, "drums")) > 0, has_drums = 0, lead_ins;
  SGTRACK *freev[3];
  int nfree = 0, modes[2];
  section_material(c, sec, nbars, chords, row0, first, last, has_kit, 1, lead, &nlead, bn, bd, drums);
  lead_ins = comp_use(c, lead_for(c, sec));
  for (s = 0; s < nbars * spb; s++)
    if (drums[s]) has_drums = 1;
  has_drums = has_drums && has_kit;
  if ((dance) && (has_drums))
  {
    render_kickbass(c, &T[0], row0, nbars, bn, bd, drums);
    render_drums(c, &T[3], row0, drums, nbars * spb, DL_K);
  }
  else
  {
    int bi = comp_use(c, c->i_bass);
    for (s = 0; s < nbars * spb; s++)
      if (bn[s] >= 0) tr_note(&T[0], row0 + s, bn[s], bi, bd[s]);
    if (has_drums) render_drums(c, &T[3], row0, drums, nbars * spb, 0);
  }
  for (k = 0; k < nlead; k++)
  {
    tr_note(&T[1], row0 + lead[k].row, lead[k].note, lead_ins, lead[k].dur);
    rl_add(&c->lead_rows, row0 + lead[k].row, lead[k].note, lead[k].dur);
  }
  if (c->voices == 3) freev[nfree++] = &T[2];
  if (!has_kit) freev[nfree++] = &T[3];
  freev[nfree++] = &T[4];
  modes[0] = a->mode2;
  modes[1] = a->mode2b;
  for (k = 0; (k < nfree) && (k < 2); k++)
    comp_render_mode2(c, freev[k], row0, nbars, chords, modes[k], lead, nlead, last, NULL, freev[k] != freev[0]);
  comp_render_mode2(c, &T[5], row0, nbars, chords, a->mode3, lead, nlead, last, NULL, 1);
  if (nfree > 2)
  {
    static const int rest[3] = {MD_BROKEN, MD_COUNTER, MD_PAD};
    int m = MD_PAD;
    for (k = 0; k < 3; k++)
      if ((rest[k] != modes[0]) && (rest[k] != modes[1]) && (rest[k] != a->mode3)) { m = rest[k]; break; }
    comp_render_mode2(c, freev[2], row0, nbars, chords, m, lead, nlead, last, NULL, 1);
  }
  if (c->sids >= 4) comp_render_layers(c, sec, nbars, chords, row0, first, last, lead, nlead, bn, bd, drums);
}

// ---------------------------------------------------------------------------
// The whole piece
// ---------------------------------------------------------------------------

typedef struct
{
  int sec, nbars, row0;
  CHORD chords[64];
} SAMEKEY;

void comp_render(COMP *c)
{
  int si, row = 0, k, seen[SEC_NUM] = {0};
  static SAMEKEY same[SG_MAXSECS];
  int nsame = 0;
  c->ntracks = 3 * c->sids;
  for (k = 0; k < c->ntracks; k++)
  {
    tr_free(&c->tracks[k]);
    tr_init(&c->tracks[k], c->total_rows + 64);
  }
  c->str_prev = 72;
  c->nshared = 0;
  free(c->chord_rows);
  c->chord_rows = (CHORD *)calloc(c->total_rows + c->spb + 1, sizeof(CHORD));
  c->nchordrows = 0;
  c->lead_rows.n = c->bass_rows.n = c->echo_rows.n = 0;
  free(c->drum_rows);
  c->drum_rows = (int *)calloc(c->total_rows + 64, sizeof(int));
  for (si = 0; si < c->nsecs; si++)
  {
    int sec = c->secs[si], nbars = c->secbars[si], last = si == c->nsecs - 1, first = !seen[sec], b, r, done = 0;
    CHORD chords[64];
    if (nbars > 64) nbars = 64;
    comp_chords_for(c, sec, nbars, sec == SEC_OUTRO, chords);
    seen[sec] = 1;
    for (b = 0; b < nbars; b++)
    {
      CHORD ch = ((last) && (b == nbars - 1)) ? c->tonic : chords[b];
      for (r = 0; r < c->spb; r++) c->chord_rows[c->nchordrows++] = ch;
    }
    // 4-8 SIDs: a section with the same chords is played the same way again
    if ((c->sids >= 4) && (!last) && (!first))
    {
      for (k = 0; k < nsame; k++)
        if ((same[k].sec == sec) && (same[k].nbars == nbars) && (!memcmp(same[k].chords, chords, sizeof(CHORD) * nbars)))
        {
          int t, rows = nbars * c->spb, d = row - same[k].row0, n;
          for (t = 0; t < c->ntracks; t++)
          {
            // as Python: the rows are deleted, then the copy is inserted in its order
            int *srt = (int *)malloc(sizeof(int) * (rows + 1)), ns = 0, a, b2;
            for (r = 0; r < rows; r++)
            {
              c->tracks[t].hascmds[row + r] = c->tracks[t].hascmds[same[k].row0 + r];
              c->tracks[t].cmds[row + r] = c->tracks[t].cmds[same[k].row0 + r];
              if (c->tracks[t].ev[same[k].row0 + r].type != EV_NONE) srt[ns++] = r;
            }
            for (a = 1; a < ns; a++)
            {
              int v = srt[a];
              b2 = a - 1;
              while ((b2 >= 0) && (c->tracks[t].ev[same[k].row0 + srt[b2]].ord > c->tracks[t].ev[same[k].row0 + v].ord)) { srt[b2 + 1] = srt[b2]; b2--; }
              srt[b2 + 1] = v;
            }
            for (r = 0; r < rows; r++)
            {
              c->tracks[t].ev[row + r] = c->tracks[t].ev[same[k].row0 + r];
              c->tracks[t].ev[row + r].type = c->tracks[t].ev[row + r].type;
            }
            for (a = 0; a < ns; a++) c->tracks[t].ev[row + srt[a]].ord = ++sg_evord;
            free(srt);
          }
          for (r = 0; r < rows; r++) c->drum_rows[row + r] = c->drum_rows[same[k].row0 + r];
          n = c->lead_rows.n;
          for (r = 0; r < n; r++)
            if ((c->lead_rows.v[r].row >= same[k].row0) && (c->lead_rows.v[r].row < same[k].row0 + rows))
              rl_add(&c->lead_rows, c->lead_rows.v[r].row + d, c->lead_rows.v[r].note, c->lead_rows.v[r].dur);
          n = c->bass_rows.n;
          for (r = 0; r < n; r++)
            if ((c->bass_rows.v[r].row >= same[k].row0) && (c->bass_rows.v[r].row < same[k].row0 + rows))
              rl_add(&c->bass_rows, c->bass_rows.v[r].row + d, c->bass_rows.v[r].note, c->bass_rows.v[r].dur);
          n = c->echo_rows.n;
          for (r = 0; r < n; r++)
            if ((c->echo_rows.v[r].row >= same[k].row0) && (c->echo_rows.v[r].row < same[k].row0 + rows))
              rl_add(&c->echo_rows, c->echo_rows.v[r].row + d, c->echo_rows.v[r].note, c->echo_rows.v[r].dur);
          for (r = c->lead_rows.n - 1; r >= 0; r--)
            if ((c->lead_rows.v[r].row >= row) && (c->lead_rows.v[r].row < row + rows)) { c->last_lead_note = c->lead_rows.v[r].note; break; }
          done = 1;
          break;
        }
    }
    if (!done)
    {
      if (c->sids >= 2) render6(c, sec, nbars, chords, row, first, last);
      else render3(c, sec, nbars, chords, row, first, last);
      if ((c->sids >= 4) && (!last) && (nsame < SG_MAXSECS))
      {
        same[nsame].sec = sec;
        same[nsame].nbars = nbars;
        same[nsame].row0 = row;
        memcpy(same[nsame].chords, chords, sizeof(CHORD) * nbars);
        nsame++;
      }
    }
    row += nbars * c->spb;
  }
  for (k = 0; k < c->ntracks; k++) tr_finalize(&c->tracks[k], c->total_rows);
}

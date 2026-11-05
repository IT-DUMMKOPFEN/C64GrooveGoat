// C64 GrooveGoat - random songs like SIDGen: dialog and command line

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "goattrk2.h"
#include "gsgcomp.h"
#include "gsgbuild.h"

// style variant from "key" or "key/subindex" (no sub: chosen by weight, as SIDGen)
static int findvariant(const char *spec, unsigned seed)
{
  char key[40];
  const char *sl = strchr(spec, '/');
  int i, first = -1, n = 0, sub = -1;
  snprintf(key, sizeof key, "%.*s", sl ? (int)(sl - spec) : (int)strlen(spec), spec);
  if (sl) sub = atoi(sl + 1);
  for (i = 0; i < sg_numstyles; i++)
    if (!strcmp(sg_styles[i].key, key))
    {
      if (first < 0) first = i;
      n++;
    }
  if (first < 0) return -1;
  if ((sub >= 0) && (sub < n)) return first + sub;
  if (n == 1) return first;
  {
    SGRNG r;
    double w[64];
    for (i = 0; (i < n) && (i < 64); i++) w[i] = sg_styles[first + i].weight;
    rng_seeds(&r, "%u-sub", seed);
    return first + rng_wchoice(&r, w, n < 64 ? n : 64);
  }
}

// compose and build into the song memory; 4-8 SIDs: if the piece does not
// fit into one song, again without the variant sections, then also without
// the register arc (as SIDGen's app)
COMP *sidgen_make(int v, unsigned seed, double seconds, int sids, int voices, int model, char *err, int errsize, int dumpfirst)
{
  int attempt;
  (void)dumpfirst;
  for (attempt = 0; attempt < 3; attempt++)
  {
    COMP *c = comp_new(v, seed, seconds, sids, voices, model);
    int r;
    if (attempt >= 1) c->variants = 0;
    if (attempt >= 2) c->compact = 1;
    if (!comp_compose(c))
    {
      snprintf(err, errsize, "%s", c->err);
      comp_free(c);
      return NULL;
    }
    r = sg_buildsong(c, 1, err, errsize);
    if (r > 0) return c;
    comp_free(c);
    if ((r == 0) || (sids < 4)) return NULL;
  }
  return NULL;
}

int sidgen_compose_cli(const char *spec, char *err, int errsize)
{
  char st[64] = "techno";
  unsigned seed = 1;
  double seconds = 90;
  int sids = 1, model = 6581, v;
  COMP *c;
  const char *p = strchr(spec, ',');
  snprintf(st, sizeof st, "%.*s", p ? (int)(p - spec) : (int)strlen(spec), spec);
  if (p) sscanf(p + 1, "%u,%lf,%d,%d", &seed, &seconds, &sids, &model);
  v = findvariant(st, seed);
  if (v < 0)
  {
    snprintf(err, errsize, "Unknown style %s", st);
    return 0;
  }
  c = sidgen_make(v, seed, seconds, sids, 3, model, err, errsize, 0);
  if (!c)
  {
    if (getenv("GG_SGDUMPSONG"))
    {
      FILE *f = fopen(getenv("GG_SGDUMPSONG"), "w");
      if (f) { fprintf(f, "nofit\n"); fclose(f); }
    }
    return 0;
  }
  if (getenv("GG_SGDUMP"))
  {
    FILE *f = fopen(getenv("GG_SGDUMP"), "w");
    int k, r;
    static const char *kord[SK_NUM] = {"clap", "crash", "hat", "kick", "ohat", "perc", "ride", "snare", "tom"};
    static const int kidx[SK_NUM] = {SK_CLAP, SK_CRASH, SK_HAT, SK_KICK, SK_OHAT, SK_PERC, SK_RIDE, SK_SNARE, SK_TOM};
    static const int bord[SEC_NUM] = {SEC_A, SEC_A2, SEC_B, SEC_B2, SEC_C, SEC_C2, SEC_INTRO, SEC_OUTRO};
    if (f)
    {
      fprintf(f, "title %s\nsub %s\nkey %d %s\ntempo %d %d\nlayout %s\nsections", c->title, sg_styles[v].sub, c->key,
              sg_modename[c->mode], c->tempo[0], c->tempo[1], c->layout);
      for (k = 0; k < c->nsecs; k++) fprintf(f, "%s%s:%d", k ? " " : " ", sg_secname[c->secs[k]], c->secbars[k]);
      fprintf(f, "\nins %s | %s | %s | %s | %s | %s | %s | %s | %s\nkit", c->i_bass, c->i_kickbass, c->i_lead, c->i_lead_b,
              c->t_arp, c->t_stab, c->t_pad, c->i_counter, c->i_broken);
      {
        int first = 1;
        for (k = 0; k < SK_NUM; k++)
          if (c->kit[kidx[k]][0]) { fprintf(f, "%s%s=%s", first ? " " : " | ", kord[k], c->kit[kidx[k]]); first = 0; }
      }
      fprintf(f, "\nfilter %s %s\nbass", c->filter_role[0] ? c->filter_role : "None", c->filter_tpl[0] ? c->filter_tpl : "None");
      for (k = 0; k < SEC_NUM; k++)
        if ((bord[k] < SEC_A2) || (c->islong) || (c->sids >= 2)) fprintf(f, " %s=%s", sg_secname[bord[k]], c->bass_pat[bord[k]]);
      fprintf(f, "\n");
      for (k = 0; k < SEC_NUM; k++)
      {
        int j;
        if ((k >= SEC_A2) && (!c->islong)) continue;
        fprintf(f, "prog %s %d", sg_secname[k], c->chord_bars[k]);
        for (j = 0; j < c->nprog[k]; j++) fprintf(f, " %d%s", c->progs[k][j].root, sg_qname[c->progs[k][j].q]);
        fprintf(f, "\n");
      }
      for (k = 0; k < c->ntracks; k++)
        for (r = 0; r < c->tracks[k].rows; r++)
        {
          SGEV *e = &c->tracks[k].ev[r];
          if (e->type == EV_NOTE)
          {
            if (e->dur >= 0) fprintf(f, "t%d %d n %d %s %d\n", k, r, e->note, c->ins[e->ins].name, e->dur);
            else fprintf(f, "t%d %d n %d %s -\n", k, r, e->note, c->ins[e->ins].name);
          }
          else if (e->type == EV_OFF) fprintf(f, "t%d %d off\n", k, r);
        }
      fclose(f);
    }
  }
  if (getenv("GG_SGDUMPSONG"))
  {
    FILE *f = fopen(getenv("GG_SGDUMPSONG"), "w");
    int k, r, t;
    if (f)
    {
      for (k = 1; k < MAX_INSTR; k++)
      {
        INSTR *i = &instr[k];
        if ((!i->ad) && (!i->sr) && (!i->ptr[0]) && (!i->name[0])) break;
        fprintf(f, "ins %d %02x %02x %d %d %d %d %d %02x %02x %s\n", k, i->ad, i->sr, i->ptr[0], i->ptr[1], i->ptr[2], i->ptr[3],
                i->vibdelay, i->gatetimer, i->firstwave, i->name);
      }
      for (t = 0; t < 4; t++)
      {
        static const char *tn[4] = {"wave", "pulse", "filt", "speed"};
        int len = gettablelen(t);
        fprintf(f, "tab %s ", tn[t]);
        for (r = 0; r < len; r++) fprintf(f, "%s%02x:%02x", r ? " " : "", ltable[t][r], rtable[t][r]);
        fprintf(f, "\n");
      }
      for (k = 0; k < numchannels; k++)
      {
        fprintf(f, "order %d", k);
        for (r = 0; r < songlen[0][k]; r++) fprintf(f, " %d", songorder[0][k][r]);
        fprintf(f, "\n");
      }
      for (k = 0; k <= highestusedpattern; k++)
      {
        fprintf(f, "patt %d", k);
        for (r = 0; r < pattlen[k]; r++)
          fprintf(f, " %02x%02x%x%02x", pattern[k][r * 4], pattern[k][r * 4 + 1], pattern[k][r * 4 + 2], pattern[k][r * 4 + 3]);
        fprintf(f, "\n");
      }
      fclose(f);
    }
  }
  fprintf(stderr, "%s - %s%s%s, %s %s, %.0f BPM, %d bars, %.0f s, layout %s\n", c->title, sg_styles[v].name,
          sg_styles[v].sub[0] ? " / " : "", sg_styles[v].sub, sg_keyname[c->key], sg_modelabel[c->mode], c->bpm, c->total_bars,
          c->seconds, c->layout);
  comp_free(c);
  return 1;
}

// C64 GrooveGoat - random songs: dialog and command line

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "goattrk2.h"
#include "gsgcomp.h"
#include "gsgbuild.h"
#include "gsgmidi.h"
#include "gsgplan.h"
#include "gollama.h"

// style variant from "key" or "key/subindex" (no sub: chosen by weight)
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

// 1.1: the song plan the next piece is composed from (NULL = the composer plans)
static const SONGPLAN *activeplan = NULL;
char plan_note[96];          // what was done with the plan (shown after composing)

static void applyplan(COMP *c)
{
  const SONGPLAN *p = activeplan;
  if ((!p) || (!c->engine) || (!p->nsec)) return;
  c->plan = p;
  if (p->key >= 0) c->want_key = p->key;
  if (p->mode >= 0) c->want_mode = p->mode;
  if (p->bpm > 0) c->want_bpm = p->bpm;
}

// compose and build into the song memory; 4-8 SIDs: if the piece does not
// fit into one song, again without the variant sections, then also without
// the register arc
static COMP *subcomp(int v, unsigned seed, double seconds, int sids, int voices, int model, int variants, int compact,
                     COMP *fixed, int jingle)
{
  COMP *c = comp_new(v, seed, seconds, sids, voices, model);
  if ((!fixed) && (!jingle)) applyplan(c);
  c->variants = variants;
  c->compact = compact;
  c->fixed = fixed;
  if (fixed) snprintf(c->title, sizeof c->title, "%s", fixed->title);
  if (jingle)
  {
    c->jingle = 1;
    c->target = 60;
  }
  if (!comp_compose(c))
  {
    comp_free(c);
    return NULL;
  }
  return c;
}

// compose and build into the song memory; 4-8 SIDs: if the piece does not
// fit into one song, again without the variant sections, then also without
// the register arc. subtunes: title music, in-game music
// and a game over jingle with the same instruments.
COMP *song_make(int v, unsigned seed, double seconds, int sids, int voices, int model, char *err, int errsize, int subtunes,
                  int enhanced)
{
  int attempt;
  plan_note[0] = 0;
  if ((enhanced) && (sids <= 2) && (!subtunes))
  {
    // enhanced SID calculation: composed for 8 SIDs, calculated down like a big MIDI file
    COMP *big = comp_new(v, seed, seconds, 8, 3, model), *c;
    int r;
    applyplan(big);
    if (!comp_compose(big))
    {
      snprintf(err, errsize, "%s", big->err[0] ? big->err : "The style data could not be read");
      comp_free(big);
      return NULL;
    }
    c = sg_enhance(big, sids, voices, err, errsize);
    comp_free(big);
    if (!c) return NULL;
    r = sg_buildsongs(&c, 1, 1, err, errsize);
    if (r > 0) return c;
    comp_free(c);
    return NULL;
  }
  for (attempt = 0; attempt < 3; attempt++)
  {
    COMP *subs[3] = {NULL, NULL, NULL};
    int r, n = 1, k, variants = attempt == 0, compact = attempt >= 2;
    subs[0] = subcomp(v, seed, seconds, sids, voices, model, variants, compact, NULL, 0);
    if (!subs[0])
    {
      snprintf(err, errsize, "The style data could not be read");
      return NULL;
    }
    if (subtunes)
    {
      subs[1] = subcomp(v, subs[0]->seed + 1, seconds > 0 ? seconds : subs[0]->target, sids, voices, model, variants, compact, subs[0], 0);
      subs[2] = subcomp(v, subs[0]->seed + 2, 0, sids, voices, model, variants, compact, subs[0], 1);
      n = 3;
    }
    r = sg_buildsongs(subs, n, 1, err, errsize);
    for (k = 1; k < n; k++) comp_free(subs[k]);
    if (r > 0) return subs[0];
    comp_free(subs[0]);
    if ((r == 0) || (sids < 4)) return NULL;
  }
  // 4-8 SIDs with a song plan: the plan is too big for one song - shorten it
  // (long parts first, then parts in the middle) until the piece fits
  if ((activeplan) && (sg_engine))
  {
    static SONGPLAN small;
    const SONGPLAN *keep = activeplan;
    int step;
    small = *keep;
    activeplan = &small;
    for (step = 0; step < 24; step++)
    {
      COMP *subs[3] = {NULL, NULL, NULL};
      int r, n = 1, k, big = -1;
      for (k = 0; k < small.nsec; k++)
        if ((small.sec[k].bars > 8) && ((big < 0) || (small.sec[k].bars > small.sec[big].bars))) big = k;
      if (big >= 0) small.sec[big].bars = (small.sec[big].bars / 2 + 3) & ~3;
      else if (small.nsec > 3)
      {
        k = small.nsec - 2;
        memmove(&small.sec[k], &small.sec[k + 1], sizeof(PLANSEC));
        small.nsec--;
      }
      else break;
      subs[0] = subcomp(v, seed, seconds, sids, voices, model, 0, 1, NULL, 0);
      if (!subs[0]) break;
      if (subtunes)
      {
        subs[1] = subcomp(v, subs[0]->seed + 1, seconds > 0 ? seconds : subs[0]->target, sids, voices, model, 0, 1, subs[0], 0);
        subs[2] = subcomp(v, subs[0]->seed + 2, 0, sids, voices, model, 0, 1, subs[0], 1);
        n = 3;
      }
      r = sg_buildsongs(subs, n, 1, err, errsize);
      for (k = 1; k < n; k++) comp_free(subs[k]);
      if (r > 0)
      {
        snprintf(plan_note, sizeof plan_note, "The plan was shortened to %d bars to fit %d SIDs", plan_bars(&small), sids);
        activeplan = keep;
        return subs[0];
      }
      comp_free(subs[0]);
      if (r == 0) break;
    }
    activeplan = keep;
  }
  return NULL;
}

// the built song (test dump)
static void dumpsong(const char *path)
{
    FILE *f = fopen(path, "w");
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
      {
        int s2;
        for (s2 = 0; s2 < 3; s2++)
        {
          if (!songlen[s2][0]) break;
          for (k = 0; k < numchannels; k++)
          {
            int rs = songorder[s2][k][songlen[s2][k] + 1];
            if (s2) fprintf(f, "order %d.%d", s2, k);
            else fprintf(f, "order %d", k);
            for (r = 0; r < songlen[s2][k]; r++) fprintf(f, " %d", songorder[s2][k][r]);
            if (rs) fprintf(f, " / %d", rs);
            fprintf(f, "\n");
          }
        }
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

int compose_cli(const char *spec, char *err, int errsize)
{
  char st[64] = "techno";
  unsigned seed = 1;
  double seconds = 90;
  int sids = 1, model = 6581, v;
  COMP *c;
  const char *p = strchr(spec, ',');
  snprintf(st, sizeof st, "%.*s", p ? (int)(p - spec) : (int)strlen(spec), spec);
  int subtunes = 0, voices = 3, enhanced = 0;
  if (p) sscanf(p + 1, "%u,%lf,%d,%d,%d,%d,%d", &seed, &seconds, &sids, &model, &subtunes, &voices, &enhanced);
  if ((getenv("GG_COMPOSER")) && (!strcmp(getenv("GG_COMPOSER"), "classic"))) sg_engine = 0;
  // GG_PLAN=FILE: compose from a song plan (JSON); its genre wins over STYLE
  if (getenv("GG_PLAN"))
  {
    static SONGPLAN plan;
    char msg[200];
    char *txt = plan_readfile(getenv("GG_PLAN"));
    plan_clear(&plan);
    if ((!txt) || (!plan_parse(txt, &plan, msg, sizeof msg)))
    {
      snprintf(err, errsize, "Plan %s: %s", getenv("GG_PLAN"), txt ? msg : "cannot read the file");
      free(txt);
      return 0;
    }
    free(txt);
    fprintf(stderr, "%s\n", msg);
    if ((plan.genre[0]) && (findvariant(plan.genre, seed) >= 0)) snprintf(st, sizeof st, "%s", plan.genre);
    activeplan = &plan;
    if (getenv("GG_PLANOUT"))
    {
      static char out[16384];
      FILE *f = fopen(getenv("GG_PLANOUT"), "w");
      plan_to_json(&plan, out, sizeof out);
      if (f) { fputs(out, f); fclose(f); }
    }
  }
  v = findvariant(st, seed);
  if (v < 0)
  {
    snprintf(err, errsize, "Unknown style %s", st);
    return 0;
  }
  c = song_make(v, seed, seconds, sids, voices, model, err, errsize, subtunes, enhanced);
  if (!c)
  {
    if (getenv("GG_SGDUMPSONG"))
    {
      FILE *f = fopen(getenv("GG_SGDUMPSONG"), "w");
      if (f) { fprintf(f, "nofit\n"); fclose(f); }
    }
    return 0;
  }
  if (plan_note[0]) fprintf(stderr, "%s\n", plan_note);
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
  if (getenv("GG_SGDUMPSONG")) dumpsong(getenv("GG_SGDUMPSONG"));
  fprintf(stderr, "%s - %s%s%s, %s %s, %.0f BPM, %d bars, %.0f s, layout %s\n", c->title, sg_styles[v].name,
          sg_styles[v].sub[0] ? " / " : "", sg_styles[v].sub, sg_keyname[c->key], sg_modelabel[c->mode], c->bpm, c->total_bars,
          c->seconds, c->layout);
  comp_free(c);
  return 1;
}


// command line: --midi=FILE,SIDS[,VOICES,MODEL,VARIANT,ACCOMP,HARMONY]
// (4-8 SIDs: fewer SIDs when the piece does not fit into one song)
int midi_cli(const char *spec, char *err, int errsize)
{
  static const char *accn[6] = {"auto", "arp", "stabs", "pads", "broken", "counter"};
  static const char *harmn[3] = {"normal", "careful", "loose"};
  char file[1024], acc[16] = "auto", harm[16] = "normal";
  const char *p = strchr(spec, ',');
  int sids = 1, voices = 3, model = 6581, variant = 0, k, len, r = 0, s2, tries[4], ntries = 0;
  unsigned char *data;
  FILE *f;
  MIDIOPTS o;
  snprintf(file, sizeof file, "%.*s", p ? (int)(p - spec) : (int)strlen(spec), spec);
  if (p) sscanf(p + 1, "%d,%d,%d,%d,%15[^,],%15s", &sids, &voices, &model, &variant, acc, harm);
  f = fopen(file, "rb");
  if (!f)
  {
    snprintf(err, errsize, "Cannot open %s", file);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);
  data = (unsigned char *)malloc(len + 1);
  len = fread(data, 1, len, f);
  fclose(f);
  midiopts_default(&o);
  o.sids = sids;
  o.voices = voices;
  o.model = model;
  o.variant = variant;
  {
    const char *b = strrchr(file, '/'), *b2 = strrchr(file, '\\'), *dot;
    if ((b2) && ((!b) || (b2 > b))) b = b2;
    b = b ? b + 1 : file;
    dot = strrchr(b, '.');
    snprintf(o.basename, sizeof o.basename, "%.*s", dot && dot != b ? (int)(dot - b) : (int)strlen(b), b);
  }
  for (k = 0; k < 6; k++)
    if (!strcmp(acc, accn[k])) o.accomp = k;
  for (k = 0; k < 3; k++)
    if (!strcmp(harm, harmn[k])) o.harmony = k;
  tries[ntries++] = sids;
  if (sids > 2)
    for (k = 0; k < 3; k++)
    {
      static const int lower[3] = {4, 2, 1};
      if (lower[k] < sids) tries[ntries++] = lower[k];
    }
  for (s2 = 0; s2 < ntries; s2++)
  {
    MIDIINFO info;
    COMP *c;
    o.sids = tries[s2];
    c = sg_import_midi(data, len, &o, &info, err, errsize);
    if (!c) break;
    if (getenv("GG_SGDUMP"))
    {
      FILE *d = fopen(getenv("GG_SGDUMP"), "w");
      int t, row;
      for (t = 0; (d) && (t < c->ntracks); t++)
        for (row = 0; row < c->tracks[t].rows; row++)
        {
          SGEV *e = &c->tracks[t].ev[row];
          if (e->type == EV_NOTE)
          {
            fprintf(d, "t%d %d n %d %s ", t, row, e->note, c->ins[e->ins].name);
            if (e->dur >= 0) fprintf(d, "%d", e->dur);
            else fprintf(d, "-");
            if (c->tracks[t].haslv[row]) fprintf(d, " lv%.4f", c->tracks[t].lv[row]);
            if (e->hascmd) fprintf(d, " c%x:%02x", e->cmd.cmd, e->cmd.a);
            fprintf(d, "\n");
          }
          else if (e->type == EV_OFF) fprintf(d, "t%d %d off\n", t, row);
        }
      if (d) fclose(d);
    }
    r = sg_buildsongs(&c, 1, 1, err, errsize);
    if (r > 0)
    {
      fprintf(stderr, "%s - MIDI import, %d parts, %d bars, %d SIDs\n", c->title, info.nparts, info.nbars, o.sids);
      comp_free(c);
      break;
    }
    comp_free(c);
    if (r == 0) break;
  }
  free(data);
  if (r > 0)
  {
    if (getenv("GG_SGDUMPSONG")) dumpsong(getenv("GG_SGDUMPSONG"));
    return 1;
  }
  if (r < 0) snprintf(err, errsize, "The piece does not fit into one song");
  return 0;
}

// ---------------------------------------------------------------------------
// Dialog: New song (CTRL+G)
// ---------------------------------------------------------------------------

#include "gres.h"
#include "gsgexpr.h"

extern char loadedsongfilename[MAX_FILENAME];
extern char songfilename[MAX_FILENAME];

typedef struct
{
  INSTR instr[MAX_INSTR];
  unsigned char l[MAX_TABLES][MAX_TABLELEN], r[MAX_TABLES][MAX_TABLELEN];
  unsigned char order[MAX_SONGS][MAX_CHN][MAX_SONGLEN + 2];
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int pattlen[MAX_PATT], songlen[MAX_SONGS][MAX_CHN];
  char name[MAX_STR], author[MAX_STR], copyright[MAX_STR];
  char loaded[MAX_FILENAME], file[MAX_FILENAME];
  int numsids, modified;
  unsigned sidaddr[MAX_SIDS];
  EDITOR_INFO ei;
  CHN_EDITOR_INFO chei[MAX_CHN];
  unsigned char mute[MAX_CHN];
} FULLSNAP;

static void fullsave(FULLSNAP *s)
{
  int c;
  memcpy(s->instr, instr, sizeof instr);
  memcpy(s->l, ltable, sizeof ltable);
  memcpy(s->r, rtable, sizeof rtable);
  memcpy(s->order, songorder, sizeof songorder);
  memcpy(s->patt, pattern, sizeof pattern);
  memcpy(s->pattlen, pattlen, sizeof pattlen);
  memcpy(s->songlen, songlen, sizeof songlen);
  memcpy(s->name, songname, sizeof songname);
  memcpy(s->author, authorname, sizeof authorname);
  memcpy(s->copyright, copyrightname, sizeof copyrightname);
  memcpy(s->loaded, loadedsongfilename, sizeof s->loaded);
  memcpy(s->file, songfilename, sizeof s->file);
  memcpy(s->sidaddr, sidaddr, sizeof s->sidaddr);
  s->numsids = numsids;
  s->modified = songmodified;
  s->ei = editorInfo;
  for (c = 0; c < MAX_CHN; c++)
  {
    s->chei[c] = editorUndoInfo.editorInfo[c];
    s->mute[c] = chn[c].mute;
  }
}

static void fullrestore(const FULLSNAP *s)
{
  int c;
  setnumsids(s->numsids);
  memcpy(instr, s->instr, sizeof instr);
  memcpy(ltable, s->l, sizeof ltable);
  memcpy(rtable, s->r, sizeof rtable);
  memcpy(songorder, s->order, sizeof songorder);
  memcpy(pattern, s->patt, sizeof pattern);
  memcpy(pattlen, s->pattlen, sizeof pattlen);
  memcpy(songlen, s->songlen, sizeof songlen);
  memcpy(songname, s->name, sizeof songname);
  memcpy(authorname, s->author, sizeof authorname);
  memcpy(copyrightname, s->copyright, sizeof copyrightname);
  memcpy(loadedsongfilename, s->loaded, sizeof s->loaded);
  memcpy(songfilename, s->file, sizeof s->file);
  memcpy(sidaddr, s->sidaddr, sizeof s->sidaddr);
  songmodified = s->modified;
  editorInfo = s->ei;
  for (c = 0; c < MAX_CHN; c++)
  {
    editorUndoInfo.editorInfo[c] = s->chei[c];
    chn[c].mute = s->mute[c];
  }
  countpatternlengths();
}

static void stopwait(void)
{
  int t;
  stopsong();
  for (t = 0; (t < 100) && (isplaying()); t++) SDL_Delay(5);
}

// settings (kept while the program runs)
static int g_genre = -1, g_sub = -1, g_sids = 1, g_voices = 3, g_chip = -1, g_subtunes = 0, g_key = -1, g_mode = -1, g_bpm = 0;
static int g_seconds = 90, g_enhanced = 0;
static unsigned g_seed = 0;
static int g_melvar = 0, g_insvar = 0;

static int genrevariant(unsigned seed, int *sub_out)
{
  int g = g_genre, first, n;
  if (g < 0)
  {
    SGRNG r;
    rng_seeds(&r, "%u-style", seed);
    g = rng_below(&r, style_numgenres());
  }
  first = style_genre_first(g);
  n = style_genre_count(g);
  if ((g_sub >= 0) && (g_sub < n))
  {
    *sub_out = g_sub;
    return first + g_sub;
  }
  if (n == 1)
  {
    *sub_out = 0;
    return first;
  }
  {
    SGRNG r;
    double w[64];
    int i;
    for (i = 0; (i < n) && (i < 64); i++) w[i] = sg_styles[first + i].weight;
    rng_seeds(&r, "%u-sub", seed);
    i = rng_wchoice(&r, w, n < 64 ? n : 64);
    *sub_out = i;
    return first + i;
  }
}

static COMP *compose_now(char *err, int errsize, int *variant)
{
  int sub, v = genrevariant(g_seed, &sub), attempt;
  int model = g_chip ? 8580 : 6581;
  *variant = v;
  if ((g_enhanced) && (g_sids <= 2) && (!g_subtunes))
  {
    // enhanced SID calculation: composed for 8 SIDs, calculated down like a big MIDI file
    COMP *big = comp_new(v, g_seed, g_seconds, 8, 3, model), *c;
    int r;
    comp_setvariations(big, g_melvar, g_insvar);
    big->want_key = g_key;
    big->want_mode = g_mode;
    big->want_bpm = g_bpm;
    applyplan(big);
    if (!comp_compose(big))
    {
      snprintf(err, errsize, "%s", big->err);
      comp_free(big);
      return NULL;
    }
    c = sg_enhance(big, g_sids, g_voices, err, errsize);
    comp_free(big);
    if (!c) return NULL;
    snd_lock();
    r = sg_buildsongs(&c, 1, 1, err, errsize);
    snd_unlock();
    if (r > 0) return c;
    comp_free(c);
    return NULL;
  }
  for (attempt = 0; attempt < 3; attempt++)
  {
    COMP *subs[3] = {NULL, NULL, NULL};
    int r, n = 1, k, variants = attempt == 0, compact = attempt >= 2;
    for (k = 0; k < (g_subtunes ? 3 : 1); k++)
    {
      COMP *c = comp_new(v, k ? subs[0]->seed + k : g_seed, k == 2 ? 0 : (k ? (g_seconds ? g_seconds : subs[0]->target) : g_seconds),
                         g_sids, g_voices, model);
      comp_setvariations(c, g_melvar, g_insvar);
      c->variants = variants;
      c->compact = compact;
      c->want_key = g_key;
      c->want_mode = g_mode;
      c->want_bpm = g_bpm;
      if (!k) applyplan(c);
      if (k)
      {
        c->fixed = subs[0];
        snprintf(c->title, sizeof c->title, "%s", subs[0]->title);
      }
      if (k == 2)
      {
        c->jingle = 1;
        c->target = 60;
      }
      if (!comp_compose(c))
      {
        snprintf(err, errsize, "%s", c->err);
        comp_free(c);
        for (k = 0; k < 3; k++) comp_free(subs[k]);
        return NULL;
      }
      subs[k] = c;
      n = k + 1;
    }
    snd_lock();
    r = sg_buildsongs(subs, n, 1, err, errsize);
    snd_unlock();
    for (k = 1; k < n; k++) comp_free(subs[k]);
    if (r > 0) return subs[0];
    comp_free(subs[0]);
    if ((r == 0) || (g_sids < 4)) return NULL;
  }
  return NULL;
}



// ---------------------------------------------------------------------------
// The song plan page of "New song" (composer 1.1)
// ---------------------------------------------------------------------------

static SONGPLAN g_plan;
static int g_planon = 0;               // the piece is composed from g_plan
static int g_arranger = 0;             // 0 built-in, 1.. an Ollama model
static char g_models[OLLAMA_MAXMODELS][64];
static int g_nmodels = -2;             // -2 not asked yet, -1 Ollama not reachable

// the genre and subgenre of a style key ("synthwave") for the settings
static int genre_of_key(const char *key, int *sub)
{
  int g, ng = style_numgenres(), k;
  for (g = 0; g < ng; g++)
  {
    int first = style_genre_first(g), n = style_genre_count(g);
    for (k = 0; k < n; k++)
      if (!strcmp(sg_styles[first + k].key, key))
      {
        *sub = -1;
        return g;
      }
  }
  return -1;
}

static void plan_take(SONGPLAN *np)
{
  int sub;
  g_plan = *np;
  g_planon = 1;
  if (g_plan.genre[0])
  {
    int g = genre_of_key(g_plan.genre, &sub);
    if (g >= 0) { g_genre = g; g_sub = -1; }
  }
}

static int planpage(COMP **cp, int *variant, int *ok, int *previewing, char *err, int errsize, int x0, int y0, int w, int h)
{
  enum { P_IDEA = 0, P_ARR, P_TITLE, P_KEY, P_TEMPO, P_MOOD, P_SECS };
  int sel = P_IDEA, top = 0, editch = 0, result = -1, dirty = 0, asking = 0, k;
  int ofs_idea = 0, ofs_title = 0, ofs_mood = 0, ofs_ch = 0;
  char msg[220] = "", buf[400];
  Uint32 lastchange = 0;
  const int rows = 12;                       // visible parts
  if (g_nmodels == -2)
  {
    char e[200];
    g_nmodels = ollama_models(g_models, OLLAMA_MAXMODELS, e, sizeof e);
    if ((g_nmodels > 0) && (!g_arranger)) g_arranger = 1;
  }
  if (g_arranger > g_nmodels) g_arranger = g_nmodels > 0 ? 1 : 0;
  if ((!g_planon) && (*cp)) plan_from_comp(*cp, &g_plan);
  key = rawkey = 0;
  while (result < 0)
  {
    int y, hit = -1, hitbtn = -1, nsec = g_plan.nsec, bpm, secs;
    const int ytab = y0 + 2, yidea = y0 + 4, yarr = y0 + 6, ybtn = y0 + 8, yinfo = y0 + 10, yhdr = y0 + 14, ygraph = y0 + 15 + rows + 1;
    static const char *btns[7] = {" Ask F5 ", " Copy prompt F6 ", " Paste plan F7 ", " Built-in plan F8 ", " Load F9 ", " Save F10 ", NULL};
    int bx[7];
    // a changed plan: compose again (after a short pause, so typing stays smooth)
    if ((dirty) && (!asking) && ((SDL_GetTicks() - lastchange > 700) || (rawkey == KEY_F1)))
    {
      int was = *previewing;
      if (was) stopwait();
      activeplan = g_planon ? &g_plan : NULL;
      if (*cp) comp_free(*cp);
      *cp = compose_now(err, errsize, variant);
      *ok = *cp != NULL;
      if ((was) && (*ok)) initsong(0, PLAY_BEGINNING);
      else *previewing = 0;
      dirty = 0;
    }
    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "New song - Song plan");
    // tabs
    printtext(x0 + 2, ytab, MKCOL(C_BUTTONTEXT, C_BUTTON), " 1 Settings ");
    printtext(x0 + 16, ytab, MKCOL(C_BUTTONTEXT, C_BUTTONHI), " 2 Song plan ");
    if ((mousey == ytab) && (mousex >= x0 + 2) && (mousex < x0 + 14)) hitbtn = 100;
    printtext(x0 + 32, ytab, MKCOL(8, C_PANEL), g_planon ? "The song is composed from this plan (TAB: settings)" :
              "The plan of the composer - change something, ask Ollama or paste a plan");
    // idea
    printtext(x0 + 2, yidea, MKCOL(sel == P_IDEA ? 15 : 8, C_PANEL), "Idea");
    printblankc(x0 + 12, yidea, MKCOL(15, C_BEATROW), w - 15);
    drawtextfield(x0 + 12, yidea, w - 15, g_plan.idea, MKCOL(15, C_BEATROW), (sel == P_IDEA) && (!asking), &ofs_idea);
    if ((mousey == yidea) && (mousex >= x0 + 12) && (mousex < x0 + w - 3)) hit = P_IDEA;
    // arranger
    {
      const char *st;
      int col;
      if (g_arranger == 0) snprintf(buf, sizeof buf, GS_PREV " Built-in " GS_NEXT);
      else snprintf(buf, sizeof buf, GS_PREV " Ollama: %s " GS_NEXT, g_models[g_arranger - 1]);
      printblankc(x0 + 2, yarr, MKCOL(8, sel == P_ARR ? C_SELECT : C_PANEL), 44);
      printtext(x0 + 2, yarr, MKCOL(sel == P_ARR ? 15 : 8, sel == P_ARR ? C_SELECT : C_PANEL), "Arranger");
      printtext(x0 + 12, yarr, MKCOL(15, sel == P_ARR ? C_SELECT : C_PANEL), buf);
      if ((mousey == yarr) && (mousex >= x0 + 2) && (mousex < x0 + 46)) hit = P_ARR;
      if (g_nmodels > 0) { st = "running on this computer, works offline"; col = 13; }
      else if (g_nmodels == 0) { st = "Ollama runs, but has no model: ollama pull llama3.2"; col = 10; }
      else { st = "Ollama not found - free from ollama.com (optional; R: look again)"; col = 3; }
      printtext(x0 + 48, yarr, MKCOL(col, C_PANEL), st);
    }
    // buttons
    {
      int xx = x0 + 12;
      for (k = 0; btns[k]; k++)
      {
        int dis = (k == 0) && (g_arranger == 0);
        bx[k] = xx;
        printtext(xx, ybtn, MKCOL(dis ? 3 : C_BUTTONTEXT, k == 0 && !dis ? C_BUTTONHI : C_BUTTON), btns[k]);
        if ((mousey == ybtn) && (mousex >= xx) && (mousex < xx + (int)strlen(btns[k]))) hitbtn = k;
        xx += strlen(btns[k]) + 1;
      }
    }
    // title, key, tempo, mood, theme
    bpm = g_plan.bpm > 0 ? g_plan.bpm : (*cp ? (int)((*cp)->bpm + 0.5) : 120);
    {
      int key0 = g_plan.key >= 0 ? g_plan.key : (*cp ? (*cp)->key : 0), mode0 = g_plan.mode >= 0 ? g_plan.mode : (*cp ? (*cp)->mode : 0);
      printtext(x0 + 2, yinfo, MKCOL(sel == P_TITLE ? 15 : 8, C_PANEL), "Title");
      printblankc(x0 + 9, yinfo, MKCOL(14, sel == P_TITLE ? C_BEATROW : C_PANEL), 24);
      drawtextfield(x0 + 9, yinfo, 24, g_plan.title, MKCOL(14, sel == P_TITLE ? C_BEATROW : C_PANEL), (sel == P_TITLE) && (!asking), &ofs_title);
      if ((mousey == yinfo) && (mousex >= x0 + 2) && (mousex < x0 + 33)) hit = P_TITLE;
      snprintf(buf, sizeof buf, "Key " GS_PREV " %s %s " GS_NEXT, sg_keyname[key0], sg_modelabel[mode0]);
      printtext(x0 + 36, yinfo, MKCOL(sel == P_KEY ? 15 : 8, sel == P_KEY ? C_SELECT : C_PANEL), buf);
      if ((mousey == yinfo) && (mousex >= x0 + 36) && (mousex < x0 + 62)) hit = P_KEY;
      snprintf(buf, sizeof buf, "Tempo " GS_PREV " %d BPM " GS_NEXT, bpm);
      printtext(x0 + 64, yinfo, MKCOL(sel == P_TEMPO ? 15 : 8, sel == P_TEMPO ? C_SELECT : C_PANEL), buf);
      if ((mousey == yinfo) && (mousex >= x0 + 64) && (mousex < x0 + 84)) hit = P_TEMPO;
      printtext(x0 + 2, yinfo + 1, MKCOL(sel == P_MOOD ? 15 : 8, C_PANEL), "Mood");
      printblankc(x0 + 9, yinfo + 1, MKCOL(15, sel == P_MOOD ? C_BEATROW : C_PANEL), 40);
      drawtextfield(x0 + 9, yinfo + 1, 40, g_plan.mood, MKCOL(15, sel == P_MOOD ? C_BEATROW : C_PANEL), (sel == P_MOOD) && (!asking), &ofs_mood);
      if ((mousey == yinfo + 1) && (mousex >= x0 + 2) && (mousex < x0 + 49)) hit = P_MOOD;
      {
        int o = snprintf(buf, sizeof buf, "Theme ");
        if (g_plan.ntheme)
        {
          for (k = 0; (k < g_plan.ntheme) && (o < 60); k++) o += snprintf(buf + o, sizeof buf - o, "%d ", g_plan.theme[k]);
          snprintf(buf + o, sizeof buf - o, " %s", g_plan.rhythm);
        }
        else snprintf(buf + o, sizeof buf - o, "from the composer");
        printtext(x0 + 52, yinfo + 1, MKCOL(13, C_PANEL), buf);
      }
    }
    // the parts
    printblankc(x0 + 2, yhdr - 1, MKCOL(8, C_PANEL), w - 4);
    printblankc(x0 + 2, yhdr, MKCOL(8, C_HEADER), w - 4);
    printtext(x0 + 3, yhdr, MKCOL(C_HEADERTEXT, C_HEADER), "#   Part          Bars     Energy            Chords (one per bar, . holds)            Note");
    if (sel >= P_SECS)
    {
      if (sel - P_SECS < top) top = sel - P_SECS;
      if (sel - P_SECS >= top + rows) top = sel - P_SECS - rows + 1;
    }
    if (top > nsec - rows) top = nsec - rows > 0 ? nsec - rows : 0;
    for (k = 0; k < rows; k++)
    {
      int i = top + k, ry = yhdr + 1 + k, isel = sel == P_SECS + i, bg = isel ? C_SELECT : C_PANEL, n, col;
      PLANSEC *ps;
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      if (i >= nsec) continue;
      ps = &g_plan.sec[i];
      snprintf(buf, sizeof buf, "%-3d " GS_PREV " %-8s " GS_NEXT " " GS_PREV " %2d " GS_NEXT, i + 1, plan_partname[ps->part], ps->bars);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      n = (int)(ps->energy * 10 + 0.5);
      col = ps->energy >= 0.85 ? 10 : ps->energy >= 0.6 ? 7 : ps->energy >= 0.4 ? 13 : 14;
      printblankc(x0 + 30, ry, MKCOL(15, col), n);
      printblankc(x0 + 30 + n, ry, MKCOL(15, C_BEATROW), 10 - n);
      snprintf(buf, sizeof buf, "%.2f", ps->energy);
      printtext(x0 + 41, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((isel) && (editch))
      {
        printblankc(x0 + 47, ry, MKCOL(15, C_BEATROW), 44);
        drawtextfield(x0 + 47, ry, 44, ps->chords, MKCOL(15, C_BEATROW), 1, &ofs_ch);
      }
      else
      {
        snprintf(buf, sizeof buf, "%-44.44s", ps->chords[0] ? ps->chords : "(of the style)");
        printtext(x0 + 47, ry, MKCOL(ps->chords[0] ? 15 : 3, bg), buf);
      }
      snprintf(buf, sizeof buf, "%-.*s", w - 98 > 0 ? w - 98 : 0, ps->idea);
      printtext(x0 + 93, ry, MKCOL(3, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hit = P_SECS + i;
    }
    // the energy over the song
    {
      int tot = plan_bars(&g_plan), gx = x0 + 2, gw = w - 4, col, row;
      printtext(x0 + 2, ygraph, MKCOL(8, C_PANEL), "Energy over the song");
      for (row = 0; row < 3; row++) printblankc(gx, ygraph + 1 + row, MKCOL(15, C_BEATROW), gw);
      if (tot > 0)
        for (col = 0; col < gw; col++)
        {
          int bar = col * tot / gw, acc = 0, i = 0, lvl;
          double e = 0;
          for (i = 0; i < nsec; i++)
          {
            if (bar < acc + g_plan.sec[i].bars) { e = g_plan.sec[i].energy; break; }
            acc += g_plan.sec[i].bars;
          }
          lvl = (int)(e * 6 + 0.5);                 // 0..6 half rows
          for (row = 0; row < 3; row++)
          {
            int from = (2 - row) * 2;               // half rows below this text row
            int c2 = e >= 0.85 ? 10 : e >= 0.6 ? 7 : e >= 0.4 ? 13 : 14;
            if (lvl >= from + 2) printtext(gx + col, ygraph + 1 + row, MKCOL(c2, C_BEATROW), GS_FULL);
            else if (lvl == from + 1) printtext(gx + col, ygraph + 1 + row, MKCOL(c2, C_BEATROW), GS_LOWER);
          }
          if ((i < nsec) && (bar == acc) && ((col == 0) || ((col - 1) * tot / gw < acc)))
          {
            snprintf(buf, sizeof buf, "%.6s", plan_partname[g_plan.sec[i].part]);
            printtext(gx + col, ygraph + 1, MKCOL(15, C_BEATROW), buf);
          }
        }
    }
    secs = (int)(plan_bars(&g_plan) * 4 * 60.0 / (bpm > 0 ? bpm : 120) + 0.5);
    snprintf(buf, sizeof buf, "%d parts, %d bars, %d:%02d min at %d BPM", nsec, plan_bars(&g_plan), secs / 60, secs % 60, bpm);
    printtext(x0 + 2, y0 + h - 6, MKCOL(8, C_PANEL), buf);
    if (asking)
    {
      snprintf(buf, sizeof buf, "Ollama (%s) is planning the song ... %u s   (ESC cancels)", g_models[g_arranger - 1], ollama_seconds());
      printtext(x0 + 2, y0 + h - 5, MKCOL(7, C_PANEL), buf);
    }
    else if (msg[0]) printtext(x0 + 2, y0 + h - 5, MKCOL(10, C_PANEL), msg);
    else if (!*ok) dlg_printwrapped(x0 + 2, y0 + h - 5, w - 4, 1, MKCOL(10, C_PANEL), err);
    printtext(x0 + 2, y0 + h - 4, MKCOL(3, C_PANEL), editch ? "Chords: one per bar (Am F C G7), '.' holds - ENTER done" :
              GS_UP GS_DOWN " part  " GS_PREV GS_NEXT " energy  +/- bars  PGUP/PGDN part type  ENTER chords  INS add  DEL remove  CTRL+" GS_UP GS_DOWN " move");
    {
      const char *bt[4] = {*previewing ? " " GS_STOP " Stop F1 " : " " GS_PLAY " Play F1 ", "  Create song  ", " Back (TAB) ", "  Cancel  "};
      int xx = x0 + 2;
      for (k = 0; k < 4; k++)
      {
        int bxx = k == 3 ? x0 + w - 12 : xx;
        printtext(bxx, y0 + h - 2, MKCOL(C_BUTTONTEXT, k == 1 ? C_BUTTONHI : C_BUTTON), bt[k]);
        if ((mousey == y0 + h - 2) && (mousex >= bxx) && (mousex < bxx + (int)strlen(bt[k]))) hitbtn = 50 + k;
        xx += strlen(bt[k]) + 2;
      }
    }
    fliptoscreen();
    getkey();
    if (win_quitted) { result = 2; break; }
    // Ollama: the answer
    if (asking)
    {
      char *ans = NULL, e[200] = "";
      int st = ollama_poll(&ans, e, sizeof e);
      if (st == 1)
      {
        SONGPLAN np = g_plan;
        char m2[200];
        asking = 0;
        if (plan_parse(ans, &np, m2, sizeof m2))
        {
          plan_take(&np);
          snprintf(msg, sizeof msg, "Ollama: %s", m2);
          dirty = 1;
          lastchange = 0;
        }
        else snprintf(msg, sizeof msg, "Ollama: %s - the plan stays as it was", m2);
        free(ans);
      }
      else if (st == -1)
      {
        asking = 0;
        snprintf(msg, sizeof msg, "%s", e);
      }
      if (asking)
      {
        if (rawkey == KEY_ESC)
        {
          ollama_cancel();
          asking = 0;
          snprintf(msg, sizeof msg, "Cancelled");
        }
        key = rawkey = 0;
        continue;
      }
    }
    // the mouse
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      if (hitbtn >= 0)
      {
        static const int bkeys[6] = {KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10};
        editch = 0;
        if (hitbtn == 100) rawkey = KEY_TAB;
        else if (hitbtn < 6) rawkey = bkeys[hitbtn];
        else if (hitbtn == 50) rawkey = KEY_F1;
        else if (hitbtn == 51) { result = 1; break; }
        else if (hitbtn == 52) rawkey = KEY_TAB;
        else if (hitbtn == 53) { result = 2; break; }
        key = 0;
      }
      else if (hit >= 0)
      {
        int ar = ui_arrowat(mousex, mousey);
        if ((sel != hit) || (hit < P_SECS)) editch = 0;
        sel = hit;
        if (hit == P_IDEA) editstring_click(g_plan.idea, mousex - (x0 + 12) + ofs_idea);
        else if (hit == P_TITLE) editstring_click(g_plan.title, mousex - (x0 + 9) + ofs_title);
        else if (hit == P_MOOD) editstring_click(g_plan.mood, mousex - (x0 + 9) + ofs_mood);
        else if ((hit == P_ARR) || (hit == P_KEY) || (hit == P_TEMPO)) rawkey = ar < 0 ? KEY_LEFT : KEY_RIGHT;
        else if (hit >= P_SECS)
        {
          PLANSEC *ps = &g_plan.sec[hit - P_SECS];
          if (mousex < x0 + 18) ps->part = (ps->part + (ar < 0 ? PP_NUM - 1 : 1)) % PP_NUM, dirty = 1;
          else if (mousex < x0 + 28) { ps->bars += ar < 0 ? -2 : 2; if (ps->bars < 2) ps->bars = 2; if (ps->bars > 32) ps->bars = 32; dirty = 1; }
          else if (mousex < x0 + 46) { ps->energy = (mousex - (x0 + 30) + 1) / 10.0; if (ps->energy > 1) ps->energy = 1; if (ps->energy < 0) ps->energy = 0; dirty = 1; }
          else if (mousex < x0 + 92)
          {
            editch = 1;
            editstring_click(ps->chords, mousex - (x0 + 47) + ofs_ch);
          }
          if (dirty) { g_planon = 1; lastchange = SDL_GetTicks(); }
        }
        key = 0;
        if ((hit != P_ARR) && (hit != P_KEY) && (hit != P_TEMPO)) rawkey = 0;
      }
    }
    if (!rawkey && !key) continue;
    msg[0] = 0;
    // keys that work everywhere
    if ((rawkey == KEY_TAB) && (!editch)) { result = 0; break; }
    if (rawkey == KEY_F1)
    {
      if (*previewing) { stopwait(); *previewing = 0; }
      else if (*ok) { initsong(0, PLAY_BEGINNING); *previewing = 1; }
      key = rawkey = 0;
      continue;
    }
    if (rawkey == KEY_F5)
    {
      if (g_arranger == 0) snprintf(msg, sizeof msg, "Choose an Ollama model as the arranger first (or use Copy prompt / Paste plan)");
      else if (!g_plan.idea[0]) snprintf(msg, sizeof msg, "Describe the song in the Idea field first");
      else
      {
        static char prompt[8192];
        plan_prompt(&g_plan, g_genre >= 0 ? sg_styles[style_genre_first(g_genre)].key : "", g_seconds, g_sids, prompt, sizeof prompt);
        if (ollama_start(g_models[g_arranger - 1], prompt)) asking = 1;
        else snprintf(msg, sizeof msg, "Ollama could not be asked");
      }
      key = rawkey = 0;
      continue;
    }
    if (rawkey == KEY_F6)
    {
      static char prompt[8192];
      plan_prompt(&g_plan, g_genre >= 0 ? sg_styles[style_genre_first(g_genre)].key : "", g_seconds, g_sids, prompt, sizeof prompt);
      SDL_SetClipboardText(prompt);
      snprintf(msg, sizeof msg, "The prompt is in the clipboard - paste it into a chatbot, copy its answer, then Paste plan (F7)");
      key = rawkey = 0;
      continue;
    }
    if (rawkey == KEY_F7)
    {
      char *clip = SDL_GetClipboardText();
      SONGPLAN np = g_plan;
      char m2[200];
      if ((clip) && (plan_parse(clip, &np, m2, sizeof m2)))
      {
        plan_take(&np);
        dirty = 1;
        lastchange = 0;
        snprintf(msg, sizeof msg, "Pasted: %s", m2);
      }
      else snprintf(msg, sizeof msg, "No plan in the clipboard%s%s", clip && clip[0] ? ": " : "", clip && clip[0] ? m2 : "");
      if (clip) SDL_free(clip);
      key = rawkey = 0;
      continue;
    }
    if (rawkey == KEY_F8)
    {
      // the plan of the composer (with a new seed after the first press)
      if (!g_planon) { g_seed = (g_seed * 69069u + 12345u) % 999999 + 1; g_melvar = g_insvar = 0; }
      g_planon = 0;
      activeplan = NULL;
      dirty = 0;
      {
        int was = *previewing;
        if (was) stopwait();
        if (*cp) comp_free(*cp);
        *cp = compose_now(err, errsize, variant);
        *ok = *cp != NULL;
        if (*ok) plan_from_comp(*cp, &g_plan);
        if ((was) && (*ok)) initsong(0, PLAY_BEGINNING);
        else *previewing = 0;
      }
      snprintf(msg, sizeof msg, "The plan of the composer (seed %u)", g_seed);
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_F9) || (rawkey == KEY_F10))
    {
      static char name[MAX_FILENAME] = "", path[MAX_PATHNAME] = "";
      char cwd[MAX_PATHNAME], filter[32] = "*.plan";
      int save = rawkey == KEY_F10;
      if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
      if (!path[0]) snprintf(path, sizeof path, "%s", cwd);
      if (save)
      {
        char *q;
        snprintf(name, sizeof name, "%s.plan", g_plan.title[0] ? g_plan.title : "song");
        for (q = name; *q; q++)
          if ((*q == ' ') || (*q == '/') || (*q == '\\') || (*q == ':')) *q = '_';
      }
      if (fileselector(name, path, filter, save ? "SAVE THE SONG PLAN (.plan)" : "LOAD A SONG PLAN (.plan)", save ? 3 : 0))
      {
        char full[MAX_PATHNAME + MAX_FILENAME + 2], dir[MAX_PATHNAME];
        if (!getcwd(dir, sizeof dir)) dir[0] = 0;
        snprintf(path, sizeof path, "%s", dir);
        snprintf(full, sizeof full, "%s/%s", dir, name);
        if (save)
        {
          if ((!strstr(name, ".plan")) && (strlen(full) + 6 < sizeof full)) strcat(full, ".plan");
          snprintf(msg, sizeof msg, plan_writefile(full, &g_plan) ? "Saved %s" : "Could not save %s", name);
        }
        else
        {
          char *txt = plan_readfile(full), m2[200];
          SONGPLAN np = g_plan;
          if ((txt) && (plan_parse(txt, &np, m2, sizeof m2)))
          {
            plan_take(&np);
            dirty = 1;
            lastchange = 0;
            snprintf(msg, sizeof msg, "%s: %s", name, m2);
          }
          else snprintf(msg, sizeof msg, "%s: %s", name, txt ? m2 : "cannot read the file");
          free(txt);
        }
      }
      if (cwd[0]) chdir(cwd);
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_ESC) && (!editch)) { result = 2; break; }
    // text fields
    if ((sel == P_IDEA) || (sel == P_TITLE) || (sel == P_MOOD) || ((sel >= P_SECS) && (editch)))
    {
      if ((rawkey == KEY_UP) || (rawkey == KEY_DOWN) || (rawkey == KEY_ESC) || ((rawkey == KEY_ENTER) && (sel >= P_SECS)))
      {
        editch = 0;
        if (rawkey == KEY_ESC) { key = rawkey = 0; continue; }
        if (rawkey == KEY_ENTER) { key = rawkey = 0; continue; }
      }
      else if (rawkey == KEY_ENTER)
      {
        // ENTER in the idea field: ask the arranger
        if ((sel == P_IDEA) && (g_arranger)) rawkey = KEY_F5;
        key = 0;
        if (rawkey == KEY_F5) continue;
        rawkey = 0;
        continue;
      }
      else
      {
        char *b = sel == P_IDEA ? g_plan.idea : sel == P_TITLE ? g_plan.title : sel == P_MOOD ? g_plan.mood : g_plan.sec[sel - P_SECS].chords;
        int sz = sel == P_IDEA ? (int)sizeof g_plan.idea : sel == P_TITLE ? (int)sizeof g_plan.title : sel == P_MOOD ? (int)sizeof g_plan.mood :
                 (int)sizeof g_plan.sec[0].chords;
        char before[256];
        snprintf(before, sizeof before, "%s", b);
        editstring(b, sz);
        if ((sel != P_IDEA) && (strcmp(before, b))) { g_planon = 1; dirty = 1; lastchange = SDL_GetTicks(); }
        key = rawkey = 0;
        continue;
      }
    }
    switch (rawkey)
    {
      case KEY_UP:
      if ((ctrlpressed) && (sel > P_SECS))
      {
        PLANSEC t = g_plan.sec[sel - P_SECS];
        g_plan.sec[sel - P_SECS] = g_plan.sec[sel - P_SECS - 1];
        g_plan.sec[sel - P_SECS - 1] = t;
        sel--;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      else if (sel > 0) sel--;
      break;
      case KEY_DOWN:
      if ((ctrlpressed) && (sel >= P_SECS) && (sel - P_SECS < nsec - 1))
      {
        PLANSEC t = g_plan.sec[sel - P_SECS];
        g_plan.sec[sel - P_SECS] = g_plan.sec[sel - P_SECS + 1];
        g_plan.sec[sel - P_SECS + 1] = t;
        sel++;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      else if (sel < P_SECS + nsec - 1) sel++;
      break;
      case KEY_LEFT:
      case KEY_RIGHT:
      {
        int d = rawkey == KEY_RIGHT ? 1 : -1;
        if (sel == P_ARR)
        {
          int n = g_nmodels > 0 ? g_nmodels + 1 : 1;
          g_arranger = (g_arranger + d + n) % n;
        }
        else if (sel == P_KEY)
        {
          int k0 = g_plan.key >= 0 ? g_plan.key : (*cp ? (*cp)->key : 0), m0 = g_plan.mode >= 0 ? g_plan.mode : (*cp ? (*cp)->mode : 0);
          if (shiftpressed) m0 = (m0 + d + M_NUM) % M_NUM;
          else k0 = (k0 + d + 12) % 12;
          g_plan.key = k0;
          g_plan.mode = m0;
          g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
        }
        else if (sel == P_TEMPO)
        {
          g_plan.bpm = bpm + d * (shiftpressed ? 10 : 2);
          if (g_plan.bpm < 60) g_plan.bpm = 60;
          if (g_plan.bpm > 200) g_plan.bpm = 200;
          g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
        }
        else if (sel >= P_SECS)
        {
          PLANSEC *ps = &g_plan.sec[sel - P_SECS];
          ps->energy += d * 0.05;
          if (ps->energy < 0) ps->energy = 0;
          if (ps->energy > 1) ps->energy = 1;
          ps->energy = (int)(ps->energy * 100 + 0.5) / 100.0;
          g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
        }
      }
      break;
      case KEY_PGUP:
      case KEY_PGDN:
      if (sel >= P_SECS)
      {
        PLANSEC *ps = &g_plan.sec[sel - P_SECS];
        ps->part = (ps->part + (rawkey == KEY_PGDN ? 1 : PP_NUM - 1)) % PP_NUM;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      break;
      case KEY_ENTER:
      if (sel >= P_SECS) { editch = 1; editstring_click(g_plan.sec[sel - P_SECS].chords, 999); }
      else if (sel == P_ARR) { rawkey = KEY_F5; key = 0; continue; }
      break;
      case KEY_INS:
      if ((sel >= P_SECS) && (nsec < PLAN_MAXSECS))
      {
        int i = sel - P_SECS;
        memmove(&g_plan.sec[i + 1], &g_plan.sec[i], sizeof(PLANSEC) * (nsec - i));
        g_plan.nsec++;
        sel++;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      break;
      case KEY_DEL:
      if ((sel >= P_SECS) && (nsec > 1))
      {
        int i = sel - P_SECS;
        memmove(&g_plan.sec[i], &g_plan.sec[i + 1], sizeof(PLANSEC) * (nsec - i - 1));
        g_plan.nsec--;
        if (sel - P_SECS >= g_plan.nsec) sel--;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      break;
      default:
      if ((sel >= P_SECS) && ((key == '+') || (key == '-')))
      {
        PLANSEC *ps = &g_plan.sec[sel - P_SECS];
        ps->bars += key == '+' ? 2 : -2;
        if (ps->bars < 2) ps->bars = 2;
        if (ps->bars > 32) ps->bars = 32;
        g_planon = 1; dirty = 1; lastchange = SDL_GetTicks();
      }
      else if ((rawkey == KEY_R) && (sel == P_ARR))
      {
        char e[200];
        g_nmodels = ollama_models(g_models, OLLAMA_MAXMODELS, e, sizeof e);
        if (g_arranger > g_nmodels) g_arranger = g_nmodels > 0 ? 1 : 0;
      }
      break;
    }
    key = rawkey = 0;
  }
  if (asking) ollama_cancel();
  // a changed plan that is not composed yet
  if ((result == 1) && (dirty))
  {
    if (*previewing) stopwait();
    *previewing = 0;
    activeplan = g_planon ? &g_plan : NULL;
    if (*cp) comp_free(*cp);
    *cp = compose_now(err, errsize, variant);
    *ok = *cp != NULL;
  }
  key = rawkey = 0;
  return result;
}

void composedialog(void)
{
  static FULLSNAP snap;
  int w = 108, h = 37;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, previewing = 0, ok = 0, variant = 0, dirty = 1, forcecreate = 0;
  char msg[200] = "", err[200] = "";
  COMP *c = NULL;
  enum { O_GENRE = 0, O_SUB, O_LEN, O_SIDS, O_VOICES, O_CHIP, O_SUBT, O_ENH, O_ENGINE, O_KEY, O_MODE, O_BPM, O_SEED, O_NUM };
  enum { B_PLAY = O_NUM, B_DICE, B_MEL, B_INS, B_CREATE, B_CANCEL, S_NUM };

  sg_init();
  if (g_chip < 0) g_chip = editorInfo.sidmodel ? 1 : 0;
  if (!g_seed) g_seed = (unsigned)time(NULL) % 999999 + 1;
  stopwait();
  fullsave(&snap);
  key = rawkey = 0;

  for (;;)
  {
    char buf[300];
    int k, hit = -1, ng = style_numgenres(), gfirst, gcount, y;
    if (dirty)
    {
      // a new piece: stop, compose, start again (clean SID state)
      if (previewing) stopwait();
      if (c) comp_free(c);
      activeplan = (g_planon) && (sg_engine) ? &g_plan : NULL;
      c = compose_now(err, sizeof err, &variant);
      ok = c != NULL;
      if (!ok)
      {
        snd_lock();
        fullrestore(&snap);
        snd_unlock();
      }
      if ((previewing) && (ok)) initsong(0, PLAY_BEGINNING);
      if (!ok) previewing = 0;
      dirty = 0;
    }
    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "New song");
    gfirst = g_genre >= 0 ? style_genre_first(g_genre) : -1;
    gcount = g_genre >= 0 ? style_genre_count(g_genre) : 0;
    for (k = 0; k < O_NUM; k++)
    {
      int ry = y0 + 2 + k, isel = sel == k, bg = isel ? C_SELECT : C_PANEL;
      switch (k)
      {
        case O_GENRE:
        snprintf(buf, sizeof buf, "Genre       " GS_PREV " %s " GS_NEXT "  %s", g_genre < 0 ? "Random" : sg_styles[gfirst].name,
                 g_genre < 0 ? "(every dice roll another genre)" : "");
        break;
        case O_SUB:
        if (g_genre < 0) snprintf(buf, sizeof buf, "Subgenre    " GS_PREV " Random " GS_NEXT);
        else if (gcount <= 1) snprintf(buf, sizeof buf, "Subgenre    -");
        else if (g_sub < 0) snprintf(buf, sizeof buf, "Subgenre    " GS_PREV " Random " GS_NEXT "  (%d subgenres)", gcount);
        else snprintf(buf, sizeof buf, "Subgenre    " GS_PREV " %s " GS_NEXT "  %d/%d", sg_styles[gfirst + g_sub].sub, g_sub + 1, gcount);
        break;
        case O_LEN: snprintf(buf, sizeof buf, "Length      " GS_PREV " %d:%02d " GS_NEXT "  (1 to 10 minutes)", g_seconds / 60, g_seconds % 60); break;
        case O_SIDS: snprintf(buf, sizeof buf, "SIDs        " GS_PREV " %d " GS_NEXT "  (%d voices%s)", g_sids, g_sids * 3,
                              g_sids == 6 ? ", saved as an 8-SID song" : ""); break;
        case O_VOICES: snprintf(buf, sizeof buf, "Voices      " GS_PREV " %s " GS_NEXT, g_voices == 3 ? "All voices" : "Voice 3 free for sound effects"); break;
        case O_CHIP: snprintf(buf, sizeof buf, "SID chip    " GS_PREV " %s " GS_NEXT, g_chip ? "8580" : "6581"); break;
        case O_SUBT: snprintf(buf, sizeof buf, "Subtunes    " GS_PREV " %s " GS_NEXT, g_subtunes ? "3: title, in-game, game over jingle" : "Off"); break;
        case O_ENH:
        if ((g_sids > 2) || (g_subtunes)) snprintf(buf, sizeof buf, "Enhanced    -  (only for 1 or 2 SIDs without subtunes)");
        else snprintf(buf, sizeof buf, "Enhanced    " GS_PREV " %s " GS_NEXT "%s", g_enhanced ? "On" : "Off",
                      g_enhanced ? "  composed for 8 SIDs, then calculated down" : "");
        break;
        case O_ENGINE:
        snprintf(buf, sizeof buf, "Composer    " GS_PREV " %s " GS_NEXT "  %s", sg_engine ? "1.1" : "Classic",
                 sg_engine ? "one theme, phrases with cadences" : "as version 1.0 (same seed, same song)");
        break;
        case O_KEY: snprintf(buf, sizeof buf, "Key         " GS_PREV " %s " GS_NEXT, g_key < 0 ? "Random" : sg_keyname[g_key]); break;
        case O_MODE: snprintf(buf, sizeof buf, "Scale       " GS_PREV " %s " GS_NEXT, g_mode < 0 ? "From the style" : sg_modelabel[g_mode]); break;
        case O_BPM:
        if (g_bpm) snprintf(buf, sizeof buf, "Tempo       " GS_PREV " %d BPM " GS_NEXT, g_bpm);
        else snprintf(buf, sizeof buf, "Tempo       " GS_PREV " From the style " GS_NEXT);
        break;
        case O_SEED: snprintf(buf, sizeof buf, "Seed        " GS_PREV " %u " GS_NEXT "  melody #%d, instruments #%d", g_seed, g_melvar, g_insvar); break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), 62);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + 64)) hit = k;
    }
    // info about the piece
    y = y0 + 2 + O_NUM + 1;
    if (ok)
    {
      const SGSTYLEREF *st = &sg_styles[variant];
      int sec = (int)(c->seconds + 0.5), n = 0;
      snprintf(buf, sizeof buf, "\"%s\"  -  %s%s%s", c->title, st->name, st->sub[0] ? " / " : "", st->sub);
      printtext(x0 + 3, y++, MKCOL(14, C_PANEL), buf);
      y += dlg_printwrapped(x0 + 3, y, w - 6, 2, MKCOL(8, C_PANEL), st->desc);
      snprintf(buf, sizeof buf, "%s %s, %.0f BPM, %d bars, %d:%02d min, %d SID%s, layout %s", sg_keyname[c->key], sg_modelabel[c->mode],
               c->bpm, c->total_bars, sec / 60, sec % 60, c->sids, c->sids > 1 ? "s" : "", c->layout);
      printtext(x0 + 3, y++, MKCOL(13, C_PANEL), buf);
      {
        static const char *nm[6] = {"glides", "bends", "fall-offs", "vibratos", "bass slides", "filter sweeps"};
        int o = snprintf(buf, sizeof buf, "Playing technique:");
        for (k = 0; k < 6; k++)
          if (c->expr_stats[k]) { o += snprintf(buf + o, sizeof buf - o, "%s %d %s", n ? "," : "", c->expr_stats[k], nm[k]); n++; }
        if (!n) o += snprintf(buf + o, sizeof buf - o, " none");
        if (c->kind == 1) snprintf(buf + o, sizeof buf - o, "; %d volume commands", c->nlevels);
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
      }
      {
        int o = snprintf(buf, sizeof buf, "Ideas:");
        n = 0;
        if (c->key_change_by) { o += snprintf(buf + o, sizeof buf - o, " key change +%d into the last chorus", c->key_change_by); n++; }
        for (k = 0; k < c->nstops; k++) { o += snprintf(buf + o, sizeof buf - o, "%s stop before the chorus at bar %d", n ? "," : "", c->stops[k]); n++; }
        if (!n) snprintf(buf + o, sizeof buf - o, " -");
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
      }
      {
        int o = snprintf(buf, sizeof buf, "Form:");
        for (k = 0; (k < c->nsecs) && (o < w - 12); k++)
          o += snprintf(buf + o, sizeof buf - o, " %s(%d)", sg_secname[c->secs[k]], c->secbars[k]);
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
      }
      if (c->sids >= 2)
      {
        int o = snprintf(buf, sizeof buf, "Voices:");
        for (k = 0; (k < c->ntracks) && (k < 6); k++)
          o += snprintf(buf + o, sizeof buf - o, "%s %d.%d %s", k ? "," : "", k / 3 + 1, k % 3 + 1, c->track_roles[c->track_order[k]]);
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
        if (c->sids >= 4)
        {
          snprintf(buf, sizeof buf, "        SIDs 3-%d: extra layers (drums 2, pads with real chords, bass 2, melody octaves ...)", c->sids);
          printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
        }
      }
    }
    else
    {
      dlg_printwrapped(x0 + 3, y, w - 6, 3, MKCOL(10, C_PANEL), err);
    }
    if (msg[0]) printtext(x0 + 3, y0 + h - 5, MKCOL(10, C_PANEL), msg);
    else if ((g_planon) && (sg_engine))
    {
      if (plan_note[0]) snprintf(buf, sizeof buf, "%s - TAB: the plan, X: plan off", plan_note);
      else snprintf(buf, sizeof buf, "Composed from the song plan \"%s\" (%d parts) - TAB: the plan, X: plan off", g_plan.title, g_plan.nsec);
      printtext(x0 + 3, y0 + h - 5, MKCOL(13, C_PANEL), buf);
    }
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL),
              "F1/SPACE play, D dice, M new melody, I new instruments, TAB song plan, ENTER create the song, ESC cancel");
    {
      const char *bt[6] = {previewing ? " " GS_STOP " Stop " : " " GS_PLAY " Play ", " " GS_DICE GS_DICER " Dice ", " New melody ",
                           " New instruments ", "  Create song  ", "  Cancel  "};
      int bx[6], xx = x0 + 3;
      for (k = 0; k < 6; k++)
      {
        int isel = sel == B_PLAY + k;
        bx[k] = k == 5 ? x0 + w - 13 : xx;
        printtext(bx[k], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), bt[k]);
        if ((mousey == y0 + h - 2) && (mousex >= bx[k]) && (mousex < bx[k] + (int)strlen(bt[k]))) hit = B_PLAY + k;
        xx += strlen(bt[k]) + 2;
      }
      if (sg_engine)
      {
        printtext(xx, y0 + h - 2, MKCOL(C_BUTTONTEXT, C_BUTTON), " Song plan (TAB) ");
        if ((mousey == y0 + h - 2) && (mousex >= xx) && (mousex < xx + 17)) hit = 999;
      }
    }
    fliptoscreen();
    getkey();
    if (win_quitted) break;
    if (forcecreate)
    {
      forcecreate = 0;
      sel = B_CREATE;
      rawkey = KEY_ENTER;
      key = 0;
    }
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit == 999))
    {
      rawkey = KEY_TAB;
      key = 0;
    }
    else if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit >= 0))
    {
      sel = hit;
      if (hit < O_NUM) rawkey = (ui_arrowat(mousex, mousey) < 0) ? KEY_LEFT : KEY_RIGHT;
      else rawkey = KEY_ENTER;
      key = 0;
    }
    msg[0] = 0;
    if ((rawkey == KEY_TAB) && (sg_engine))
    {
      int pw = 128, ph = 46, r;
      r = planpage(&c, &variant, &ok, &previewing, err, sizeof err, (MAX_COLUMNS - pw) / 2, (MAX_ROWS - ph) / 2, pw, ph);
      if (r == 2) break;
      if (r == 1) forcecreate = 1;
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_X) && (g_planon))
    {
      g_planon = 0;
      dirty = 1;
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_F1) || ((rawkey == KEY_SPACE) && (sel < B_DICE)) || ((rawkey == KEY_ENTER) && (sel == B_PLAY)))
    {
      if (previewing)
      {
        stopwait();
        previewing = 0;
      }
      else if (ok)
      {
        initsong(0, PLAY_BEGINNING);
        previewing = 1;
      }
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_D) || ((rawkey == KEY_ENTER) && (sel == B_DICE)))
    {
      g_seed = (g_seed * 69069u + 12345u) % 999999 + 1;
      g_melvar = g_insvar = 0;
      dirty = 1;
    }
    else if ((rawkey == KEY_M) || ((rawkey == KEY_ENTER) && (sel == B_MEL)))
    {
      g_melvar++;
      dirty = 1;
    }
    else if ((rawkey == KEY_I) || ((rawkey == KEY_ENTER) && (sel == B_INS)))
    {
      g_insvar++;
      dirty = 1;
    }
    else if ((rawkey == KEY_ESC) || ((rawkey == KEY_ENTER) && (sel == B_CANCEL))) break;
    else if (((rawkey == KEY_ENTER) && ((sel < O_NUM) || (sel == B_CREATE))) && (ok))
    {
      if ((snap.modified) && (!dlg_confirm("New song", "Discard the changes of the current song?"))) continue;
      stopwait();
      undoInitAllAreas();
      songmodified = 1;
      loadedsongfilename[0] = 0;
      {
        char nm[200], *p;
        snprintf(nm, sizeof nm, "%s_%s_%u%s", sg_styles[variant].key, c->title, g_seed, numsids > 2 ? ".ggt" : ".sng");
        for (p = nm; *p; p++)
          if ((*p == ' ') || (*p == '/') || (*p == '\\') || (*p == ':')) *p = '_';
          else if ((*p >= 'A') && (*p <= 'Z')) *p += 32;
        snprintf(songfilename, MAX_FILENAME, "%s", nm);
      }
      snprintf(buf, sizeof buf, "New song \"%s\" (%s%s%s, seed %u) - click Save to keep it", c->title, sg_styles[variant].name,
               sg_styles[variant].sub[0] ? " / " : "", sg_styles[variant].sub, g_seed);
      setstatusmessage(buf);
      comp_free(c);
      key = rawkey = 0;
      printmainscreen();
      return;
    }
    else
    {
      switch (rawkey)
      {
        case KEY_UP: if (sel > 0) sel--; break;
        case KEY_DOWN: case KEY_TAB: if (sel < S_NUM - 1) sel++; break;
        case KEY_LEFT:
        case KEY_RIGHT:
        {
          int d = rawkey == KEY_LEFT ? -1 : 1;
          if (sel >= O_NUM)
          {
            sel += d;
            if (sel < O_NUM) sel = O_NUM;
            if (sel >= S_NUM) sel = S_NUM - 1;
            break;
          }
          dirty = 1;
          switch (sel)
          {
            case O_GENRE: g_genre = ((g_genre + 1 + d) % (ng + 1) + ng + 1) % (ng + 1) - 1; g_sub = -1; break;
            case O_SUB:
            if (g_genre >= 0)
            {
              int n = style_genre_count(g_genre);
              if (n > 1) g_sub = ((g_sub + 1 + d) % (n + 1) + n + 1) % (n + 1) - 1;
              else dirty = 0;
            }
            else dirty = 0;
            break;
            case O_LEN:
            g_seconds += d * (shiftpressed ? 60 : 15);
            if (g_seconds < 60) g_seconds = 60;
            if (g_seconds > 600) g_seconds = 600;
            break;
            case O_SIDS:
            {
              static const int sv[5] = {1, 2, 4, 6, 8};
              int i;
              for (i = 0; i < 5; i++)
                if (sv[i] == g_sids) break;
              i = (i + d + 5) % 5;
              g_sids = sv[i];
            }
            break;
            case O_VOICES: g_voices = g_voices == 3 ? 2 : 3; break;
            case O_CHIP: g_chip ^= 1; break;
            case O_SUBT: g_subtunes ^= 1; break;
            case O_ENH:
            if ((g_sids <= 2) && (!g_subtunes)) g_enhanced ^= 1;
            else dirty = 0;
            break;
            case O_ENGINE: sg_engine ^= 1; break;
            case O_KEY: g_key = ((g_key + 1 + d) % 13 + 13) % 13 - 1; break;
            case O_MODE: g_mode = ((g_mode + 1 + d) % (M_NUM + 1) + M_NUM + 1) % (M_NUM + 1) - 1; break;
            case O_BPM:
            if (!g_bpm) g_bpm = d > 0 ? 60 : 200;
            else
            {
              g_bpm += d * (shiftpressed ? 10 : 2);
              if ((g_bpm < 60) || (g_bpm > 200)) g_bpm = 0;
            }
            break;
            case O_SEED:
            g_seed = (unsigned)((int)g_seed + d * (shiftpressed ? 100 : 1));
            if (g_seed < 1) g_seed = 1;
            g_melvar = g_insvar = 0;
            break;
          }
        }
        break;
      }
    }
    key = rawkey = 0;
  }
  // cancel: the old song again
  stopwait();
  if (c) comp_free(c);
  snd_lock();
  fullrestore(&snap);
  snd_unlock();
  key = rawkey = 0;
  printmainscreen();
}

// ---------------------------------------------------------------------------
// Dialog: MIDI import (CTRL+F)
// ---------------------------------------------------------------------------

static const char *gm_family[16] = {"Piano", "Chrom. percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
                                    "Reed", "Pipe", "Synth lead", "Synth pad", "Synth effects", "Ethnic", "Percussive", "Sound effects"};
static const char *acc_label[6] = {"Auto (decided per 2 bars)", "Arpeggios", "Stabs (short chords)", "Pads (held chords)",
                                   "Broken chords (piano / guitar)", "Counter melody"};
static const char *harm_label[3] = {"Normal", "Careful (fewer wrong notes)", "Loose (all chords)"};
static int m_sids = 1, m_voices = 3, m_chip = -1, m_acc = 0, m_harm = 0, m_drums = 1, m_len = 0, m_variant = 0;
static int m_melody = -1, m_bass = -2;          // index into the parts; -1 auto (melody), -2 auto / -1 none (bass)
static char midipath[MAX_PATHNAME] = "";

// convert: 4-8 SIDs: fewer SIDs when the piece does not fit into one song
static COMP *midi_convert(const unsigned char *data, int len, const char *base, MIDIINFO *info, int *sids_used, char *err, int errsize)
{
  MIDIOPTS o;
  int tries[4], ntries = 0, k, s2;
  midiopts_default(&o);
  o.voices = m_voices;
  o.model = m_chip ? 8580 : 6581;
  o.variant = m_variant;
  o.accomp = m_acc;
  o.harmony = m_harm;
  o.drums_off = !m_drums;
  o.max_seconds = m_len;
  snprintf(o.basename, sizeof o.basename, "%s", base);
  if ((m_melody >= 0) && (m_melody < info->nparts)) snprintf(o.melody, sizeof o.melody, "%d:%d", info->track[m_melody], info->channel[m_melody]);
  if (m_bass == -1) snprintf(o.bass, sizeof o.bass, "none");
  else if ((m_bass >= 0) && (m_bass < info->nparts)) snprintf(o.bass, sizeof o.bass, "%d:%d", info->track[m_bass], info->channel[m_bass]);
  tries[ntries++] = m_sids;
  if (m_sids > 2)
    for (k = 0; k < 3; k++)
    {
      static const int lower[3] = {4, 2, 1};
      if (lower[k] < m_sids) tries[ntries++] = lower[k];
    }
  for (s2 = 0; s2 < ntries; s2++)
  {
    COMP *c;
    MIDIINFO in2;
    int r;
    o.sids = tries[s2];
    c = sg_import_midi(data, len, &o, &in2, err, errsize);
    if (!c) return NULL;
    snd_lock();
    r = sg_buildsongs(&c, 1, 1, err, errsize);
    snd_unlock();
    if (r > 0)
    {
      *info = in2;
      *sids_used = o.sids;
      return c;
    }
    comp_free(c);
    if (r == 0) return NULL;
  }
  snprintf(err, errsize, "The piece does not fit into one song, even with one SID");
  return NULL;
}

static unsigned char *midi_loadfile(const char *path, int *len)
{
  FILE *f = fopen(path, "rb");
  unsigned char *d;
  long n;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((n <= 0) || (n > 32 * 1024 * 1024))
  {
    fclose(f);
    return NULL;
  }
  d = (unsigned char *)malloc(n);
  *len = (int)fread(d, 1, n, f);
  fclose(f);
  return d;
}

// choose a MIDI file: 1 = full path in out
static int midi_choosefile(char *out, int outsize)
{
  static char name[MAX_FILENAME] = "";
  char cwd[MAX_PATHNAME], dir[MAX_PATHNAME], filter[MAX_FILENAME];
  int ok;
  if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
  if (!midipath[0]) snprintf(midipath, sizeof midipath, "%s", cwd);
  snprintf(filter, sizeof filter, "*.mid;*.midi;*.rmi;*.kar");
  ok = fileselector(name, midipath, filter, "IMPORT A MIDI FILE (.mid, .midi, .rmi, .kar)", 0);
  if (ok)
  {
    if (!getcwd(dir, sizeof dir)) dir[0] = 0;
    snprintf(midipath, sizeof midipath, "%s", dir);
    snprintf(out, outsize, "%s/%s", dir, name);
  }
  if (cwd[0]) chdir(cwd);
  return ok;
}

void midiimportdialog(void)
{
  static FULLSNAP snap;
  int w = 108, h = 38;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, previewing = 0, ok = 0, dirty = 1, len = 0, sids_used = 1, armed = 0;
  char msg[200] = "", err[200] = "", file[MAX_PATHNAME + MAX_FILENAME + 2], base[64];
  unsigned char *data = NULL;
  MIDIINFO info;
  COMP *c = NULL;
  enum { O_SIDS = 0, O_VOICES, O_CHIP, O_ACC, O_HARM, O_MEL, O_BASS, O_DRUMS, O_LEN, O_NUM };
  enum { B_PLAY = O_NUM, B_AGAIN, B_FILE, B_CREATE, B_CANCEL, S_NUM };

  sg_init();
  if (m_chip < 0) m_chip = editorInfo.sidmodel ? 1 : 0;
  memset(&info, 0, sizeof info);
  if (!midi_choosefile(file, sizeof file))
  {
    key = rawkey = 0;
    printmainscreen();
    return;
  }
  data = midi_loadfile(file, &len);
  stopwait();
  fullsave(&snap);
  key = rawkey = 0;
  m_variant = 0;
  m_melody = -1;
  m_bass = -2;

  for (;;)
  {
    char buf[300];
    int k, hit = -1, y;
    if (dirty)
    {
      const char *b = strrchr(file, '/'), *dot;
#ifdef __WIN32__
      const char *b2 = strrchr(file, '\\');
      if ((b2) && ((!b) || (b2 > b))) b = b2;
#endif
      b = b ? b + 1 : file;
      dot = strrchr(b, '.');
      snprintf(base, sizeof base, "%.*s", dot && dot != b ? (int)(dot - b) : (int)strlen(b), b);
      if (previewing) stopwait();
      if (c) comp_free(c);
      c = NULL;
      if (!data) snprintf(err, sizeof err, "Cannot read the file %s", file);
      else c = midi_convert(data, len, base, &info, &sids_used, err, sizeof err);
      ok = c != NULL;
      if (!ok)
      {
        snd_lock();
        fullrestore(&snap);
        snd_unlock();
      }
      if ((previewing) && (ok)) initsong(0, PLAY_BEGINNING);
      if (!ok) previewing = 0;
      dirty = 0;
    }
    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "Import MIDI file");
    for (k = 0; k < O_NUM; k++)
    {
      int ry = y0 + 2 + k, isel = sel == k, bg = isel ? C_SELECT : C_PANEL;
      switch (k)
      {
        case O_SIDS:
        snprintf(buf, sizeof buf, "SIDs            " GS_PREV " %d " GS_NEXT "  (%d voices%s)", m_sids, m_sids * 3,
                 (ok) && (sids_used != m_sids) ? ", too big: fewer SIDs used" : (m_sids == 6 ? ", saved as an 8-SID song" : ""));
        break;
        case O_VOICES: snprintf(buf, sizeof buf, "Voices          " GS_PREV " %s " GS_NEXT, m_voices == 3 ? "All voices" : "Voice 3 free for sound effects"); break;
        case O_CHIP: snprintf(buf, sizeof buf, "SID chip        " GS_PREV " %s " GS_NEXT, m_chip ? "8580" : "6581"); break;
        case O_ACC: snprintf(buf, sizeof buf, "Accompaniment   " GS_PREV " %s " GS_NEXT, acc_label[m_acc]); break;
        case O_HARM: snprintf(buf, sizeof buf, "Harmony check   " GS_PREV " %s " GS_NEXT, harm_label[m_harm]); break;
        case O_MEL:
        if ((m_melody >= 0) && (m_melody < info.nparts))
          snprintf(buf, sizeof buf, "Melody part     " GS_PREV " %d: %s " GS_NEXT, m_melody + 1, info.name[m_melody][0] ? info.name[m_melody] : "(no name)");
        else snprintf(buf, sizeof buf, "Melody part     " GS_PREV " Auto " GS_NEXT);
        break;
        case O_BASS:
        if ((m_bass >= 0) && (m_bass < info.nparts))
          snprintf(buf, sizeof buf, "Bass part       " GS_PREV " %d: %s " GS_NEXT, m_bass + 1, info.name[m_bass][0] ? info.name[m_bass] : "(no name)");
        else snprintf(buf, sizeof buf, "Bass part       " GS_PREV " %s " GS_NEXT, m_bass == -1 ? "None (low notes of the accompaniment)" : "Auto");
        break;
        case O_DRUMS: snprintf(buf, sizeof buf, "Drums           " GS_PREV " %s " GS_NEXT, m_drums ? "On" : "Off"); break;
        case O_LEN:
        if (m_len) snprintf(buf, sizeof buf, "Length          " GS_PREV " at most %d:%02d " GS_NEXT, m_len / 60, m_len % 60);
        else snprintf(buf, sizeof buf, "Length          " GS_PREV " Whole file " GS_NEXT);
        break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), 70);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + 72)) hit = k;
    }
    y = y0 + 2 + O_NUM + 1;
    if (ok)
    {
      static const char *grid[9] = {"", "", "eighths", "eighth triplets", "16ths", "", "16th triplets", "", "32nds"};
      int sec = (int)(c->seconds + 0.5);
      snprintf(buf, sizeof buf, "\"%s\"  -  %s", c->title, file);
      buf[w - 6] = 0;
      printtext(x0 + 3, y++, MKCOL(14, C_PANEL), buf);
      snprintf(buf, sizeof buf, "%d/%d, grid %s, %.0f BPM (MIDI %.1f), %d bars, %d:%02d min, %d SID%s%s%s", info.timesig[0], info.timesig[1],
               info.q <= 8 ? grid[info.q] : "?", c->bpm, info.midi_bpm, info.nbars, sec / 60, sec % 60, sids_used, sids_used > 1 ? "s" : "",
               m_variant ? ", conversion #" : "", "");
      if (m_variant) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%d", m_variant + 1);
      printtext(x0 + 3, y++, MKCOL(13, C_PANEL), buf);
      printtext(x0 + 3, y++, MKCOL(7, C_PANEL), "Parts of the MIDI file:");
      for (k = 0; (k < info.nparts) && (k < 12); k++)
      {
        static const char *rn[4] = {"Accompaniment", "Bass", "Melody", "Melody 2"};
        snprintf(buf, sizeof buf, "%2d  %-24s ch %2d  %-17s %5d notes  -> %s", k + 1, info.name[k][0] ? info.name[k] : "(no name)",
                 info.channel[k] + 1, gm_family[(info.program[k] / 8) & 15], info.notes[k], rn[info.role[k] & 3]);
        printtext(x0 + 3, y++, MKCOL(info.role[k] == 2 ? 15 : 8, C_PANEL), buf);
      }
      if (info.nparts > 12)
      {
        snprintf(buf, sizeof buf, "    ... and %d more parts", info.nparts - 12);
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
      }
      if (info.drum_notes)
      {
        snprintf(buf, sizeof buf, "    Drums (channel 10)                         %5d notes  -> Drums%s", info.drum_notes, info.dance ? " (dance kit)" : "");
        printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
      }
      snprintf(buf, sizeof buf, "Chords: %d beats, %d checked against the melody, %d simplified, %d rests; %d volume commands",
               info.harm_beats, info.harm_changed, info.harm_simplified, info.harm_rests, info.level_commands);
      printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
    }
    else dlg_printwrapped(x0 + 3, y, w - 6, 3, MKCOL(10, C_PANEL), err);
    if (msg[0]) printtext(x0 + 3, y0 + h - 5, MKCOL(10, C_PANEL), msg);
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL),
              "F1/SPACE play, A convert again (other arrangement), O other file, ENTER create the song, ESC cancel");
    {
      const char *bt[5] = {previewing ? " " GS_STOP " Stop " : " " GS_PLAY " Play ", " " GS_DICE GS_DICER " Convert again ",
                           " Other file ", "  Create song  ", "  Cancel  "};
      int bx[5], xx = x0 + 3;
      for (k = 0; k < 5; k++)
      {
        int isel = sel == B_PLAY + k;
        bx[k] = k == 4 ? x0 + w - 13 : xx;
        printtext(bx[k], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), bt[k]);
        if ((mousey == y0 + h - 2) && (mousex >= bx[k]) && (mousex < bx[k] + (int)strlen(bt[k]))) hit = B_PLAY + k;
        xx += strlen(bt[k]) + 2;
      }
    }
    fliptoscreen();
    getkey();
    if (win_quitted) break;
    // the key that closed the file selector must not create the song at once
    if (!armed)
    {
      if ((key) || (rawkey) || (mouseb)) { key = rawkey = 0; continue; }
      armed = 1;
    }
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit >= 0))
    {
      sel = hit;
      if (hit < O_NUM) rawkey = (ui_arrowat(mousex, mousey) < 0) ? KEY_LEFT : KEY_RIGHT;
      else rawkey = KEY_ENTER;
      key = 0;
    }
    msg[0] = 0;
    if ((rawkey == KEY_F1) || ((rawkey == KEY_SPACE) && (sel < B_AGAIN)) || ((rawkey == KEY_ENTER) && (sel == B_PLAY)))
    {
      if (previewing)
      {
        stopwait();
        previewing = 0;
      }
      else if (ok)
      {
        initsong(0, PLAY_BEGINNING);
        previewing = 1;
      }
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_A) || ((rawkey == KEY_ENTER) && (sel == B_AGAIN)))
    {
      m_variant++;
      dirty = 1;
    }
    else if ((rawkey == KEY_O) || ((rawkey == KEY_ENTER) && (sel == B_FILE)))
    {
      char f2[sizeof file];
      if (previewing)
      {
        stopwait();
        previewing = 0;
      }
      if (midi_choosefile(f2, sizeof f2))
      {
        snprintf(file, sizeof file, "%s", f2);
        free(data);
        data = midi_loadfile(file, &len);
        m_variant = 0;
        m_melody = -1;
        m_bass = -2;
        memset(&info, 0, sizeof info);
        dirty = 1;
      }
      armed = 0;
    }
    else if ((rawkey == KEY_ESC) || ((rawkey == KEY_ENTER) && (sel == B_CANCEL))) break;
    else if (((rawkey == KEY_ENTER) && ((sel < O_NUM) || (sel == B_CREATE))) && (ok))
    {
      if ((snap.modified) && (!dlg_confirm("MIDI import", "Discard the changes of the current song?"))) continue;
      stopwait();
      undoInitAllAreas();
      songmodified = 1;
      loadedsongfilename[0] = 0;
      {
        char nm[200], *p;
        snprintf(nm, sizeof nm, "midi_%s%s%s", base, m_variant ? "_v" : "", numsids > 2 ? ".ggt" : ".sng");
        if (m_variant) snprintf(nm, sizeof nm, "midi_%s_v%d%s", base, m_variant, numsids > 2 ? ".ggt" : ".sng");
        for (p = nm; *p; p++)
          if ((*p == ' ') || (*p == '/') || (*p == '\\') || (*p == ':')) *p = '_';
          else if ((*p >= 'A') && (*p <= 'Z')) *p += 32;
        snprintf(songfilename, MAX_FILENAME, "%s", nm);
      }
      snprintf(buf, sizeof buf, "MIDI file imported: \"%s\", %d SID%s - click Save to keep it", c->title, numsids, numsids > 1 ? "s" : "");
      setstatusmessage(buf);
      comp_free(c);
      free(data);
      key = rawkey = 0;
      printmainscreen();
      return;
    }
    else
    {
      switch (rawkey)
      {
        case KEY_UP: if (sel > 0) sel--; break;
        case KEY_DOWN: case KEY_TAB: if (sel < S_NUM - 1) sel++; break;
        case KEY_LEFT:
        case KEY_RIGHT:
        {
          int d = rawkey == KEY_LEFT ? -1 : 1;
          if (sel >= O_NUM)
          {
            sel += d;
            if (sel < O_NUM) sel = O_NUM;
            if (sel >= S_NUM) sel = S_NUM - 1;
            break;
          }
          dirty = 1;
          switch (sel)
          {
            case O_SIDS:
            {
              static const int sv[5] = {1, 2, 4, 6, 8};
              int i;
              for (i = 0; i < 5; i++)
                if (sv[i] == m_sids) break;
              m_sids = sv[(i + d + 5) % 5];
            }
            break;
            case O_VOICES: m_voices = m_voices == 3 ? 2 : 3; break;
            case O_CHIP: m_chip ^= 1; break;
            case O_ACC: m_acc = (m_acc + d + 6) % 6; break;
            case O_HARM: m_harm = (m_harm + d + 3) % 3; break;
            case O_MEL:
            if (info.nparts) m_melody = ((m_melody + 1 + d) % (info.nparts + 1) + info.nparts + 1) % (info.nparts + 1) - 1;
            else dirty = 0;
            break;
            case O_BASS: m_bass = ((m_bass + 2 + d) % (info.nparts + 2) + info.nparts + 2) % (info.nparts + 2) - 2; break;
            case O_DRUMS: m_drums ^= 1; break;
            case O_LEN:
            m_len += d * 30;
            if (m_len < 0) m_len = 600;
            if (m_len > 600) m_len = 0;
            break;
          }
        }
        break;
      }
    }
    key = rawkey = 0;
  }
  stopwait();
  if (c) comp_free(c);
  free(data);
  snd_lock();
  fullrestore(&snap);
  snd_unlock();
  key = rawkey = 0;
  printmainscreen();
}

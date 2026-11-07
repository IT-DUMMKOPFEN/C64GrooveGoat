// C64 GrooveGoat - random songs: dialog and command line

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "goattrk2.h"
#include "gsgcomp.h"
#include "gsgbuild.h"
#include "gsgmidi.h"

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

// compose and build into the song memory; 4-8 SIDs: if the piece does not
// fit into one song, again without the variant sections, then also without
// the register arc
static COMP *subcomp(int v, unsigned seed, double seconds, int sids, int voices, int model, int variants, int compact,
                     COMP *fixed, int jingle)
{
  COMP *c = comp_new(v, seed, seconds, sids, voices, model);
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
  if ((enhanced) && (sids <= 2) && (!subtunes))
  {
    // enhanced SID calculation: composed for 8 SIDs, calculated down like a big MIDI file
    COMP *big = comp_new(v, seed, seconds, 8, 3, model), *c;
    int r;
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



void composedialog(void)
{
  static FULLSNAP snap;
  int w = 108, h = 37;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, previewing = 0, ok = 0, variant = 0, dirty = 1;
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
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL),
              "F1/SPACE play, D dice (new piece), M new melody, I new instruments, ENTER create the song, ESC cancel");
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
    }
    fliptoscreen();
    getkey();
    if (win_quitted) break;
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit >= 0))
    {
      sel = hit;
      if (hit < O_NUM) rawkey = (ui_arrowat(mousex, mousey) < 0) ? KEY_LEFT : KEY_RIGHT;
      else rawkey = KEY_ENTER;
      key = 0;
    }
    msg[0] = 0;
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

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
// the register arc (as SIDGen's app). subtunes: title music, in-game music
// and a game over jingle with the same instruments.
COMP *sidgen_make(int v, unsigned seed, double seconds, int sids, int voices, int model, char *err, int errsize, int subtunes)
{
  int attempt;
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

int sidgen_compose_cli(const char *spec, char *err, int errsize)
{
  char st[64] = "techno";
  unsigned seed = 1;
  double seconds = 90;
  int sids = 1, model = 6581, v;
  COMP *c;
  const char *p = strchr(spec, ',');
  snprintf(st, sizeof st, "%.*s", p ? (int)(p - spec) : (int)strlen(spec), spec);
  int subtunes = 0, voices = 3;
  if (p) sscanf(p + 1, "%u,%lf,%d,%d,%d,%d", &seed, &seconds, &sids, &model, &subtunes, &voices);
  v = findvariant(st, seed);
  if (v < 0)
  {
    snprintf(err, errsize, "Unknown style %s", st);
    return 0;
  }
  c = sidgen_make(v, seed, seconds, sids, voices, model, err, errsize, subtunes);
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
  fprintf(stderr, "%s - %s%s%s, %s %s, %.0f BPM, %d bars, %.0f s, layout %s\n", c->title, sg_styles[v].name,
          sg_styles[v].sub[0] ? " / " : "", sg_styles[v].sub, sg_keyname[c->key], sg_modelabel[c->mode], c->bpm, c->total_bars,
          c->seconds, c->layout);
  comp_free(c);
  return 1;
}

// ---------------------------------------------------------------------------
// Dialog: New song like SIDGen (CTRL+G)
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
static int g_seconds = 90;
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
  int w = 108, h = 36;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, previewing = 0, ok = 0, variant = 0, dirty = 1;
  char msg[200] = "", err[200] = "";
  COMP *c = NULL;
  enum { O_GENRE = 0, O_SUB, O_LEN, O_SIDS, O_VOICES, O_CHIP, O_SUBT, O_KEY, O_MODE, O_BPM, O_SEED, O_NUM };
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
    dlg_drawframe(x0, y0, w, h, "New song like SIDGen");
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
        if (!n) snprintf(buf + o, sizeof buf - o, " none");
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

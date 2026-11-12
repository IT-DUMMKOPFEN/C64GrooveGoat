// C64 GrooveGoat - Bass & Arps (CTRL+P)
//
// A bassline and / or chord arpeggios for the current subtune:
//   - the chords are recognised from what the other voices play (arpeggio
//     instruments count with the intervals of their wavetable); when there
//     is nothing to recognise, a chord progression of a style is used
//   - the bass figures (root, octave, walking, offbeat, acid, disco ...), the
//     arpeggio, stab, pad and broken-chord parts and the instruments come from
//     the composer (gsgcomp.c, gsgrender.c)
//   - the part is written into the voice of the cursor from the start of the
//     subtune (new patterns; tempo commands of that voice are kept)
// Everything happens on a copy of the song: the preview starts again after
// every change, Cancel brings the old song back, Create keeps the new one
// (a backup of the old song is written first).

#include <time.h>
#include "goattrk2.h"
#include "gsgcomp.h"
#include "gsgbuild.h"
#include "gres.h"
#include "gbassline.h"

extern char loadedsongfilename[MAX_FILENAME];

#define BL_MAXROWS 16384
#define BL_MAXBARS 1024

typedef struct
{
  INSTR instr[MAX_INSTR];
  unsigned char l[MAX_TABLES][MAX_TABLELEN], r[MAX_TABLES][MAX_TABLELEN];
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int pattlen[MAX_PATT];
  unsigned char order[MAX_SONGS][MAX_CHN][MAX_SONGLEN + 2];
  int songlen[MAX_SONGS][MAX_CHN];
} BSNAP;

static void bsave(BSNAP *s)
{
  memcpy(s->instr, instr, sizeof instr);
  memcpy(s->l, ltable, sizeof ltable);
  memcpy(s->r, rtable, sizeof rtable);
  memcpy(s->patt, pattern, sizeof pattern);
  memcpy(s->pattlen, pattlen, sizeof pattlen);
  memcpy(s->order, songorder, sizeof songorder);
  memcpy(s->songlen, songlen, sizeof songlen);
}

static void brestore(const BSNAP *s)
{
  memcpy(instr, s->instr, sizeof instr);
  memcpy(ltable, s->l, sizeof ltable);
  memcpy(rtable, s->r, sizeof rtable);
  memcpy(pattern, s->patt, sizeof pattern);
  memcpy(pattlen, s->pattlen, sizeof pattlen);
  memcpy(songorder, s->order, sizeof songorder);
  memcpy(songlen, s->songlen, sizeof songlen);
  countpatternlengths();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

enum { PART_BASS = 0, PART_ARPS, PART_BOTH, PART_NUM };
static const char *partname[PART_NUM] = {"Bassline", "Arpeggios / chords", "Bass + arpeggios"};
enum { AM_HOLD = 0, AM_RHYTHM, AM_PAD, AM_BROKEN, AM_COUNTER, AM_NUM };
static const char *arpmodename[AM_NUM] = {"Arpeggio held per chord", "Arpeggio stabs (rhythm of the style)", "Pad (slow chord)",
                                         "Broken chords (piano / guitar)", "Counter line (chord tones)"};
static const int arpmode2[AM_NUM] = {MD_ARP_HOLD, MD_ARP_RHYTHM, MD_PAD, MD_BROKEN, MD_COUNTER};
static const char *figures[] = {"root8", "root16", "offbeat", "rolling", "octave", "rock", "pop", "syncop", "walking", "boogie",
                                "reggae", "sustain", "acid", "dnb", "halftime", "drone", "march", "oompah", "arpbass", "alberti",
                                "surf", "motown", "disco", "eighties", "gallop", "ostinato", "pedal", "tresillo", "rnb", "electro",
                                "robot", "sequencer"};
#define NFIG ((int)(sizeof figures / sizeof figures[0]))

static int b_part = PART_BOTH, b_genre = -1, b_sub = -1, b_figure = -1, b_bassins = -1, b_arpmode = AM_HOLD, b_arptpl = -1;
static int b_source = 0;               // 0 recognise (random when nothing is found), 1 always random
static int b_bt = 4, b_key = -1, b_arpchn = -1;
static unsigned b_seed = 0;
static int b_newsid = 0, b_secs = 90, b_sids = 1;   // a new SID (bass, arps & chords) instead of the current song

// bass instruments of the library (hand-made ones)
static int nbassins, bassins[512];

static void listbass(void)
{
  int k, n = sg_handcount();
  nbassins = 0;
  for (k = 0; (k < n) && (nbassins < 512); k++)
  {
    const SGINS *i = sg_hand(k);
    if (!strcmp(i->cat, "bass")) bassins[nbassins++] = k;
  }
}

// ---------------------------------------------------------------------------
// The song: timeline of the subtune, sounding notes, chords
// ---------------------------------------------------------------------------

static int sub, total;                 // subtune, rows of the longest voice
static signed char *snote;             // [ch][row] sounding note (-1 none)
static unsigned char *sins;            // [ch][row] its instrument
static int drumins[MAX_INSTR], relint[MAX_INSTR][12], nrel[MAX_INSTR];
static CHORD bars[BL_MAXBARS];
static int barfound[BL_MAXBARS], nbars, nfound;

static int flatlen(int c)
{
  int d, n = 0, rep = 0;
  for (d = 0; d < songlen[sub][c]; d++)
  {
    int v = songorder[sub][c][d];
    if (v >= TRANSDOWN) continue;
    if (v >= REPEAT) { rep = v - REPEAT; continue; }
    n += pattlen[v] * (rep + 1);
    rep = 0;
  }
  return n;
}

// instruments: noise / fixed-pitch drums do not count for the chords;
// arpeggio instruments add the intervals of their wavetable
static void classify(void)
{
  int i;
  for (i = 1; i < MAX_INSTR; i++)
  {
    int pos = instr[i].ptr[WTBL], k, noise = 0, tones = 0, abs_ = 0, seen = 0;
    nrel[i] = 0;
    drumins[i] = 0;
    if (!pos) continue;
    for (k = pos - 1; (k < MAX_TABLELEN) && (seen < 32); k++, seen++)
    {
      int l = ltable[WTBL][k], r = rtable[WTBL][k];
      if (l == 0xff) break;
      if ((l >= 0x10) && (l < 0xe0))
      {
        if ((l & 0xf0) == 0x80) noise++;
        else tones++;
      }
      if (r >= 0x81 && r <= 0xdf) abs_++;
      else if ((r < 0x60) && (l < 0xe0))
      {
        int iv = r % 12, j;
        for (j = 0; j < nrel[i]; j++)
          if (relint[i][j] == iv) break;
        if ((j >= nrel[i]) && (nrel[i] < 12)) relint[i][nrel[i]++] = iv;
      }
    }
    if (((noise) && (!tones)) || ((abs_ >= 2) && (abs_ * 2 >= tones + noise))) drumins[i] = 1;
  }
}

static void readsong(int skip1, int skip2)
{
  int c;
  total = 0;
  for (c = 0; c < numchannels; c++)
  {
    int n = flatlen(c);
    if (n > total) total = n;
  }
  if (total > BL_MAXROWS) total = BL_MAXROWS;
  free(snote);
  free(sins);
  snote = (signed char *)malloc(MAX_CHN * (BL_MAXROWS + 1));
  sins = (unsigned char *)calloc(MAX_CHN * (BL_MAXROWS + 1), 1);
  memset(snote, -1, MAX_CHN * (BL_MAXROWS + 1));
  classify();
  for (c = 0; c < numchannels; c++)
  {
    int d, row = 0, trans = 0, rep = 0, cur = -1, ins = 1;
    if ((c == skip1) || (c == skip2)) continue;
    for (d = 0; (d < songlen[sub][c]) && (row < total); d++)
    {
      int v = songorder[sub][c][d], x, r;
      if (v >= TRANSDOWN) { trans = v - TRANSUP; continue; }
      if (v >= REPEAT) { rep = v - REPEAT; continue; }
      for (x = 0; x <= rep; x++)
        for (r = 0; (r < pattlen[v]) && (row < total); r++, row++)
        {
          unsigned char *q = &pattern[v][r * 4];
          if (q[1]) ins = q[1];
          if (q[0] <= LASTNOTE) cur = q[0] - FIRSTNOTE + trans;
          else if (q[0] == KEYOFF) cur = -1;
          if ((cur >= 0) && (!drumins[ins]))
          {
            snote[c * (BL_MAXROWS + 1) + row] = cur;
            sins[c * (BL_MAXROWS + 1) + row] = ins;
          }
        }
      rep = 0;
    }
  }
}

static int tonesof(CHORD ch)
{
  int k, t = 0;
  for (k = 0; k < sg_qlen[ch.q]; k++) t |= 1 << ((ch.root + sg_qiv[ch.q][k]) % 12);
  return t;
}

// chord of rows r0..r1-1 (as the MIDI importer: everything that sounds, bass bonus)
static int chordof(int r0, int r1, CHORD prev, int haveprev, CHORD *out)
{
  static const int quals[9] = {Q_MAJ, Q_MIN, Q_DOM7, Q_MIN7, Q_MAJ7, Q_SUS4, Q_DIM, Q_AUG, Q_POW};
  double w[12] = {0}, tot = 0, best = 0;
  int c, r, k, low = 999, lowpc = -1, have = 0;
  for (c = 0; c < numchannels; c++)
    for (r = r0; r < r1; r++)
    {
      int n = snote[c * (BL_MAXROWS + 1) + r], ins = sins[c * (BL_MAXROWS + 1) + r];
      if (n < 0) continue;
      w[n % 12] += 1.0;
      for (k = 0; k < nrel[ins]; k++)
        if (relint[ins][k]) w[(n + relint[ins][k]) % 12] += 0.6;
      if ((r == r0) && (n < low)) { low = n; lowpc = n % 12; }
    }
  for (k = 0; k < 12; k++) tot += w[k];
  if (tot < (r1 - r0) * 0.8) return 0;
  for (r = 0; r < 12; r++)
    for (k = 0; k < 9; k++)
    {
      CHORD ch;
      int t, i, n = 0;
      double inside = 0, sc;
      ch.root = r;
      ch.q = quals[k];
      t = tonesof(ch);
      for (i = 0; i < 12; i++)
        if ((t >> i) & 1) { inside += w[i]; n++; }
      sc = inside - 0.9 * (tot - inside) - 0.15 * tot * (n - 3) / 3.0;
      if (lowpc == r) sc += 0.25 * tot;
      if ((ch.q == Q_DIM) || (ch.q == Q_AUG) || (ch.q == Q_SUS4) || (ch.q == Q_POW)) sc -= 0.12 * tot;
      if ((haveprev) && (prev.root == r) && (prev.q == ch.q)) sc += 0.12 * tot;
      if ((!have) || (sc > best)) { have = 1; best = sc; *out = ch; }
    }
  return have;
}

static void findchords(int spb)
{
  int b, haveprev = 0;
  CHORD prev = {0, 0};
  nbars = (total + spb - 1) / spb;
  if (nbars > BL_MAXBARS) nbars = BL_MAXBARS;
  nfound = 0;
  for (b = 0; b < nbars; b++)
  {
    CHORD ch;
    int r0 = b * spb, r1 = (b + 1) * spb < total ? (b + 1) * spb : total;
    barfound[b] = chordof(r0, r1, prev, haveprev, &ch);
    if (barfound[b])
    {
      bars[b] = ch;
      prev = ch;
      haveprev = 1;
      nfound++;
    }
  }
  // gaps: the chord before (or the next one at the start)
  for (b = 0; b < nbars; b++)
    if (!barfound[b])
    {
      int j;
      for (j = b - 1; (j >= 0) && (!barfound[j]); j--) ;
      if (j >= 0) bars[b] = bars[j];
      else
      {
        for (j = b + 1; (j < nbars) && (!barfound[j]); j++) ;
        if (j < nbars) bars[b] = bars[j];
        else { bars[b].root = 0; bars[b].q = Q_MIN; }
      }
    }
}

// ---------------------------------------------------------------------------
// Writing a voice
// ---------------------------------------------------------------------------

static int patternfree(int p, const unsigned char *reserved)
{
  int s, c, d, r;
  if (reserved[p]) return 0;
  for (r = 0; r < pattlen[p]; r++)
    if ((pattern[p][r * 4] != REST) || (pattern[p][r * 4 + 1]) || (pattern[p][r * 4 + 2]) || (pattern[p][r * 4 + 3])) return 0;
  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < songlen[s][c]; d++)
        if (songorder[s][c][d] == p) return 0;
  return 1;
}

static int songtempo(void)
{
  int c, d, r, t = 0;
  for (c = 0; c < numchannels; c++)
    for (d = 0; d < songlen[sub][c]; d++)
    {
      int v = songorder[sub][c][d];
      if (v >= REPEAT) continue;
      for (r = 0; r < pattlen[v]; r++)
        if ((pattern[v][r * 4 + 2] == CMD_SETTEMPO) && ((pattern[v][r * 4 + 3] & 0x7f) >= 2))
        {
          int x = pattern[v][r * 4 + 3] & 0x7f;
          if ((!t) || (x < t)) t = x;
        }
    }
  return t ? t : 6;
}

// the voice's tempo commands per row (kept in the new patterns)
static void tempocmds(int c, unsigned char *cmd, unsigned char *dat)
{
  int d, row = 0, rep = 0;
  memset(cmd, 0, total);
  memset(dat, 0, total);
  for (d = 0; (d < songlen[sub][c]) && (row < total); d++)
  {
    int v = songorder[sub][c][d], x, r;
    if (v >= TRANSDOWN) continue;
    if (v >= REPEAT) { rep = v - REPEAT; continue; }
    for (x = 0; x <= rep; x++)
      for (r = 0; (r < pattlen[v]) && (row < total); r++, row++)
      {
        int cm = pattern[v][r * 4 + 2];
        if ((cm == CMD_SETTEMPO) || (cm == CMD_FUNKTEMPO))
        {
          cmd[row] = cm;
          dat[row] = pattern[v][r * 4 + 3];
        }
      }
    rep = 0;
  }
}

// track -> new patterns and orderlist of voice c
static int writevoice(COMP *cp, SGTRACK *t, int c, int plen, char *err, int errsize)
{
  static unsigned char reserved[MAX_PATT];
  static unsigned char tcmd[BL_MAXROWS + 1], tdat[BL_MAXROWS + 1];
  int map[SG_MAXINS], k, start, n = 0, gt = songtempo() - 1, old[MAX_SONGLEN], nold = 0;
  unsigned char order[MAX_SONGLEN + 2];
  if (gt > 2) gt = 2;
  if (gt < 1) gt = 1;
  for (k = 0; k < SG_MAXINS; k++) map[k] = 0;
  memset(reserved, 0, sizeof reserved);
  tempocmds(c, tcmd, tdat);
  for (k = 0; k < songlen[sub][c]; k++)
    if (songorder[sub][c][k] < REPEAT) old[nold++] = songorder[sub][c][k];
  // the old patterns of the voice: free when no other voice / subtune uses them
  songlen[sub][c] = 0;
  songorder[sub][c][0] = LOOPSONG;
  songorder[sub][c][1] = 0;
  for (k = 0; k < nold; k++)
  {
    int p = old[k], s, cc, d, used = 0;
    for (s = 0; (s < MAX_SONGS) && (!used); s++)
      for (cc = 0; (cc < MAX_CHN) && (!used); cc++)
        for (d = 0; d < songlen[s][cc]; d++)
          if (songorder[s][cc][d] == p) { used = 1; break; }
    if (!used)
    {
      int r;
      for (r = 0; r < pattlen[p]; r++)
      {
        pattern[p][r * 4] = REST;
        pattern[p][r * 4 + 1] = pattern[p][r * 4 + 2] = pattern[p][r * 4 + 3] = 0;
      }
    }
  }
  for (start = 0; start < total; start += plen)
  {
    unsigned char rows[MAX_PATTROWS * 4];
    int len = total - start < plen ? total - start : plen, r, p, q;
    for (r = 0; r < len; r++)
    {
      int row = start + r;
      SGEV *e = row < t->rows ? &t->ev[row] : NULL;
      unsigned char *w = &rows[r * 4];
      w[0] = REST;
      w[1] = w[2] = w[3] = 0;
      if ((e) && (e->type == EV_OFF)) w[0] = KEYOFF;
      else if ((e) && (e->type == EV_NOTE))
      {
        int note = e->note;
        if (!map[e->ins])
        {
          map[e->ins] = sg_addinstrument(&cp->ins[e->ins], 1 << (c % 3), gt, err, errsize);
          if (!map[e->ins]) return 0;
        }
        while (note > 92) note -= 12;
        while (note < 0) note += 12;
        w[0] = FIRSTNOTE + note;
        w[1] = map[e->ins];
      }
      if (tcmd[row])
      {
        w[2] = tcmd[row];
        w[3] = tdat[row];
      }
    }
    // the same pattern again: used once
    p = -1;
    for (q = 0; q < MAX_PATT; q++)
      if ((reserved[q]) && (pattlen[q] == len) && (!memcmp(pattern[q], rows, len * 4))) { p = q; break; }
    if (p < 0)
    {
      for (q = 0; q < MAX_PATT; q++)
        if (patternfree(q, reserved)) { p = q; break; }
      if (p < 0)
      {
        snprintf(err, errsize, "No free pattern left (all %d are used)", MAX_PATT);
        return 0;
      }
      memcpy(pattern[p], rows, len * 4);
      pattern[p][len * 4] = ENDPATT;
      pattern[p][len * 4 + 1] = pattern[p][len * 4 + 2] = pattern[p][len * 4 + 3] = 0;
      pattlen[p] = len;
      reserved[p] = 1;
    }
    if (n >= MAX_SONGLEN - 2)
    {
      snprintf(err, errsize, "The orderlist of the voice would be too long");
      return 0;
    }
    order[n++] = p;
  }
  for (k = 0; k < n; k++) songorder[sub][c][k] = order[k];
  songorder[sub][c][n] = LOOPSONG;
  songorder[sub][c][n + 1] = 0;
  songlen[sub][c] = n;
  countpatternlengths();
  return 1;
}

// ---------------------------------------------------------------------------
// Making the parts
// ---------------------------------------------------------------------------

static COMP *comp = NULL;
static char info1[200], info2[300];

static int defaultstyle(void)
{
  int k;
  for (k = 0; k < sg_numstyles; k++)
    if (!strcmp(sg_styles[k].key, "pop")) return k;
  return 0;
}

static int genreofstyle(int st, int *sub)
{
  int g, ng = style_numgenres();
  for (g = 0; g < ng; g++)
  {
    int first = style_genre_first(g), n = style_genre_count(g);
    if ((st >= first) && (st < first + n))
    {
      *sub = st - first;
      return g;
    }
  }
  *sub = -1;
  return 0;
}

// the genre (and subgenre) of the dialog; "Any" subgenre: chosen by the seed
static void genredefault(void)
{
  int g, sb;
  song_genre_get(&g, &sb);
  if (g >= 0) { b_genre = g; b_sub = sb; }
  if (b_genre < 0) b_genre = genreofstyle(defaultstyle(), &sb);
}

static int curstyle(void)
{
  if (b_genre < 0) genredefault();
  return song_style_of(b_genre, b_sub, b_seed ? b_seed : 1);
}

static int emptyvoice(int c)
{
  int d, r;
  for (d = 0; d < songlen[sub][c]; d++)
  {
    int v = songorder[sub][c][d];
    if (v >= REPEAT) continue;
    for (r = 0; r < pattlen[v]; r++)
      if (pattern[v][r * 4] <= LASTNOTE) return 0;
  }
  return 1;
}

static int notecount(int c)
{
  int d, r, n = 0;
  for (d = 0; d < songlen[sub][c]; d++)
  {
    int v = songorder[sub][c][d];
    if (v >= REPEAT) continue;
    for (r = 0; r < pattlen[v]; r++)
      if (pattern[v][r * 4] <= LASTNOTE) n++;
  }
  return n;
}

static int bassvoice(void) { return editorInfo.epchn; }
static int used_ac = -1;               // the chord voice of the last make() (found on the old song)

static int arpvoice(void)
{
  int c, bc = bassvoice();
  if (b_part == PART_ARPS) return bc;
  if ((b_arpchn >= 0) && (b_arpchn < numchannels) && (b_arpchn != bc)) return b_arpchn;
  for (c = 0; c < numchannels; c++)
    if ((c != bc) && (c / 3 == bc / 3) && (emptyvoice(c))) return c;
  for (c = 0; c < numchannels; c++)
    if ((c != bc) && (emptyvoice(c))) return c;
  // no free voice: the one with the fewest notes on this SID (the melody stays)
  {
    int best = -1, bn = 0;
    for (c = (bc / 3) * 3; (c < (bc / 3) * 3 + 3) && (c < numchannels); c++)
    {
      int n = notecount(c);
      if (c == bc) continue;
      if ((best < 0) || (n <= bn)) { best = c; bn = n; }
    }
    return best >= 0 ? best : (bc + 1) % numchannels;
  }
}

static int make(char *err, int errsize)
{
  int spb = 4 * b_bt, plen, bc = bassvoice(), ac, b, random = 0, style;
  SGTRACK tb, ta;
  if (!b_seed) b_seed = (unsigned)time(NULL) % 99999 + 1;
  style = curstyle();
  sub = editorInfo.esnum;
  ac = arpvoice();
  used_ac = ac;
  readsong(b_part != PART_ARPS ? bc : -1, b_part != PART_BASS ? ac : -1);
  if (total < spb * 4) total = spb * 32;          // an empty subtune: 32 bars
  // the composer of the style: progression, key, instruments, bass figures
  if (comp) comp_free(comp);
  comp = comp_new(style, b_seed, 60, 1, 3, editorInfo.sidmodel ? 8580 : 6581);
  comp->engine = 0;                 // the bass dialog keeps the progressions of the style
  comp->want_key = b_key;
  if (!comp_compose(comp))
  {
    snprintf(err, errsize, "%s", comp->err[0] ? comp->err : "The style data could not be read");
    return 0;
  }
  comp->bt = b_bt;
  comp->spb = spb;
  comp->cur = &comp->rng;
  findchords(spb);
  if ((b_source == 1) || (nfound * 4 < nbars))
  {
    // nothing (or not enough) to recognise: the progression of the style, bar by bar
    int cbars = comp->nchordrows / (comp->total_rows / comp->total_bars);
    random = 1;
    for (b = 0; b < nbars; b++)
    {
      int src = cbars ? b % cbars : 0;
      bars[b] = comp->chord_rows[src * (comp->total_rows / comp->total_bars)];
    }
  }
  {
    int k, o = snprintf(info1, sizeof info1, "%s: ", random ? "Chords of the style" : "Chords found");
    for (k = 0; (k < nbars) && (k < 16) && (o < (int)sizeof info1 - 12); k++)
      o += snprintf(info1 + o, sizeof info1 - o, "%s%s%s", k ? " " : "", sg_keyname[bars[k].root],
                    bars[k].q == Q_MAJ ? "" : (bars[k].q == Q_MIN ? "m" : sg_qfile[bars[k].q]));
    if (nbars > 16) snprintf(info1 + o, sizeof info1 - o, " ...");
    if (random) snprintf(info2, sizeof info2, "%d bars, %s %s (%s)%s", nbars, sg_keyname[comp->key], sg_modelabel[comp->mode], sg_styles[style].name,
                         nfound ? " - too few chords found in the song" : "");
    else snprintf(info2, sizeof info2, "%d bars, chords recognised in %d of them", nbars, nfound);
  }
  plen = spb * 4 <= MAX_PATTROWS ? spb * 4 : spb;
  // bass
  if (b_part != PART_ARPS)
  {
    const char *bi = b_bassins >= 0 && b_bassins < nbassins ? sg_hand(bassins[b_bassins])->name : comp->i_bass;
    int ins = comp_use(comp, bi);
    tr_init(&tb, total + 64);
    for (b = 0; b < nbars; b++)
    {
      RND out[64];
      int n = 0, k, sec = comp->secs[0] < SEC_NUM ? SEC_A : SEC_A;
      const char *fig = b_figure >= 0 ? figures[b_figure] : comp->bass_pat[SEC_A];
      comp_bass_bar(comp, fig[0] ? fig : "root8", bars[b], bars[b + 1 < nbars ? b + 1 : b], b, comp->riffs[sec], out, &n);
      for (k = 0; k < n; k++)
        if ((out[k].row >= 0) && (out[k].row < spb) && (b * spb + out[k].row < total))
          tr_note(&tb, b * spb + out[k].row, out[k].note, ins, out[k].dur > 0 ? out[k].dur : 1);
    }
    tr_finalize(&tb, total);
    if (!writevoice(comp, &tb, bc, plen, err, errsize))
    {
      tr_free(&tb);
      return 0;
    }
    tr_free(&tb);
  }
  // arpeggios / chords
  if (b_part != PART_BASS)
  {
    if ((b_arptpl >= 0) && (b_arptpl < sg_arpcount()))
    {
      const char *tn = sg_arpat(b_arptpl)->name;
      snprintf(comp->t_arp, sizeof comp->t_arp, "%s", tn);
      snprintf(comp->t_stab, sizeof comp->t_stab, "%s", tn);
      snprintf(comp->t_pad, sizeof comp->t_pad, "%s", tn);
    }
    tr_init(&ta, total + 64);
    for (b = 0; b < nbars; b += 4)
    {
      int nb = nbars - b < 4 ? nbars - b : 4;
      comp_render_mode2(comp, &ta, b * spb, nb, &bars[b], arpmode2[b_arpmode], NULL, 0, 0, NULL, 0);
    }
    tr_finalize(&ta, total);
    if (!writevoice(comp, &ta, ac, plen, err, errsize))
    {
      tr_free(&ta);
      return 0;
    }
    tr_free(&ta);
  }
  return 1;
}

// command line: --bassline=PART,STYLE,FIGURE,ARPMODE,SEED (for tests)
int bassline_cli(const char *spec, char *err, int errsize)
{
  char st[40] = "";
  int part = PART_BOTH, fig = -1, am = AM_HOLD, k;
  unsigned seed = 1;
  listbass();
  sscanf(spec, "%d,%39[^,],%d,%d,%u", &part, st, &fig, &am, &seed);
  b_part = part >= 0 && part < PART_NUM ? part : PART_BOTH;
  b_genre = genreofstyle(defaultstyle(), &b_sub);
  for (k = 0; k < sg_numstyles; k++)
    if (!strcmp(sg_styles[k].key, st)) { b_genre = genreofstyle(k, &b_sub); break; }
  b_figure = fig < NFIG ? fig : -1;
  b_arpmode = am >= 0 && am < AM_NUM ? am : AM_HOLD;
  b_seed = seed;
  if (!make(err, errsize)) return 0;
  fprintf(stderr, "%s\n%s\n", info1, info2);
  return 1;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

static BSNAP *orig = NULL;
static int previewing = 0;

static void stopandwait(void)
{
  int t;
  stopsong();
  for (t = 0; (t < 100) && (isplaying()); t++) SDL_Delay(5);
}

static COMP *ncomp = NULL;             // the new SID (bass, arps & chords)
static int nvariant = 0;

static int rebuild(char *err, int errsize)
{
  int ok;
  if (previewing) stopandwait();
  if (ncomp) { comp_free(ncomp); ncomp = NULL; }
  snd_lock();
  songsnap_back();
  if (b_newsid)
  {
    snd_unlock();
    if (!b_seed) b_seed = (unsigned)time(NULL) % 99999 + 1;
    if (b_genre < 0) genredefault();
    ncomp = compose_parts(b_genre, b_sub, b_seed, b_secs, b_sids, b_key, SGP_NOLEAD | SGP_NODRUMS, err, errsize, &nvariant);
    ok = ncomp != NULL;
    snd_lock();
    if (!ok) songsnap_back();
    else
    {
      snprintf(info1, sizeof info1, "\"%s\" - %s%s%s, %s %s, %d BPM", ncomp->title, sg_styles[nvariant].name,
               sg_styles[nvariant].sub[0] ? " / " : "", sg_styles[nvariant].sub, sg_keyname[ncomp->key], sg_modelabel[ncomp->mode],
               (int)(ncomp->bpm + 0.5));
      snprintf(info2, sizeof info2, "%d parts, %d bars: bass, arpeggios and chords - the melody and drum voices stay free",
               ncomp->nsecs, ncomp->total_bars);
    }
    sub = 0;
  }
  else
  {
    brestore(orig);
    sub = editorInfo.esnum;
    ok = make(err, errsize);
    if (!ok) brestore(orig);
  }
  snd_unlock();
  if ((previewing) && (ok)) initsong(sub, PLAY_BEGINNING);
  if (!ok) previewing = 0;
  return ok;
}

static int writebackup(char *out, int outsize)
{
  char base[MAX_FILENAME], *dot;
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  int ggt = numsids > 2;
  snprintf(base, sizeof base, "%s", loadedsongfilename[0] ? loadedsongfilename : "untitled");
  dot = strrchr(base, '.');
  if ((dot) && (!strchr(dot, '/'))) *dot = 0;
  snprintf(out, outsize, "%s_before-bass_%04d-%02d-%02d_%02d%02d%s", base, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, ggt ? ".ggt" : ".sng");
  return writesongfile(out, ggt);
}

void basslinedialog(void)
{
  int w = 100, h = 32;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, ok, dirty = 0;
  int restoresel = -1;
  char err[200] = "", msg[200] = "", buf[300];
  enum { O_WRITE = 0, O_PART, O_SOURCE, O_GENRE, O_SUB, O_KEY, O_LEN, O_SIDS, O_FIGURE, O_BASSINS, O_ARPMODE, O_ARPTPL, O_ARPCHN,
         O_BT, O_NUM };
  enum { B_PLAY = O_NUM, B_DICE, B_CREATE, B_CANCEL, S_NUM };

  sg_init();
  listbass();
  genredefault();
  if (!orig) orig = (BSNAP *)malloc(sizeof(BSNAP));
  stopandwait();
  sub = editorInfo.esnum;
  bsave(orig);
  songsnap_take();
  previewing = 0;
  ok = rebuild(err, sizeof err);
  key = rawkey = 0;
  for (;;)
  {
    int k, hit = -1, bc = bassvoice(), ac = used_ac >= 0 ? used_ac : bc, y, vis[O_NUM], nvis = 0, vi;
    int gfirst, gcount;
    // the rows of the mode
    for (k = 0; k < O_NUM; k++)
    {
      int show;
      if (b_newsid) show = (k == O_WRITE) || (k == O_GENRE) || (k == O_SUB) || (k == O_KEY) || (k == O_LEN) || (k == O_SIDS);
      else show = (k != O_LEN) && (k != O_SIDS);
      if (show) vis[nvis++] = k;
    }
    if (sel < O_NUM)
    {
      for (vi = 0; vi < nvis; vi++)
        if (vis[vi] == sel) break;
      if (vi == nvis) sel = O_WRITE;
    }
    if (dirty)
    {
      ok = rebuild(err, sizeof err);
      dirty = 0;
    }
    if (b_genre < 0) genredefault();
    gfirst = style_genre_first(b_genre);
    gcount = style_genre_count(b_genre);
    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "Bass, Chords & Arps");
    for (vi = 0; vi < nvis; vi++)
    {
      int k2 = vis[vi], ry = y0 + 2 + vi, isel = sel == k2, bg = isel ? C_SELECT : C_PANEL;
      switch (k2)
      {
        case O_WRITE: snprintf(buf, sizeof buf, "Write           " GS_PREV " %s " GS_NEXT, b_newsid ?
                               "New SID: a whole song with bass, arpeggios and chords" : "Into the current song (voice of the cursor)"); break;
        case O_PART: snprintf(buf, sizeof buf, "Part            " GS_PREV " %s " GS_NEXT, partname[b_part]); break;
        case O_SOURCE: snprintf(buf, sizeof buf, "Chords          " GS_PREV " %s " GS_NEXT, b_source ? "Progression of the genre" :
                                "Recognised from the song (else of the genre)"); break;
        case O_GENRE: snprintf(buf, sizeof buf, "Genre           " GS_PREV " %s " GS_NEXT "  %d/%d", sg_styles[gfirst].name, b_genre + 1,
                               style_numgenres()); break;
        case O_SUB:
        if (gcount <= 1) snprintf(buf, sizeof buf, "Subgenre        -");
        else if (b_sub < 0) snprintf(buf, sizeof buf, "Subgenre        " GS_PREV " Any " GS_NEXT "  (%d subgenres - D picks another one)", gcount);
        else snprintf(buf, sizeof buf, "Subgenre        " GS_PREV " %s " GS_NEXT "  %d/%d", sg_styles[gfirst + b_sub].sub, b_sub + 1, gcount);
        break;
        case O_KEY: snprintf(buf, sizeof buf, "Key             " GS_PREV " %s " GS_NEXT, b_key < 0 ? "From the genre" : sg_keyname[b_key]); break;
        case O_LEN: snprintf(buf, sizeof buf, "Length          " GS_PREV " %d:%02d " GS_NEXT "  (1 to 10 minutes)", b_secs / 60, b_secs % 60); break;
        case O_SIDS: snprintf(buf, sizeof buf, "SIDs            " GS_PREV " %d " GS_NEXT "  (%d voices)", b_sids, b_sids * 3); break;
        case O_FIGURE: snprintf(buf, sizeof buf, "Bass figure     " GS_PREV " %s " GS_NEXT, b_figure < 0 ? "From the genre" : figures[b_figure]); break;
        case O_BASSINS:
        snprintf(buf, sizeof buf, "Bass sound      " GS_PREV " %s " GS_NEXT, b_bassins < 0 ? "From the genre" :
                 (sg_hand(bassins[b_bassins])->en[0] ? sg_hand(bassins[b_bassins])->en : sg_hand(bassins[b_bassins])->name));
        break;
        case O_ARPMODE: snprintf(buf, sizeof buf, "Chord part      " GS_PREV " %s " GS_NEXT, arpmodename[b_arpmode]); break;
        case O_ARPTPL:
        snprintf(buf, sizeof buf, "Arpeggio sound  " GS_PREV " %s " GS_NEXT, b_arptpl < 0 ? "From the genre" :
                 (sg_arpat(b_arptpl)->en[0] ? sg_arpat(b_arptpl)->en : sg_arpat(b_arptpl)->name));
        break;
        case O_ARPCHN:
        if (b_part == PART_BOTH) snprintf(buf, sizeof buf, "Voices          bass %d.%d (cursor), chords " GS_PREV " %d.%d " GS_NEXT "%s", bc / 3 + 1, bc % 3 + 1,
                                          ac / 3 + 1, ac % 3 + 1, b_arpchn < 0 ? "  (automatic)" : "");
        else snprintf(buf, sizeof buf, "Voice           %d.%d (the voice of the cursor)", bc / 3 + 1, bc % 3 + 1);
        break;
        case O_BT: snprintf(buf, sizeof buf, "Rows per beat   " GS_PREV " %d " GS_NEXT "  (%d rows per bar)", b_bt, 4 * b_bt); break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hit = k2;
    }
    y = y0 + 3 + nvis;
    if (ok)
    {
      printtext(x0 + 3, y++, MKCOL(14, C_PANEL), info1);
      printtext(x0 + 3, y++, MKCOL(8, C_PANEL), info2);
      if (b_newsid) snprintf(buf, sizeof buf, "Seed %u - Create replaces the current song (like New song)", b_seed);
      else snprintf(buf, sizeof buf, "Seed %u - written from the start of subtune %02X, the old notes of the voice are replaced", b_seed, sub);
      printtext(x0 + 3, y++, MKCOL(8, C_PANEL), buf);
    }
    else dlg_printwrapped(x0 + 3, y, w - 6, 3, MKCOL(10, C_PANEL), err);
    if (msg[0]) printtext(x0 + 3, y0 + h - 5, MKCOL(10, C_PANEL), msg);
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL), b_newsid ? "F1/SPACE play, D dice, ENTER create (a new song), ESC cancel (the old song again)" :
              "F1/SPACE play, D dice, ENTER create (backup + undo step), ESC cancel (the old song again)");
    {
      const char *bt[4] = {previewing ? " " GS_STOP " Stop " : " " GS_PLAY " Play ", " " GS_DICE GS_DICER " Dice ", "  Create  ", "  Cancel  "};
      int bx[4], xx = x0 + 3;
      for (k = 0; k < 4; k++)
      {
        int isel = sel == B_PLAY + k;
        bx[k] = k == 3 ? x0 + w - 13 : xx;
        printtext(bx[k], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), bt[k]);
        if ((mousey == y0 + h - 2) && (mousex >= bx[k]) && (mousex < bx[k] + (int)strlen(bt[k]))) hit = B_PLAY + k;
        xx += strlen(bt[k]) + 2;
      }
    }
    if (restoresel >= 0)
    {
      // a clicked button (or F1) did its job; the keyboard focus stays where it was
      sel = restoresel;
      restoresel = -1;
    }
    fliptoscreen();
    getkey();
    if (win_quitted) break;
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb) && (hit >= 0))
    {
      if ((hit >= O_NUM) && (sel < O_NUM)) restoresel = sel;
      sel = hit;
      if (hit < O_NUM) rawkey = (ui_arrowat(mousex, mousey) < 0) ? KEY_LEFT : KEY_RIGHT;
      else
      {
        rawkey = KEY_ENTER;
      }
      key = 0;
    }
    msg[0] = 0;
    if ((rawkey == KEY_F1) || ((rawkey == KEY_SPACE) && (sel < B_DICE)) || ((rawkey == KEY_ENTER) && (sel == B_PLAY)))
    {
      if (previewing)
      {
        stopandwait();
        previewing = 0;
      }
      else if (ok)
      {
        initsong(sub, PLAY_BEGINNING);
        previewing = 1;
      }
      key = rawkey = 0;
      continue;
    }
    if ((rawkey == KEY_D) || ((rawkey == KEY_ENTER) && (sel == B_DICE)))
    {
      b_seed = (b_seed * 69069u + 12345u) % 99999 + 1;
      dirty = 1;
    }
    else if ((rawkey == KEY_ESC) || ((rawkey == KEY_ENTER) && (sel == B_CANCEL))) break;
    else if (((rawkey == KEY_ENTER) && ((sel < O_NUM) || (sel == B_CREATE))) && (ok) && (b_newsid))
    {
      if ((songsnap_modified()) && (!dlg_confirm("New SID", "Discard the changes of the current song?"))) continue;
      stopandwait();
      previewing = 0;
      compose_keep(ncomp, nvariant, b_seed, "New SID with bass, arpeggios and chords");
      comp_free(ncomp);
      ncomp = NULL;
      song_genre_set(b_genre, b_sub);
      key = rawkey = 0;
      printmainscreen();
      return;
    }
    else if (((rawkey == KEY_ENTER) && ((sel < O_NUM) || (sel == B_CREATE))) && (ok))
    {
      char bak[MAX_FILENAME + 64];
      BSNAP *now = (BSNAP *)malloc(sizeof(BSNAP));
      stopandwait();
      previewing = 0;
      // backup of the old song, then the new one stays
      bsave(now);
      brestore(orig);
      if (!writebackup(bak, sizeof bak)) bak[0] = 0;
      // one undo step (CTRL+Z brings the old voices back)
      {
        void *ed = undoinstrbegin();
        int s2, c;
        undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
        for (s2 = 0; s2 < MAX_SONGS; s2++)
          for (c = 0; c < numchannels; c++)
            undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c + s2 * MAX_CHN, UNDO_AREA_DIRTY_CHECK);
        undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
        brestore(now);
        undoinstrend(ed);
      }
      free(now);
      songmodified = 1;
      song_genre_set(b_genre, b_sub);
      snprintf(buf, sizeof buf, "%s written%s%s", partname[b_part], bak[0] ? " - backup of the old song: " : "", bak);
      setstatusmessage(buf);
      key = rawkey = 0;
      printmainscreen();
      return;
    }
    else
    {
      int cur = 0;
      for (vi = 0; vi < nvis; vi++)
        if (vis[vi] == sel) cur = vi;
      switch (rawkey)
      {
        case KEY_UP:
        if (sel >= O_NUM) sel = vis[nvis - 1];
        else if (cur > 0) sel = vis[cur - 1];
        break;
        case KEY_DOWN: case KEY_TAB:
        if (sel >= O_NUM) { if (sel < S_NUM - 1) sel++; }
        else if (cur < nvis - 1) sel = vis[cur + 1];
        else sel = B_PLAY;
        break;
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
            case O_WRITE: b_newsid ^= 1; break;
            case O_PART: b_part = (b_part + d + PART_NUM) % PART_NUM; break;
            case O_SOURCE: b_source ^= 1; break;
            case O_GENRE:
            {
              int ng = style_numgenres();
              b_genre = (b_genre + d + ng) % ng;
              b_sub = -1;
            }
            break;
            case O_SUB:
            if (gcount > 1) b_sub = ((b_sub + 1 + d) % (gcount + 1) + gcount + 1) % (gcount + 1) - 1;
            else dirty = 0;
            break;
            case O_KEY: b_key = ((b_key + 1 + d) % 13 + 13) % 13 - 1; break;
            case O_LEN:
            b_secs += d * (shiftpressed ? 60 : 15);
            if (b_secs < 60) b_secs = 60;
            if (b_secs > 600) b_secs = 600;
            break;
            case O_SIDS:
            {
              static const int sv[4] = {1, 2, 4, 8};
              int i;
              for (i = 0; i < 4; i++)
                if (sv[i] == b_sids) break;
              b_sids = sv[(i + d + 4) % 4];
            }
            break;
            case O_FIGURE: b_figure = ((b_figure + 1 + d) % (NFIG + 1) + NFIG + 1) % (NFIG + 1) - 1; break;
            case O_BASSINS: b_bassins = ((b_bassins + 1 + d) % (nbassins + 1) + nbassins + 1) % (nbassins + 1) - 1; break;
            case O_ARPMODE: b_arpmode = (b_arpmode + d + AM_NUM) % AM_NUM; break;
            case O_ARPTPL:
            {
              int n = sg_arpcount();
              b_arptpl = ((b_arptpl + 1 + d) % (n + 1) + n + 1) % (n + 1) - 1;
            }
            break;
            case O_ARPCHN:
            if (b_part == PART_BOTH)
            {
              int c = used_ac >= 0 ? used_ac : bassvoice(), tries;
              for (tries = 0; tries < numchannels; tries++)
              {
                c = (c + d + numchannels) % numchannels;
                if (c != bassvoice()) break;
              }
              b_arpchn = c;
            }
            else dirty = 0;
            break;
            case O_BT:
            {
              static const int bv[5] = {2, 3, 4, 6, 8};
              int i;
              for (i = 0; i < 5; i++)
                if (bv[i] == b_bt) break;
              b_bt = bv[(i + d + 5) % 5];
            }
            break;
          }
        }
        break;
      }
    }
    key = rawkey = 0;
  }
  // cancel: the old song again
  stopandwait();
  previewing = 0;
  if (ncomp) { comp_free(ncomp); ncomp = NULL; }
  snd_lock();
  songsnap_back();
  snd_unlock();
  key = rawkey = 0;
  printmainscreen();
}

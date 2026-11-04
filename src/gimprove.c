// C64 GrooveGoat - Improve (CTRL+I)
//
// Automatic improvements of the current subtune:
//   - instrument roles from the tables and the notes (kick, snare, hi-hat,
//     tom, bass, lead, pad, arpeggio), changeable in the window
//   - drums: new punchier kick / snare / hat / tom sounds (beat editor kits)
//   - bass: fuller (pulse with slow width sweep, more sustain), pushier
//     (octave pop at the start, hard restart, quick decay)
//   - melody: toneportamento slides between close notes, delayed vibrato
//   - pulse width modulation for static pulse sounds
//   - balance: pads and arpeggios softer
//   - fixes: gate timer longer than the tempo allows
//   - echo of the melody on an (almost) empty channel
//   - filter sweep for the bass where the SID filter is free
//   - SID chip adaptation of the filter cutoffs (6581 <-> 8580)
//   - clean-up of unused instruments, patterns and table rows
// Changed instruments are copies used only by this subtune; patterns that
// other subtunes share are copied first. A backup is written before.

#include <time.h>
#include <stdarg.h>
#include "goattrk2.h"
#include "gres.h"
#include "gbeat.h"
#include "gimprove.h"

extern char loadedsongfilename[MAX_FILENAME];

enum { RL_OTHER = 0, RL_KICK, RL_SNARE, RL_HAT, RL_TOM, RL_BASS, RL_LEAD, RL_PAD, RL_ARP, RL_COMBO, RL_NUM };
static const char *rolenames[RL_NUM] = {"Other (keep)", "Kick", "Snare", "Hi-hat", "Tom", "Bass", "Lead", "Pad", "Arpeggio", "Drum+tone (keep)"};

static const char *modnames[IM_NUM] = {
  "Drums: punchier kick, clearer snare, crisp hi-hats",
  "Bass: fuller (more bass)",
  "Bass: pushier attack",
  "Melody: slides between close notes",
  "Melody: delayed vibrato on long notes",
  "Pulse width modulation (lead, pad, arpeggio, bass)",
  "Balance: pads and arpeggios softer",
  "Fix: gate timers that are too long for the tempo",
  "Echo of the melody on an (almost) empty channel",
  "Filter sweep for the bass (where the SID filter is free)",
  "Clean up unused instruments, patterns, table rows (song)"
};

static int modules = (1 << IM_NUM) - 1;
static int strength = 2;
static int drumkit = KIT_909;
static int chipadapt = 0;
static int roleof[MAX_INSTR];           // role per instrument (current subtune)
static int roleset[MAX_INSTR];          // 1 = chosen by the user

static char report[40][120];
static int numreport;

static void rep(const char *fmt, ...)
{
  va_list ap;
  if (numreport >= 40) return;
  va_start(ap, fmt);
  vsnprintf(report[numreport++], 120, fmt, ap);
  va_end(ap);
}

// ---------------------------------------------------------------------------
// Song snapshot (instruments, tables, patterns, orderlists)
// ---------------------------------------------------------------------------

typedef struct
{
  INSTR instr[MAX_INSTR];
  unsigned char l[MAX_TABLES][MAX_TABLELEN], r[MAX_TABLES][MAX_TABLELEN];
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int pattlen[MAX_PATT];
  unsigned char order[MAX_SONGS][MAX_CHN][MAX_SONGLEN + 2];
  int songlen[MAX_SONGS][MAX_CHN];
} SNAP;

static void snapsave(SNAP *s)
{
  memcpy(s->instr, instr, sizeof instr);
  memcpy(s->l, ltable, sizeof ltable);
  memcpy(s->r, rtable, sizeof rtable);
  memcpy(s->patt, pattern, sizeof pattern);
  memcpy(s->pattlen, pattlen, sizeof pattlen);
  memcpy(s->order, songorder, sizeof songorder);
  memcpy(s->songlen, songlen, sizeof songlen);
}

static void snaprestore(const SNAP *s)
{
  memcpy(instr, s->instr, sizeof instr);
  memcpy(ltable, s->l, sizeof ltable);
  memcpy(rtable, s->r, sizeof rtable);
  memcpy(pattern, s->patt, sizeof pattern);
  memcpy(pattlen, s->pattlen, sizeof pattlen);
  memcpy(songorder, s->order, sizeof songorder);
  memcpy(songlen, s->songlen, sizeof songlen);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int instrempty(int i)
{
  const INSTR *in = &instr[i];
  return (!in->ad) && (!in->sr) && (!in->ptr[0]) && (!in->ptr[1]) && (!in->ptr[2]) && (!in->ptr[3]) &&
         (!in->vibdelay) && (!in->name[0]);
}

static int freeinstr(void)
{
  int i;
  for (i = 1; i < MAX_INSTR - 1; i++)
    if (instrempty(i)) return i;
  return 0;
}

typedef struct { unsigned char l[64], r[64]; int n; } TB;

static void tadd(TB *t, int l, int r)
{
  if (t->n < 63) { t->l[t->n] = l; t->r[t->n] = r; t->n++; }
}

// append a program (jumps relative to its start, FF xx with xx = row+1 of
// the program); returns the 1-based pointer or 0 when the table is full
static int putprog(int tbl, const TB *t)
{
  int start = gettablelen(tbl), c;
  if (!t->n) return 0;
  if (start + t->n > MAX_TABLELEN) return 0;
  for (c = 0; c < t->n; c++)
  {
    ltable[tbl][start + c] = t->l[c];
    rtable[tbl][start + c] = t->r[c];
    if ((t->l[c] == 0xff) && (t->r[c])) rtable[tbl][start + c] = t->r[c] + start;
  }
  return start + 1;
}

// read a program from the song (until stop / jump, at most 60 rows)
static void getprog(int tbl, int ptr, TB *t)
{
  int pos = ptr - 1;
  t->n = 0;
  while ((pos >= 0) && (pos < MAX_TABLELEN) && (t->n < 60))
  {
    int l = ltable[tbl][pos], r = rtable[tbl][pos];
    if (l == 0xff)
    {
      // jumps into the program become relative, others stop
      int target = r - 1 - (ptr - 1);
      tadd(t, 0xff, (r && target >= 0 && target < t->n) ? target + 1 : 0);
      break;
    }
    tadd(t, l, r);
    pos++;
  }
}

static int isrelwave(int l) { return (l >= 0x10) && (l < 0xe0); }

// ---------------------------------------------------------------------------
// Scope: the current subtune
// ---------------------------------------------------------------------------

static int sub;

static int patternreferenced(int p, int except)
{
  int s, c, d;
  for (s = 0; s < MAX_SONGS; s++)
  {
    if (s == except) continue;
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < MAX_SONGLEN; d++)
      {
        int v = songorder[s][c][d];
        if (v == LOOPSONG) break;
        if (v == p) return 1;
      }
  }
  return 0;
}

static unsigned char reserved[MAX_PATT];

static int freepattern(void)
{
  int p, r;
  for (p = 0; p < MAX_PATT; p++)
  {
    int empty = 1;
    if (reserved[p]) continue;
    for (r = 0; r < pattlen[p]; r++)
      if ((pattern[p][r * 4] != REST) || (pattern[p][r * 4 + 1]) || (pattern[p][r * 4 + 2]) || (pattern[p][r * 4 + 3])) empty = 0;
    if ((empty) && (!patternreferenced(p, -1))) return p;
  }
  return -1;
}

static int clonepattern(int p)
{
  int n = freepattern();
  if (n < 0) return -1;
  memcpy(pattern[n], pattern[p], sizeof pattern[p]);
  pattlen[n] = pattlen[p];
  reserved[n] = 1;
  return n;
}

// patterns of the subtune that other subtunes use too get their own copy
static int ownpatterns(void)
{
  static int map[MAX_PATT];
  int c, d, p, n = 0;
  for (p = 0; p < MAX_PATT; p++) map[p] = -1;
  for (c = 0; c < numchannels; c++)
    for (d = 0; d < songlen[sub][c]; d++)
    {
      p = songorder[sub][c][d];
      if (p >= REPEAT) continue;
      if (map[p] < 0)
      {
        map[p] = p;
        if (patternreferenced(p, sub))
        {
          int q = clonepattern(p);
          if (q < 0) return -1;
          map[p] = q;
          n++;
        }
      }
      songorder[sub][c][d] = map[p];
    }
  return n;
}

static int inscope(int p)
{
  int c, d;
  for (c = 0; c < numchannels; c++)
    for (d = 0; d < songlen[sub][c]; d++)
      if (songorder[sub][c][d] == p) return 1;
  return 0;
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

typedef struct
{
  int used, notes, notesum, durs, chnmask;
  int noise, absnotes, tones, relnotes, pitched, lowabs, minnote, maxnote;
} INSINFO;

static INSINFO info[MAX_INSTR];
static int chnnotes[MAX_CHN][RL_NUM];

static void analyse(void)
{
  int c, d, r, i;
  memset(info, 0, sizeof info);
  for (c = 0; c < numchannels; c++)
  {
    int cur = 1, lastrow = -1, lastins = 0, row = 0, trans = 0;
    for (d = 0; d < songlen[sub][c]; d++)
    {
      int v = songorder[sub][c][d];
      if (v >= TRANSDOWN) { trans = v - TRANSUP; continue; }
      if (v >= REPEAT) continue;
      for (r = 0; r < pattlen[v]; r++, row++)
      {
        unsigned char *q = &pattern[v][r * 4];
        if (q[1]) cur = q[1];
        if ((q[0] <= LASTNOTE) || (q[0] == KEYOFF))
        {
          if ((lastrow >= 0) && (lastins)) info[lastins].durs += row - lastrow;
          lastrow = -1;
        }
        if (q[0] <= LASTNOTE)
        {
          INSINFO *f = &info[cur];
          int note = q[0] - FIRSTNOTE + trans;
          if (!f->notes) { f->minnote = note; f->maxnote = note; }
          f->used = 1;
          f->notes++;
          f->notesum += note;
          if (note < f->minnote) f->minnote = note;
          if (note > f->maxnote) f->maxnote = note;
          f->chnmask |= 1 << c;
          lastrow = row;
          lastins = cur;
        }
      }
    }
  }
  for (i = 1; i < MAX_INSTR - 1; i++)
  {
    INSINFO *f = &info[i];
    TB w;
    int k;
    if (!f->used) continue;
    f->lowabs = 999;
    if (instr[i].ptr[WTBL])
    {
      getprog(WTBL, instr[i].ptr[WTBL], &w);
      for (k = 0; k < w.n; k++)
      {
        int l = w.l[k], rr = w.r[k];
        if ((l >= 0x80) && (l < 0xe0) && (l & 0x80) && ((l & 0xf0) == 0x80)) f->noise++;
        else if (isrelwave(l) && ((l & 0xf0) != 0x80)) f->tones++;
        if ((isrelwave(l) || ((l >= 0x80) && (l < 0xe0))) && (rr >= 0x81) && (rr <= 0xdf))
        {
          f->absnotes++;
          if (rr - 0x80 < f->lowabs) f->lowabs = rr - 0x80;
        }
        if (isrelwave(l) && (rr > 0) && (rr < 0x60)) f->relnotes++;
        if (isrelwave(l) && ((l & 0xf0) != 0x80) && (rr < 0x80)) f->pitched++;
      }
    }
  }
}

static int guessrole(int i)
{
  char n[MAX_INSTRNAMELEN + 1];
  INSINFO *f = &info[i];
  int c, avg;
  for (c = 0; c < MAX_INSTRNAMELEN && instr[i].name[c]; c++) n[c] = tolower((unsigned char)instr[i].name[c]);
  n[c] = 0;
  // drum + tone in one instrument (BD+Bass, Snare+Bass ...): the drum frames
  // are followed by a tone that follows the note - kept as it is
  if ((f->absnotes >= 2) && (f->pitched)) return RL_COMBO;
  if ((strchr(n, '+')) && (strstr(n, "bd") || strstr(n, "kick") || strstr(n, "snare") || strstr(n, "hat"))) return RL_COMBO;
  if (strstr(n, "kick") || strstr(n, "bassdrum") || strstr(n, "bass drum") || !strncmp(n, "bd", 2)) return RL_KICK;
  if (strstr(n, "snare") || strstr(n, "clap") || !strncmp(n, "sd", 2)) return RL_SNARE;
  if (strstr(n, "hat") || strstr(n, "hh") || strstr(n, "cymb") || strstr(n, "crash") || strstr(n, "ride")) return RL_HAT;
  if (strstr(n, "tom")) return RL_TOM;
  if (strstr(n, "say ")) return RL_OTHER;          // speech
  if (strstr(n, "arp")) return RL_ARP;
  if (strstr(n, "bass")) return RL_BASS;
  if (strstr(n, "pad") || strstr(n, "string") || strstr(n, "chord")) return RL_PAD;
  if (strstr(n, "lead") || strstr(n, "solo") || strstr(n, "melod")) return RL_LEAD;
  // drums: noise and absolute notes
  if ((f->noise) && (!f->tones)) return RL_HAT;
  if ((f->absnotes >= 2) && (f->absnotes * 2 >= f->tones + f->noise))
  {
    if ((f->noise >= 2) && (f->tones)) return RL_SNARE;
    if (f->lowabs <= 40) return RL_KICK;
    return f->noise >= 2 ? RL_SNARE : RL_TOM;
  }
  if (!f->notes) return RL_OTHER;
  avg = f->notesum / f->notes;
  if (f->relnotes >= 2) return RL_ARP;
  if (avg < 36) return RL_BASS;
  if (((instr[i].ad >> 4) >= 6) || ((f->notes) && (f->durs / f->notes >= 12) && ((instr[i].sr & 0x0f) >= 8))) return RL_PAD;
  return RL_LEAD;
}

static void guessroles(void)
{
  int i;
  analyse();
  for (i = 1; i < MAX_INSTR - 1; i++)
    if (!roleset[i]) roleof[i] = info[i].used ? guessrole(i) : RL_OTHER;
}

static int songtempo(void)
{
  // smallest tempo of the subtune (tempo commands), else the default
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
  if (t) return t;
  if ((instr[MAX_INSTR - 1].ad >= 2) && (!instr[MAX_INSTR - 1].ptr[WTBL])) return instr[MAX_INSTR - 1].ad;
  return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

// ---------------------------------------------------------------------------
// Instrument improvements (on copies)
// ---------------------------------------------------------------------------

static int copyof[MAX_INSTR];

static int makecopy(int i, char *err, int errsize)
{
  int n;
  char nm[MAX_INSTRNAMELEN + 4];
  if (copyof[i]) return copyof[i];
  n = freeinstr();
  if (!n)
  {
    snprintf(err, errsize, "No free instrument slot for a copy of %02X", i);
    return 0;
  }
  instr[n] = instr[i];
  snprintf(nm, sizeof nm, "%.13s+", instr[i].name[0] ? instr[i].name : "Instr");
  memset(instr[n].name, 0, MAX_INSTRNAMELEN);
  strncpy(instr[n].name, nm, MAX_INSTRNAMELEN - 1);
  copyof[i] = n;
  return n;
}

static int pulsestatic(int i)
{
  TB p;
  int k;
  if (!instr[i].ptr[PTBL]) return 1;
  getprog(PTBL, instr[i].ptr[PTBL], &p);
  for (k = 0; k < p.n; k++)
    if ((p.l[k] < 0x80) && (p.l[k] != 0) && (p.r[k])) return 0;      // has modulation
  return 1;
}

static int haspulsewave(int i)
{
  TB w;
  int k;
  if (!instr[i].ptr[WTBL]) return (instr[i].firstwave & 0x40) != 0;
  getprog(WTBL, instr[i].ptr[WTBL], &w);
  for (k = 0; k < w.n; k++)
    if (isrelwave(w.l[k]) && (w.l[k] & 0x40)) return 1;
  return 0;
}

static int pwmprog(int ins, int start, int lo, int hi, int frames)
{
  TB p;
  int speed = (hi - lo) / frames;
  (void)ins;
  if (speed < 1) speed = 1;
  if (speed > 0x7f) speed = 0x7f;
  p.n = 0;
  tadd(&p, 0x80 | ((start >> 8) & 0x0f), start & 0xff);
  tadd(&p, frames, speed);
  tadd(&p, frames, (-speed) & 0xff);
  tadd(&p, 0xff, 2);
  return putprog(PTBL, &p);
}

static int filterfree[MAX_SIDS];

static void sidfilteruse(void)
{
  int c, d, r, s;
  for (s = 0; s < MAX_SIDS; s++) filterfree[s] = 1;
  for (c = 0; c < numchannels; c++)
  {
    int cur = 1;
    for (d = 0; d < songlen[sub][c]; d++)
    {
      int v = songorder[sub][c][d];
      if (v >= REPEAT) continue;
      for (r = 0; r < pattlen[v]; r++)
      {
        unsigned char *q = &pattern[v][r * 4];
        if (q[1]) cur = q[1];
        if ((q[2] >= CMD_SETFILTERPTR) && (q[2] <= CMD_SETFILTERCUTOFF)) filterfree[c / 3] = 0;
        if ((q[0] <= LASTNOTE) && (instr[cur].ptr[FTBL])) filterfree[c / 3] = 0;
      }
    }
  }
}

// 6581 / 8580 cutoff curves (register high byte -> Hz, approximate)
static int cut2hz(int c, int is8580)
{
  static const int p6581[][2] = {{0, 200}, {32, 230}, {64, 330}, {96, 600}, {128, 1300}, {160, 2600}, {192, 5000}, {224, 8500}, {255, 12000}};
  int k;
  if (is8580) return c * 49;
  for (k = 1; k < 9; k++)
    if (c <= p6581[k][0])
      return p6581[k - 1][1] + (c - p6581[k - 1][0]) * (p6581[k][1] - p6581[k - 1][1]) / (p6581[k][0] - p6581[k - 1][0]);
  return 12000;
}

static int hz2cut(int hz, int is8580)
{
  int c;
  for (c = 0; c < 256; c++)
    if (cut2hz(c, is8580) >= hz) return c;
  return 255;
}

static void adaptfilter(int ins)
{
  TB f;
  int k, from8580 = chipadapt == 2;
  if (!instr[ins].ptr[FTBL]) return;
  getprog(FTBL, instr[ins].ptr[FTBL], &f);
  for (k = 0; k < f.n; k++)
    if ((f.l[k] == 0x00) && (k > 0 || 1))
      f.r[k] = hz2cut(cut2hz(f.r[k], from8580), !from8580);
  instr[ins].ptr[FTBL] = putprog(FTBL, &f);
}

static int improveinstr(int i, int tempo, char *err, int errsize)
{
  int role = roleof[i], n, gt;
  int changed = 0;
  INSTR *in;

  // what changes for this role?
  int drums = (modules & (1 << IM_DRUMS)) && (role >= RL_KICK) && (role <= RL_TOM);
  int full = (modules & (1 << IM_BASSFULL)) && (role == RL_BASS);
  int push = (modules & (1 << IM_BASSPUSH)) && (role == RL_BASS);
  int vib = (modules & (1 << IM_VIBRATO)) && (role == RL_LEAD) && (!instr[i].ptr[STBL]);
  int pwm = (modules & (1 << IM_PWM)) && ((role == RL_LEAD) || (role == RL_PAD) || (role == RL_ARP)) && haspulsewave(i) && pulsestatic(i);
  int bal = (modules & (1 << IM_BALANCE)) && ((role == RL_PAD) || (role == RL_ARP)) && ((instr[i].sr >> 4) >= 7);
  int fix = (modules & (1 << IM_FIX)) && ((instr[i].gatetimer & 0x3f) >= tempo) && (tempo >= 2);
  int adapt = chipadapt && instr[i].ptr[FTBL];

  if (!(drums || full || push || vib || pwm || bal || fix || adapt)) return 1;

  if (drums)
  {
    static const int map[] = {0, BR_KICK, BR_SNARE, BR_CHAT, BR_TOMM};
    int lev = (info[i].notes > 0) ? BL_NORMAL : BL_NORMAL;
    n = beat_newdrum(map[role], lev, drumkit, tempo);
    if (!n)
    {
      snprintf(err, errsize, "No room for a new drum sound");
      return 0;
    }
    if ((role == RL_KICK) && (strength >= 3)) instr[n].ad = (instr[n].ad & 0xf0) | 0x09;
    copyof[i] = n;
    rep("%02X %-15.15s %-8s -> %02X %s", i, instr[i].name, rolenames[role], n, instr[n].name);
    return 1;
  }

  n = makecopy(i, err, errsize);
  if (!n) return 0;
  in = &instr[n];
  gt = in->gatetimer;

  if (full || push)
  {
    TB w;
    int mw = 0x41, k;
    // main waveform of the original (triangle basses become pulse: more bass)
    if (in->ptr[WTBL])
    {
      getprog(WTBL, in->ptr[WTBL], &w);
      for (k = 0; k < w.n; k++)
        if (isrelwave(w.l[k]) && !(w.l[k] & 0x80)) { mw = w.l[k] | 0x01; break; }
    }
    if ((full) && (mw == 0x11)) mw = 0x41;
    w.n = 0;
    if (push)
    {
      if (strength >= 2) tadd(&w, 0x81, 0xc0 - 4 * (3 - strength));       // a click of noise
      tadd(&w, mw, 12);                                                    // octave pop
    }
    tadd(&w, mw, 0);
    tadd(&w, 0xff, 0);
    k = putprog(WTBL, &w);
    if (k) in->ptr[WTBL] = k;
    in->firstwave = 0x09;
    in->gatetimer &= ~0x80;                       // hard restart: every note starts clean
    if (push) in->ad = 0x00 | (strength >= 3 ? 0x08 : 0x09);
    if (full)
    {
      int s = in->sr >> 4;
      s += strength + 1;
      if (s > 0xd) s = 0xd;
      in->sr = (s << 4) | (in->sr & 0x0f);
      if ((mw & 0x40) && (pulsestatic(i) || !in->ptr[PTBL]))
      {
        int pp = pwmprog(n, 0x300, 0x300, 0x700, 0x30 - 8 * strength);
        if (pp) in->ptr[PTBL] = pp;
      }
    }
    changed = 1;
  }
  if (vib)
  {
    // note-independent vibrato: speed entry $8x (time), shift
    int shift = 6 - strength, pos;
    pos = makespeedtable(((0x80 | 4) << 8) | shift, MST_RAW, 0);
    if (pos >= 0)
    {
      in->ptr[STBL] = pos + 1;
      in->vibdelay = 0x18 - 4 * strength;
      changed = 1;
    }
  }
  if (pwm)
  {
    int pp = pwmprog(n, 0x400, 0x200, role == RL_PAD ? 0xa00 : 0x800, role == RL_PAD ? 0x60 : 0x40 - 8 * strength);
    if (pp) { in->ptr[PTBL] = pp; changed = 1; }
  }
  if (bal)
  {
    int s = (in->sr >> 4) - (strength + 1);
    if (s < 4) s = 4;
    in->sr = (s << 4) | (in->sr & 0x0f);
    changed = 1;
  }
  if (fix)
  {
    in->gatetimer = (in->gatetimer & 0xc0) | (tempo > 2 ? tempo - 1 : 1);
    rep("   fix: gate timer of %02X %.15s was %d, the tempo is %d", i, instr[i].name, gt & 0x3f, tempo);
    changed = 1;
  }
  if (adapt)
  {
    adaptfilter(n);
    changed = 1;
  }
  if (changed)
    rep("%02X %-15.15s %-8s -> %02X%s%s%s%s%s", i, instr[i].name, rolenames[role], n, full ? " fuller" : "", push ? " pushier" : "",
        vib ? " vibrato" : "", pwm ? " PWM" : "", bal ? " softer" : "");
  return 1;
}

// filter sweep for the bass copy where the filter of the SID is free
static void bassfilter(void)
{
  int i, s;
  sidfilteruse();
  for (s = 0; s < numsids; s++)
  {
    if (!filterfree[s]) continue;
    for (i = 1; i < MAX_INSTR - 1; i++)
    {
      int mask, n;
      TB f;
      if ((roleof[i] != RL_BASS) || (!copyof[i])) continue;
      mask = (info[i].chnmask >> (s * 3)) & 7;
      if (!mask) continue;
      n = copyof[i];
      f.n = 0;
      tadd(&f, 0x90, (0x60 + 0x20 * strength) | mask);          // low-pass, resonance
      tadd(&f, 0x00, editorInfo.sidmodel ? 0x50 : 0xa0);       // open
      tadd(&f, 0x0c, (-(2 + strength)) & 0xff);                  // close quickly
      tadd(&f, 0xff, 0);
      instr[n].ptr[FTBL] = putprog(FTBL, &f);
      filterfree[s] = 0;
      rep("   filter sweep: bass %02X on SID %d", n, s + 1);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Pattern changes
// ---------------------------------------------------------------------------

static void remapinstruments(void)
{
  int p, r;
  for (p = 0; p < MAX_PATT; p++)
  {
    if (!inscope(p)) continue;
    for (r = 0; r < pattlen[p]; r++)
    {
      int ins = pattern[p][r * 4 + 1];
      if ((ins > 0) && (ins < MAX_INSTR - 1) && (copyof[ins])) pattern[p][r * 4 + 1] = copyof[ins];
    }
  }
}

static int leadchannel(int c)
{
  int r, best = 0, tot = 0;
  for (r = 0; r < RL_NUM; r++) tot += chnnotes[c][r];
  best = chnnotes[c][RL_LEAD];
  return (tot > 0) && (best * 2 > tot);
}

static void countchannelroles(void)
{
  int c, d, r;
  memset(chnnotes, 0, sizeof chnnotes);
  for (c = 0; c < numchannels; c++)
  {
    int cur = 1;
    for (d = 0; d < songlen[sub][c]; d++)
    {
      int v = songorder[sub][c][d];
      if (v >= REPEAT) continue;
      for (r = 0; r < pattlen[v]; r++)
      {
        unsigned char *q = &pattern[v][r * 4];
        if (q[1]) cur = q[1];
        if (q[0] <= LASTNOTE) chnnotes[c][roleof[cur]]++;
      }
    }
  }
}

static int slides(void)
{
  static unsigned char done[MAX_PATT];
  int c, d, count = 0, pos, every = strength == 1 ? 3 : (strength == 2 ? 2 : 1), maxint = strength == 1 ? 2 : (strength == 2 ? 4 : 5);
  int mindur = strength == 1 ? 6 : (strength == 2 ? 4 : 3), k = 0;
  pos = makespeedtable((0x80 << 8) | (strength >= 3 ? 1 : 2), MST_RAW, 0);
  if (pos < 0) return 0;
  memset(done, 0, sizeof done);
  for (c = 0; c < numchannels; c++)
  {
    if (!leadchannel(c)) continue;
    for (d = 0; d < songlen[sub][c]; d++)
    {
      int p = songorder[sub][c][d], r, prev = -1, prevrow = -1, previns = 0;
      if ((p >= REPEAT) || (done[p])) continue;
      done[p] = 1;
      for (r = 0; r < pattlen[p]; r++)
      {
        unsigned char *q = &pattern[p][r * 4];
        if (q[0] == KEYOFF) { prev = -1; continue; }
        if (q[0] > LASTNOTE) continue;
        if ((prev >= 0) && (q[2] == 0) && (q[3] == 0) && ((q[1] == 0) || (q[1] == previns)) && (r - prevrow >= 2))
        {
          int interval = abs((int)q[0] - prev), next, dur;
          for (next = r + 1; next < pattlen[p]; next++)
            if ((pattern[p][next * 4] <= LASTNOTE) || (pattern[p][next * 4] == KEYOFF)) break;
          dur = next - r;
          if ((interval >= 1) && (interval <= maxint) && (dur >= mindur))
          {
            if ((k++ % every) == 0)
            {
              q[2] = CMD_TONEPORTA;
              q[3] = pos + 1;
              count++;
            }
          }
        }
        prev = q[0];
        prevrow = r;
        if (q[1]) previns = q[1];
      }
    }
  }
  return count;
}

// song row -> (orderlist entry, pattern, row) of a channel
typedef struct { int d, p, start, len, trans, rep; } OENT;

static int flatten(int c, OENT *e, int max)
{
  int d, n = 0, start = 0, trans = 0, rep = 0;
  for (d = 0; (d < songlen[sub][c]) && (n < max); d++)
  {
    int v = songorder[sub][c][d];
    if (v >= TRANSDOWN) { trans = v - TRANSUP; continue; }
    if (v >= REPEAT) { rep = v - REPEAT; continue; }
    e[n].d = d; e[n].p = v; e[n].start = start; e[n].len = pattlen[v] * (rep + 1); e[n].trans = trans; e[n].rep = rep;
    start += e[n].len;
    rep = 0;
    n++;
  }
  return n;
}

static int echo(int tempo)
{
  static OENT le[MAX_SONGLEN], ee[MAX_SONGLEN];
  static unsigned char own[MAX_SONGLEN];
  int lc = -1, ec = -1, c, nl, ne, k, delay, count = 0, echoins = 0, leadins = 0;
  int best = 0x7fffffff;
  // lead channel and the emptiest channel
  for (c = 0; c < numchannels; c++)
    if ((lc < 0) && (leadchannel(c))) lc = c;
  if (lc < 0) return 0;
  for (c = 0; c < numchannels; c++)
  {
    int r, tot = 0;
    if (c == lc) continue;
    for (r = 0; r < RL_NUM; r++) tot += chnnotes[c][r];
    if (tot < best) { best = tot; ec = c; }
  }
  if (ec < 0) return 0;
  {
    int leadnotes = 0, r;
    for (r = 0; r < RL_NUM; r++) leadnotes += chnnotes[lc][r];
    if (best * 8 > leadnotes) { rep("   echo: no free channel (the emptiest has %d notes)", best); return 0; }
  }
  delay = (12 + tempo / 2) / tempo;
  if (delay < 1) delay = 1;
  nl = flatten(lc, le, MAX_SONGLEN);
  ne = flatten(ec, ee, MAX_SONGLEN);
  memset(own, 0, sizeof own);
  // echo instrument: copy of the most used lead instrument, softer
  {
    int i, most = 0;
    for (i = 1; i < MAX_INSTR - 1; i++)
      if ((roleof[i] == RL_LEAD) && (info[i].notes > most)) { most = info[i].notes; leadins = i; }
    if (!leadins) return 0;
    echoins = freeinstr();
    if (!echoins) { rep("   echo: no free instrument slot"); return 0; }
    instr[echoins] = instr[copyof[leadins] ? copyof[leadins] : leadins];
    {
      int s = (instr[echoins].sr >> 4) / 2;
      if (s < 3) s = 3;
      instr[echoins].sr = (s << 4) | (instr[echoins].sr & 0x0f);
      instr[echoins].ptr[FTBL] = 0;
      memset(instr[echoins].name, 0, MAX_INSTRNAMELEN);
      snprintf(instr[echoins].name, MAX_INSTRNAMELEN, "Echo %.10s", instr[leadins].name);
    }
  }
  for (k = 0; k < nl; k++)
  {
    int r, rr;
    if (le[k].rep) continue;
    for (r = 0; r < pattlen[le[k].p]; r++)
    {
      unsigned char *q = &pattern[le[k].p][r * 4];
      int songrow = le[k].start + r + delay, j, er, free = 1, x;
      if ((q[0] > LASTNOTE) && (q[0] != KEYOFF)) continue;
      for (j = 0; j < ne; j++)
        if ((songrow >= ee[j].start) && (songrow < ee[j].start + ee[j].len)) break;
      if ((j >= ne) || (ee[j].rep)) continue;
      er = songrow - ee[j].start;
      // the echo channel must be quiet around this row
      for (x = er - 4; x <= er + 1; x++)
      {
        unsigned char *eq;
        if ((x < 0) || (x >= pattlen[ee[j].p])) continue;
        eq = &pattern[ee[j].p][x * 4];
        if ((eq[0] <= LASTNOTE) && (eq[1] != echoins)) free = 0;
        if ((eq[1]) && (eq[1] != echoins)) free = 0;
        if (eq[2]) free = 0;
      }
      if (!free) continue;
      // own pattern for this orderlist entry if it is used more than once
      if (!own[j])
      {
        int uses = 0, c2, d2;
        for (c2 = 0; c2 < numchannels; c2++)
          for (d2 = 0; d2 < songlen[sub][c2]; d2++)
            if (songorder[sub][c2][d2] == ee[j].p) uses++;
        if (uses > 1)
        {
          int np = clonepattern(ee[j].p);
          if (np < 0) continue;
          songorder[sub][ec][ee[j].d] = np;
          ee[j].p = np;
        }
        own[j] = 1;
      }
      rr = er;
      {
        unsigned char *eq = &pattern[ee[j].p][rr * 4];
        if (q[0] == KEYOFF) { if (eq[0] == REST) eq[0] = KEYOFF; continue; }
        x = q[0] + le[k].trans - ee[j].trans;
        if ((x < FIRSTNOTE) || (x > LASTNOTE)) continue;
        eq[0] = x;
        eq[1] = echoins;
        count++;
      }
    }
  }
  if (count) rep("   echo: %d notes of channel %d.%d on channel %d.%d, %d rows later (instrument %02X)", count, lc / 3 + 1, lc % 3 + 1,
                 ec / 3 + 1, ec % 3 + 1, delay, echoins);
  else memset(&instr[echoins], 0, sizeof(INSTR));
  return count;
}

// ---------------------------------------------------------------------------
// The whole improvement
// ---------------------------------------------------------------------------

static int improve(char *err, int errsize)
{
  int i, tempo, n;
  numreport = 0;
  memset(copyof, 0, sizeof copyof);
  memset(reserved, 0, sizeof reserved);
  sub = editorInfo.esnum;
  countpatternlengths();
  if (!songvalid(sub))
  {
    snprintf(err, errsize, "Subtune %02X is empty", sub);
    return 0;
  }
  n = ownpatterns();
  if (n < 0) { snprintf(err, errsize, "No free pattern for the copies of shared patterns"); return 0; }
  if (n) rep("%d pattern%s shared with other subtunes copied", n, n > 1 ? "s" : "");
  guessroles();
  countchannelroles();
  tempo = songtempo();
  for (i = 1; i < MAX_INSTR - 1; i++)
  {
    if (!info[i].used) continue;
    if (!improveinstr(i, tempo, err, errsize)) return 0;
  }
  if (modules & (1 << IM_FILTER)) bassfilter();
  remapinstruments();
  if (modules & (1 << IM_SLIDES))
  {
    n = slides();
    rep("   slides: %d toneportamento commands in the melody", n);
  }
  if (modules & (1 << IM_ECHO)) echo(tempo);
  if (modules & (1 << IM_CLEANUP))
  {
    optimizeeverything(1, 1);
    rep("   clean-up: unused instruments, patterns and table rows removed");
  }
  countpatternlengths();
  {
    int ins = 0;
    for (i = 1; i < MAX_INSTR - 1; i++)
      if (!instrempty(i)) ins++;
    rep("Result: %d instruments, %d patterns, tables W%d P%d F%d S%d (of 255)", ins, highestusedpattern + 1, gettablelen(0),
        gettablelen(1), gettablelen(2), gettablelen(3));
  }
  return 1;
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
  snprintf(out, outsize, "%s_before-improve_%04d-%02d-%02d_%02d%02d%s", base, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, ggt ? ".ggt" : ".sng");
  return writesongfile(out, ggt);
}

int improve_cli(int mods, int str, int kit, int chip, char *msg, int msgsize)
{
  int k, ok;
  char err[200] = "";
  modules = mods;
  strength = str < 1 ? 1 : (str > 3 ? 3 : str);
  drumkit = kit;
  chipadapt = chip;
  memset(roleset, 0, sizeof roleset);
  ok = improve(err, sizeof err);
  for (k = 0; k < numreport; k++) fprintf(stderr, "%s\n", report[k]);
  snprintf(msg, msgsize, "%s", ok ? "Improved" : err);
  return ok;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

static SNAP *orig = NULL, *better = NULL;
static int showing = 0;            // 0 original, 1 improved (in the song memory)
static int previewing = 0;

static void show(int which)
{
  snd_lock();
  snaprestore(which ? better : orig);
  countpatternlengths();
  snd_unlock();
  showing = which;
}

static int rebuild(char *err, int errsize)
{
  int ok;
  snd_lock();
  snaprestore(orig);
  ok = improve(err, errsize);
  snapsave(better);
  snaprestore(showing ? better : orig);
  countpatternlengths();
  snd_unlock();
  return ok;
}

static void stoppreview(void)
{
  if (!previewing) return;
  snd_lock();
  stopsong();
  snd_unlock();
  while (isplaying()) SDL_Delay(5);
  previewing = 0;
}

void improvedialog(void)
{
  int w = 116, h = 46;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2;
  int sel = 0, dirty = 1, ok = 0, scroll = 0, c;
  int list[MAX_INSTR], nlist = 0;
  char err[200] = "", msg[200] = "";
  enum { O_STRENGTH = 0, O_MODS = 1, O_KIT = O_MODS + IM_NUM, O_CHIP, O_NUM };
  enum { B_PLAY = 0, B_AB, B_APPLY, B_CANCEL, B_NUM };
  static const int btnw[B_NUM] = {12, 18, 9, 10};
  int btnx[B_NUM];

  stopsong();
  sub = editorInfo.esnum;
  countpatternlengths();
  if (!songvalid(sub))
  {
    setstatusmessage("Improve: the current subtune is empty");
    return;
  }
  orig = (SNAP *)malloc(sizeof(SNAP));
  better = (SNAP *)malloc(sizeof(SNAP));
  if ((!orig) || (!better))
  {
    free(orig);
    free(better);
    return;
  }
  snapsave(orig);
  showing = 0;
  memset(roleset, 0, sizeof roleset);
  guessroles();
  for (c = 1; c < MAX_INSTR - 1; c++)
    if (info[c].used) list[nlist++] = c;
  btnx[B_PLAY] = x0 + 3;
  btnx[B_AB] = x0 + 17;
  btnx[B_APPLY] = x0 + w - 24;
  btnx[B_CANCEL] = x0 + w - 13;
  key = rawkey = 0;

  for (;;)
  {
    char buf[200];
    int hit = -1, hitbtn = -1, nsel, ly = y0 + 3, oy, ry, vis = 9;
    const char *bt[B_NUM];
    if (dirty)
    {
      ok = rebuild(err, sizeof err);
      dirty = 0;
    }
    nsel = nlist + O_NUM + B_NUM;
    bt[B_PLAY] = previewing ? " " GS_STOP " Stop     " : " " GS_PLAY " Play     ";
    bt[B_AB] = showing ? " Hearing: IMPROVED " : " Hearing: ORIGINAL ";
    bt[B_APPLY] = "  Apply  ";
    bt[B_CANCEL] = "  Cancel  ";

    printstatus();
    dlg_drawframe(x0, y0, w, h, "Improve");
    sprintf(buf, "Subtune %02X - instruments and their roles (LEFT/RIGHT to change):", sub);
    printtext(x0 + 3, y0 + 2, MKCOL(14, C_PANEL), buf);
    if (sel < nlist)
    {
      if (sel < scroll) scroll = sel;
      if (sel >= scroll + vis) scroll = sel - vis + 1;
    }
    for (c = 0; c < vis; c++)
    {
      int k = scroll + c, isel = (sel == k), bg = isel ? C_SELECT : C_PANEL, y = ly + c;
      printblankc(x0 + 2, y, MKCOL(8, bg), w - 4);
      if (k >= nlist) continue;
      {
        int i = list[k];
        char nm[MAX_INSTRNAMELEN + 1];
        memcpy(nm, instr[i].name, MAX_INSTRNAMELEN);
        nm[MAX_INSTRNAMELEN] = 0;
        if (showing) memcpy(nm, orig->instr[i].name, MAX_INSTRNAMELEN);
        sprintf(buf, "%02X %-16s " GS_PREV " %-12s " GS_NEXT "  %d notes%s", i, nm, rolenames[roleof[i]], info[i].notes, roleset[i] ? "  (set)" : "");
        printtext(x0 + 3, y, MKCOL(isel ? 15 : 8, bg), buf);
        if ((mousey == y) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hit = k;
      }
    }
    if (nlist > vis) printtext(x0 + w - 12, ly + vis, MKCOL(3, C_PANEL), "(scroll)");
    oy = ly + vis + 1;
    for (c = 0; c < O_NUM; c++)
    {
      int s = nlist + c, isel = (sel == s), bg = isel ? C_SELECT : C_PANEL, y = oy + c;
      if (c == O_STRENGTH) sprintf(buf, "Strength       " GS_PREV " %s " GS_NEXT, strength == 1 ? "Subtle" : (strength == 2 ? "Medium" : "Strong"));
      else if (c == O_KIT) sprintf(buf, "Drum sounds    " GS_PREV " %s " GS_NEXT, kitnames[drumkit]);
      else if (c == O_CHIP) sprintf(buf, "SID chip       " GS_PREV " %s " GS_NEXT, chipadapt == 0 ? "Keep the filter values" : (chipadapt == 1 ? "Written for the 6581: adapt the filters to the 8580" : "Written for the 8580: adapt the filters to the 6581"));
      else sprintf(buf, "[%c] %s", modules & (1 << (c - O_MODS)) ? 'x' : ' ', modnames[c - O_MODS]);
      printblankc(x0 + 2, y, MKCOL(8, bg), 70);
      printtext(x0 + 3, y, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == y) && (mousex >= x0 + 2) && (mousex < x0 + 72)) hit = s;
    }
    ry = oy + O_NUM + 1;
    printtext(x0 + 3, ry, MKCOL(14, C_PANEL), ok ? "What changes:" : "Cannot improve:");
    if (!ok) printtext(x0 + 5, ry + 1, MKCOL(10, C_PANEL), err);
    else
      for (c = 0; c < numreport && c < h - (ry - y0) - 6; c++)
        printtext(x0 + 5, ry + 1 + c, MKCOL(strncmp(report[c], "Result", 6) ? 8 : 13, C_PANEL), report[c]);
    if (msg[0]) printtext(x0 + 3, y0 + h - 4, MKCOL(10, C_PANEL), msg);
    printtext(x0 + 3, y0 + h - 3, MKCOL(3, C_PANEL), "F1 play/stop, SPACE original <-> improved (A/B), ENTER apply (backup + one undo step), ESC cancel");
    for (c = 0; c < B_NUM; c++)
    {
      int isel = (sel == nlist + O_NUM + c);
      printtext(btnx[c], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : (c == B_AB && showing ? C_SELECT : C_BUTTON)), bt[c]);
      if ((mousey == y0 + h - 2) && (mousex >= btnx[c]) && (mousex < btnx[c] + btnw[c] + 8)) hitbtn = c;
    }

    fliptoscreen();
    getkey();
    if (win_quitted) break;
    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn >= 0) { sel = nlist + O_NUM + hitbtn; rawkey = KEY_ENTER; }
      else if (hit >= 0)
      {
        sel = hit;
        rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        if (ui_arrowat(mousex, mousey) < 0) rawkey = KEY_LEFT;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h)) break;
    }
    msg[0] = 0;
    {
      int action = -1;
      if (rawkey == KEY_F1) action = B_PLAY;
      if (rawkey == KEY_SPACE) action = B_AB;
      switch (rawkey)
      {
        case KEY_ESC: goto cancel;
        case KEY_UP: if (sel > 0) sel--; break;
        case KEY_DOWN: case KEY_TAB: if (sel < nsel - 1) sel++; break;
        case KEY_PGUP: sel = sel > 10 ? sel - 10 : 0; break;
        case KEY_PGDN: sel = sel + 10 < nsel ? sel + 10 : nsel - 1; break;
        case KEY_LEFT:
        case KEY_RIGHT:
        {
          int dir = rawkey == KEY_LEFT ? -1 : 1;
          if (sel < nlist)
          {
            int i = list[sel];
            roleof[i] = (roleof[i] + dir + RL_NUM) % RL_NUM;
            roleset[i] = 1;
            dirty = 1;
          }
          else if (sel < nlist + O_NUM)
          {
            int o = sel - nlist;
            if (o == O_STRENGTH) strength = (strength - 1 + dir + 3) % 3 + 1;
            else if (o == O_KIT) drumkit = (drumkit + dir + KIT_NUM) % KIT_NUM;
            else if (o == O_CHIP) chipadapt = (chipadapt + dir + 3) % 3;
            else modules ^= 1 << (o - O_MODS);
            dirty = 1;
          }
          else
          {
            sel += dir;
            if (sel < nlist + O_NUM) sel = nlist + O_NUM;
            if (sel >= nsel) sel = nsel - 1;
          }
        }
        break;
        case KEY_ENTER:
        if (sel >= nlist + O_NUM) action = sel - nlist - O_NUM;
        else if (sel >= nlist + O_MODS && sel < nlist + O_KIT) { modules ^= 1 << (sel - nlist - O_MODS); dirty = 1; }
        else action = B_APPLY;
        break;
      }
      switch (action)
      {
        case B_PLAY:
        if (previewing) stoppreview();
        else
        {
          initsong(sub, PLAY_BEGINNING);
          previewing = 1;
        }
        break;
        case B_AB:
        if (ok) show(!showing);
        break;
        case B_APPLY:
        if (!ok) break;
        {
          char bak[MAX_FILENAME + 64];
          void *ed;
          stoppreview();
          show(0);
          if (!writebackup(bak, sizeof bak))
          {
            snprintf(msg, sizeof msg, "Could not write the backup %s - nothing was changed", bak);
            break;
          }
          ed = undoinstrbegin();
          undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
          {
            int s2;
            for (s2 = 0; s2 < MAX_SONGS; s2++)
              for (c = 0; c < numchannels; c++)
                undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c + s2 * MAX_CHN, UNDO_AREA_DIRTY_CHECK);
          }
          undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
          show(1);
          undoinstrend(ed);
          songmodified = 1;
          snprintf(buf, sizeof buf, "Subtune %02X improved. Backup: %s", sub, bak);
          setstatusmessage(buf);
          goto done;
        }
        case B_CANCEL:
        goto cancel;
      }
    }
  }
cancel:
  stoppreview();
  show(0);
done:
  stoppreview();
  free(orig);
  free(better);
  orig = better = NULL;
  key = rawkey = 0;
  printmainscreen();
}

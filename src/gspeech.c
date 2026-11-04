//
// C64 GrooveGoat - speech generator
//
// English text -> phonemes (CMU Pronouncing Dictionary + letter rules) ->
// formant synthesis on ONE SID voice, written as GoatTracker data:
//
// - one instrument per spoken sound (legato: no gate off, no hard restart)
// - wavetable: waveform per frame (pulse / sawtooth for voiced sounds, noise
//   for hissing sounds, silence for the closure of P, T, K ...)
// - pulsetable: pulse width per frame (shapes the low formant F1)
// - filtertable: filter mode, resonance and cutoff per frame (the resonant
//   peak follows the second formant F2, which tells the vowels apart). Only
//   the speaking voice is routed through the filter.
// - pattern: one row with note + instrument per sound, the following rows
//   empty, a key off at the end. The tables work with 1-frame (20 ms) steps,
//   the sounds start on the pattern rows (3 frames = 60 ms with tempo 3).
//
// Formant values: average adult male voice (Peterson & Barney, Klatt).
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include "goattrk2.h"
#include "gres.h"
#include "gspeech.h"

// ---------------------------------------------------------------------------
// Phoneme inventory (ARPAbet, same order as tools/mkspeechdict.py)
// ---------------------------------------------------------------------------

enum { PV, PD, PG, PN, PFU, PFV, PSU, PSV, PAF, PH, PSIL };

typedef struct
{
  const char *name;
  int type;
  int f1, f2;        // formants (Hz); for consonants: locus / noise band
  int f1b, f2b;      // end formants of diphthongs
  int dur;           // duration (ms) when stressed
  int noise;         // fricatives / bursts: noise band (Hz), 0 = none
} PHON;

#define NUMPHON 39
static const PHON phon[NUMPHON] = {
  {"AA", PV, 730, 1090, 0, 0, 170, 0},
  {"AE", PV, 660, 1720, 0, 0, 170, 0},
  {"AH", PV, 640, 1190, 0, 0, 140, 0},
  {"AO", PV, 570, 840, 0, 0, 170, 0},
  {"AW", PD, 730, 1090, 320, 870, 230, 0},
  {"AY", PD, 730, 1090, 300, 2150, 230, 0},
  {"B", PSV, 300, 900, 0, 0, 70, 1200},
  {"CH", PAF, 300, 1800, 0, 0, 130, 2600},
  {"D", PSV, 300, 1700, 0, 0, 70, 3500},
  {"DH", PFV, 350, 1600, 0, 0, 70, 3800},
  {"EH", PV, 530, 1840, 0, 0, 140, 0},
  {"ER", PV, 490, 1350, 0, 0, 160, 0},
  {"EY", PD, 530, 1840, 300, 2250, 200, 0},
  {"F", PFU, 0, 1100, 0, 0, 110, 6500},
  {"G", PSV, 300, 1900, 0, 0, 70, 2200},
  {"HH", PH, 0, 0, 0, 0, 70, 0},
  {"IH", PV, 390, 1990, 0, 0, 110, 0},
  {"IY", PV, 270, 2290, 0, 0, 150, 0},
  {"JH", PAF, 300, 1800, 0, 0, 120, 2600},
  {"K", PSU, 300, 1900, 0, 0, 90, 2200},
  {"L", PG, 380, 880, 0, 0, 80, 0},
  {"M", PN, 280, 1100, 0, 0, 90, 0},
  {"N", PN, 280, 1700, 0, 0, 80, 0},
  {"NG", PN, 280, 2300, 0, 0, 90, 0},
  {"OW", PD, 570, 840, 320, 900, 200, 0},
  {"OY", PD, 570, 840, 300, 2150, 240, 0},
  {"P", PSU, 300, 900, 0, 0, 90, 1200},
  {"R", PG, 420, 1300, 0, 0, 80, 0},
  {"S", PFU, 0, 1800, 0, 0, 120, 5500},
  {"SH", PFU, 0, 1900, 0, 0, 120, 2600},
  {"T", PSU, 300, 1800, 0, 0, 90, 4000},
  {"TH", PFU, 0, 1500, 0, 0, 100, 5000},
  {"UH", PV, 440, 1020, 0, 0, 110, 0},
  {"UW", PV, 300, 870, 0, 0, 150, 0},
  {"V", PFV, 300, 1100, 0, 0, 80, 3500},
  {"W", PG, 300, 610, 0, 0, 80, 0},
  {"Y", PG, 280, 2250, 0, 0, 80, 0},
  {"Z", PFV, 300, 1700, 0, 0, 100, 5000},
  {"ZH", PFV, 300, 1900, 0, 0, 90, 2600},
};

#define PSILENCE 255

typedef struct
{
  int ph;            // index into phon[], PSILENCE = pause
  int stress;        // 0 unstressed, 1 primary, 2 secondary
  int word;          // word number
  int wordend;       // last phoneme of a word
  int frames;        // duration in frames (20 ms)
  int note;          // pattern note
  int start;         // first frame
} PHONE;

#define MAXPHONES 128
static PHONE phones[MAXPHONES];
static int numphones;

static int phonindex(const char *name)
{
  int c;
  for (c = 0; c < NUMPHON; c++)
    if (!strcmp(phon[c].name, name)) return c;
  return -1;
}

// ---------------------------------------------------------------------------
// Dictionary (res/speech/speechdict.bin, built from the CMU Pronouncing
// Dictionary by tools/mkspeechdict.py)
// ---------------------------------------------------------------------------

static int dictlookup(const char *word, unsigned char *codes, int max)
{
  int len = 0;
  const unsigned char *d = res_getfile("speechdict.bin", &len);
  const unsigned char *end;
  char cur[64];
  int target = 0;
  if (!d) return -1;
  end = d + len;
  cur[0] = 0;
  while (d < end)
  {
    int pre = d[0] >> 4, sl = d[0] & 15, n, cmp;
    memcpy(cur + pre, d + 1, sl);
    cur[pre + sl] = 0;
    d += 1 + sl;
    n = *d++;
    cmp = strcmp(cur, word);
    if (!cmp)
    {
      if (n > max) n = max;
      memcpy(codes, d, n);
      return n;
    }
    if (cmp > 0) return -1;
    d += n;
    (void)target;
  }
  return -1;
}

// Very small letter-to-sound fallback for words that are not in the
// dictionary (names, made-up words). Digraphs first, then single letters.
static int letterrules(const char *w, unsigned char *codes, int max)
{
  static const struct { const char *g; const char *p; } rules[] = {
    {"tch", "CH"}, {"sch", "S K"}, {"igh", "AY1"}, {"ough", "AO1"}, {"tion", "SH AH0 N"},
    {"th", "TH"}, {"sh", "SH"}, {"ch", "CH"}, {"ph", "F"}, {"ng", "NG"}, {"ck", "K"}, {"qu", "K W"},
    {"wh", "W"}, {"kn", "N"}, {"wr", "R"}, {"ee", "IY1"}, {"ea", "IY1"}, {"oo", "UW1"}, {"ou", "AW1"},
    {"ow", "OW1"}, {"oa", "OW1"}, {"ai", "EY1"}, {"ay", "EY1"}, {"ei", "EY1"}, {"ey", "IY0"}, {"oi", "OY1"},
    {"oy", "OY1"}, {"au", "AO1"}, {"aw", "AO1"}, {"ie", "IY1"}, {"er", "ER0"}, {"ar", "AA1 R"}, {"or", "AO1 R"},
    {"ir", "ER1"}, {"ur", "ER1"},
    {"a", "AE1"}, {"b", "B"}, {"c", "K"}, {"d", "D"}, {"e", "EH1"}, {"f", "F"}, {"g", "G"}, {"h", "HH"},
    {"i", "IH1"}, {"j", "JH"}, {"k", "K"}, {"l", "L"}, {"m", "M"}, {"n", "N"}, {"o", "AA1"}, {"p", "P"},
    {"q", "K"}, {"r", "R"}, {"s", "S"}, {"t", "T"}, {"u", "AH1"}, {"v", "V"}, {"w", "W"}, {"x", "K S"},
    {"y", "Y"}, {"z", "Z"}, {NULL, NULL}};
  int n = 0, pos = 0, wl = strlen(w);
  while ((pos < wl) && (n < max))
  {
    int r, matched = 0;
    // final silent e
    if ((w[pos] == 'e') && (pos == wl - 1) && (pos > 1)) break;
    // y at the end: "IY"
    if ((w[pos] == 'y') && (pos == wl - 1) && (pos > 0))
    {
      codes[n++] = phonindex("IY") * 3;
      break;
    }
    // double consonants once
    if ((pos > 0) && (w[pos] == w[pos - 1]) && (!strchr("aeiou", w[pos]))) { pos++; continue; }
    if (w[pos] == '\'') { pos++; continue; }
    for (r = 0; rules[r].g; r++)
    {
      int gl = strlen(rules[r].g);
      if (!strncmp(w + pos, rules[r].g, gl))
      {
        char buf[32], *tok;
        strcpy(buf, rules[r].p);
        for (tok = strtok(buf, " "); tok && (n < max); tok = strtok(NULL, " "))
        {
          char nm[4] = {0};
          int s = 0, l = strlen(tok), pi;
          if (isdigit((unsigned char)tok[l - 1])) { s = tok[l - 1] - '0'; l--; }
          memcpy(nm, tok, l < 3 ? l : 3);
          pi = phonindex(nm);
          if (pi >= 0) codes[n++] = pi * 3 + s;
        }
        pos += gl;
        matched = 1;
        break;
      }
    }
    if (!matched) pos++;
  }
  // only one primary stress: keep the first
  {
    int c, seen = 0;
    for (c = 0; c < n; c++)
    {
      if ((codes[c] % 3 == 1) && (phon[codes[c] / 3].type <= PD))
      {
        if (seen) codes[c] -= 1;
        seen = 1;
      }
    }
  }
  return n;
}

static int addphone(int ph, int stress, int word)
{
  if (numphones >= MAXPHONES) return 0;
  memset(&phones[numphones], 0, sizeof(PHONE));
  phones[numphones].ph = ph;
  phones[numphones].stress = stress;
  phones[numphones].word = word;
  numphones++;
  return 1;
}

// Parse text (or /phonemes/) into phones[]
static int parsetext(const char *text, char *err, int errsize)
{
  int word = 0;
  const char *s = text;
  numphones = 0;
  while (*s)
  {
    if (*s == '/')
    {
      // phonemes: /T AH0 G EH1 DH ER0/
      s++;
      while ((*s) && (*s != '/'))
      {
        char nm[8];
        int l = 0, stress = 0, pi;
        while ((*s == ' ') || (*s == ',')) s++;
        if ((*s == '/') || (!*s)) break;
        if (*s == '|')
        {
          if (numphones) phones[numphones - 1].wordend = 1;
          word++;
          s++;
          continue;
        }
        while ((*s) && (*s != ' ') && (*s != '/') && (*s != '|') && (l < 6)) nm[l++] = toupper((unsigned char)*s++);
        nm[l] = 0;
        if ((l) && (isdigit((unsigned char)nm[l - 1]))) { stress = nm[l - 1] - '0'; nm[--l] = 0; }
        pi = phonindex(nm);
        if (pi < 0)
        {
          snprintf(err, errsize, "Unknown phoneme \"%s\"", nm);
          return 0;
        }
        if ((phon[pi].type <= PD) && (!isdigit((unsigned char)s[-1])) && (stress == 0)) stress = 1;
        if (!addphone(pi, stress, word)) break;
      }
      if (numphones) phones[numphones - 1].wordend = 1;
      word++;
      if (*s == '/') s++;
      continue;
    }
    if (isalpha((unsigned char)*s) || (*s == '\''))
    {
      char w[64];
      unsigned char codes[64];
      int l = 0, n, c;
      while ((isalpha((unsigned char)*s) || (*s == '\'')) && (l < 63)) w[l++] = tolower((unsigned char)*s++);
      w[l] = 0;
      n = dictlookup(w, codes, 64);
      if (n <= 0) n = letterrules(w, codes, 64);
      for (c = 0; c < n; c++)
        addphone(codes[c] / 3, codes[c] % 3, word);
      if (numphones) phones[numphones - 1].wordend = 1;
      word++;
      continue;
    }
    if (isdigit((unsigned char)*s))
    {
      static const char *digits[10] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
      unsigned char codes[32];
      int n = dictlookup(digits[*s - '0'], codes, 32), c;
      for (c = 0; c < n; c++) addphone(codes[c] / 3, codes[c] % 3, word);
      if (numphones) phones[numphones - 1].wordend = 1;
      word++;
      s++;
      continue;
    }
    if ((*s == ',') || (*s == '.') || (*s == '!') || (*s == '?') || (*s == ';') || (*s == ':') || (*s == '-'))
    {
      // pause
      if ((numphones) && (phones[numphones - 1].ph != PSILENCE))
        addphone(PSILENCE, (*s == ',') || (*s == '-') ? 0 : 1, word);
    }
    s++;
  }
  // no pause at the end
  while ((numphones) && (phones[numphones - 1].ph == PSILENCE)) numphones--;
  if (!numphones)
  {
    snprintf(err, errsize, "Nothing to say");
    return 0;
  }
  if (numphones >= MAXPHONES)
  {
    snprintf(err, errsize, "The text is too long (at most %d sounds)", MAXPHONES);
    return 0;
  }
  return numphones;
}

int speech_phonemes(const char *text, char *out, int outsize, char *err, int errsize)
{
  int c, n;
  out[0] = 0;
  n = parsetext(text, err, errsize);
  for (c = 0; c < n; c++)
  {
    char buf[16];
    if (phones[c].ph == PSILENCE) strcpy(buf, phones[c].stress ? ". " : ", ");
    else if (phon[phones[c].ph].type <= PD) sprintf(buf, "%s%d ", phon[phones[c].ph].name, phones[c].stress);
    else sprintf(buf, "%s ", phon[phones[c].ph].name);
    if ((phones[c].wordend) && (c < n - 1) && (phones[c + 1].ph != PSILENCE)) strcat(buf, "| ");
    if (strlen(out) + strlen(buf) < (size_t)outsize) strcat(out, buf);
  }
  return n;
}

// ---------------------------------------------------------------------------
// Synthesis: per frame parameters
// ---------------------------------------------------------------------------

#define MAXFRAMES 2048
#define W_SILENT 0xe1     // gate on, no waveform
#define W_NOISE 0x81

typedef struct
{
  unsigned char wave;     // waveform register value (or W_SILENT)
  unsigned char note;     // wavetable right side: 0x00 = pattern note, 0x81-0xdf absolute
  int pulse;              // 12 bit
  unsigned char ftype;    // 0x10 LP, 0x20 BP, 0x40 HP (combinations)
  unsigned char res;      // 0-15
  int cutoff;             // 0-255
} FRAME;

static FRAME frames[MAXFRAMES];
static int numframes;
static int f1t[MAXFRAMES], f2t[MAXFRAMES];   // formant tracks (Hz)

// 8580: cutoff register (8 bit) ~ 50 Hz per step up to 6.5 kHz (reSID curve)
static int hz2cutoff(int hz)
{
  static const int pts[][2] = {{0, 0}, {16, 800}, {32, 1600}, {48, 2500}, {64, 3300}, {80, 4100}, {96, 4800},
                               {112, 5600}, {128, 6500}, {144, 7500}, {160, 8400}, {176, 9200}, {192, 9800},
                               {208, 10500}, {224, 11000}, {240, 11700}, {255, 12500}};
  int c;
  // measured 8580 band pass peaks lie ~12% below the nominal curve up to 3.5 kHz
  if (hz < 3500) hz = hz * 100 / 88;
  for (c = 1; c < (int)(sizeof pts / sizeof pts[0]); c++)
  {
    if (hz <= pts[c][1])
    {
      int v = pts[c - 1][0] + (hz - pts[c - 1][1]) * (pts[c][0] - pts[c - 1][0]) / (pts[c][1] - pts[c - 1][1]);
      return v < 1 ? 1 : v;
    }
  }
  return 255;
}

// Voices: waveform, formant scale (shorter vocal tract = higher formants),
// pitch, melody
enum { VW_PULSE, VW_SAW, VW_NOISE, VW_METAL };
typedef struct
{
  const char *name;
  int wave;           // VW_*
  int formant;        // percent
  int note;           // default pitch (A-2 = 33)
  int monotone;
} VOICE;

static const VOICE voices[] = {
  {"Man", VW_PULSE, 100, 33, 0},
  {"Woman", VW_PULSE, 117, 44, 0},
  {"Child", VW_PULSE, 130, 50, 0},
  {"Giant (deep)", VW_SAW, 88, 26, 0},
  {"Robot", VW_METAL, 100, 31, 1},
  {"Bright (sawtooth)", VW_SAW, 100, 33, 0},
  {"Whisper", VW_NOISE, 105, 33, 0},
};
#define NUMVOICES ((int)(sizeof voices / sizeof voices[0]))

int speech_numvoices(void) { return NUMVOICES; }
const char *speech_voicename(int v) { return voices[v >= 0 && v < NUMVOICES ? v : 0].name; }
int speech_voicenote(int v) { return voices[v >= 0 && v < NUMVOICES ? v : 0].note; }

static const VOICE *curvoice;
static int fscale(int hz) { return hz * curvoice->formant / 100; }

static int notefreq(int note)
{
  return (int)(440.0 * pow(2.0, (note - 57) / 12.0));
}

static int noisenote(int hz)
{
  // the SID noise is only white when it runs fast: at low notes it becomes
  // a crackle of big steps (a "wooden" knock). Always a high noise note; the
  // filter makes the band.
  int n = 86 + hz / 1500;
  if (n > 95) n = 95;
  return 0x80 + n;
}

// Voiced frame: waveform and filter from the formants
static void voiced(FRAME *f, int f1, int f2, int note, const SPEECHPARAMS *p, int fr)
{
  int f0 = notefreq(note);
  double d;
  memset(f, 0, sizeof *f);
  f->wave = curvoice->wave == VW_SAW ? 0x21 : (curvoice->wave == VW_NOISE ? W_NOISE : 0x41);
  if (curvoice->wave == VW_METAL) f->wave = editorInfo.sidmodel ? 0x61 : 0x41;     // pulse+saw: metallic (8580)
  f->note = curvoice->wave == VW_NOISE ? noisenote(f2) : 0x00;
  // pulse width: emphasize the harmonics around F1 (first spectral peak of a
  // narrow pulse at 1/(2*duty) times the fundamental)
  d = (double)f0 / (2.0 * (f1 > 150 ? f1 : 150));
  if (d < 0.05) d = 0.05;
  if (d > 0.45) d = 0.45;
  f->pulse = (int)(d * 4096);
  f->res = 15;
  switch (p->mode)
  {
    default:
    case 11:  // lowpass + bandpass at F2: low harmonics pass (F1 region), resonant peak at F2
    f->ftype = 0x30;
    f->cutoff = hz2cutoff(f2);
    break;
    case 1:   // bandpass at F2
    f->ftype = 0x20;
    f->cutoff = hz2cutoff(f2);
    break;
    case 2:   // bandpass at F1, pulse width from F2
    f->ftype = 0x20;
    f->cutoff = hz2cutoff(f1);
    d = (double)f0 / (2.0 * f2);
    if (d < 0.03) d = 0.03;
    f->pulse = (int)(d * 4096);
    break;
    case 8:   // bandpass + highpass at F1, pulse width from F2
    case 9:   // bandpass at F1, sawtooth
    case 10:
    case 0:   // default: bandpass at F1 (resonance $F), the pulse width follows
              // F2 (narrow pulse = bright front vowel), sawtooth for back vowels
    f->ftype = p->mode == 8 ? 0x60 : 0x20;
    f->cutoff = hz2cutoff(f1);
    d = (double)f0 / (2.0 * f2);
    if (d < 0.03) d = 0.03;
    f->pulse = (int)(d * 4096);
    if (p->mode == 9) f->wave = 0x21;
    if ((p->mode == 10 || p->mode == 0) && (f2 < fscale(1100)) && (curvoice->wave == VW_PULSE)) f->wave = 0x21;
    break;
    case 3:   // bandpass, alternating F1 / F2 on every frame
    f->ftype = 0x20;
    f->cutoff = hz2cutoff((fr & 1) ? f1 : f2);
    break;
    case 4:   // lowpass at F2, sawtooth
    f->wave = 0x21;
    f->ftype = 0x10;
    f->cutoff = hz2cutoff(f2);
    break;
    case 5:   // timbre: waveform from F2 (dark back vowels, bright front vowels),
              // pulse width from F1, lowpass+bandpass at F2
    case 6:   // same with bandpass
    case 7:   // same with lowpass
    if (f2 < 1000) f->wave = 0x11;            // triangle: u, o
    else if (f2 < 1600) f->wave = 0x21;       // sawtooth: a, er
    else f->wave = 0x41;                      // narrow pulse: e, i
    f->ftype = p->mode == 5 ? 0x30 : (p->mode == 6 ? 0x20 : 0x10);
    f->cutoff = hz2cutoff(f2);
    break;
  }
  if (curvoice->wave == VW_NOISE)
  {
    f->ftype = 0x20;
    f->res = 12;
    f->cutoff = hz2cutoff(f2);
  }
}

static void noiseframe(FRAME *f, int hz, int ftype, int res)
{
  memset(f, 0, sizeof *f);
  f->wave = W_NOISE;
  f->note = noisenote(hz);
  f->pulse = 0x800;
  f->ftype = ftype;
  f->res = res;
  f->cutoff = hz2cutoff(hz);
}

static void silentframe(FRAME *f, const FRAME *prev)
{
  if (prev) *f = *prev;
  else memset(f, 0, sizeof *f);
  f->wave = W_SILENT;
}

// next / previous voiced (vowel-like) phone for formant targets
static int nextvowel(int i)
{
  for (i++; i < numphones; i++)
  {
    if (phones[i].ph == PSILENCE) return -1;
    if (phon[phones[i].ph].type <= PG) return i;
  }
  return -1;
}

static void durations(const SPEECHPARAMS *p)
{
  int c;
  for (c = 0; c < numphones; c++)
  {
    int ms;
    if (phones[c].ph == PSILENCE) ms = phones[c].stress ? 300 : 160;
    else
    {
      const PHON *ph = &phon[phones[c].ph];
      ms = ph->dur;
      if (ph->type <= PD)
      {
        if (phones[c].stress == 0) ms = ms * 55 / 100;
        else if (phones[c].stress == 2) ms = ms * 80 / 100;
        // phrase final lengthening
        if ((phones[c].wordend) || ((c + 1 < numphones) && (phones[c + 1].wordend) && (phon[phones[c + 1].ph].type > PG)))
          ms = ms * 120 / 100;
      }
    }
    phones[c].frames = ms * p->speed / 100;     // milliseconds until timeline()
  }
}

// Player frames per second: 50 (PAL) times the speed multiplier, 25 at 0
static int framespersec(void)
{
  return editorInfo.multiplier ? 50 * editorInfo.multiplier : 25;
}

// Timing: the sounds keep their natural length (in player frames); the
// pattern only decides where a new instrument can start. The speech is cut
// into segments at the rows in which sounds begin, so it sounds the same at
// every song speed (F02 ... F10); at slow speeds one instrument may hold
// several short sounds.
typedef struct
{
  int start, end;     // frames
  int first, last;    // phones
  int row;            // pattern row (relative)
} SEG;

static SEG segs[MAXPHONES];
static int numsegs;

static void timeline(const SPEECHPARAMS *p)
{
  int c, t = 0, fps = framespersec(), tempo = p->tempo;
  for (c = 0; c < numphones; c++)
  {
    int f = (phones[c].frames * fps + 500) / 1000;     // phones[c].frames holds ms here
    if (f < 1) f = 1;
    phones[c].start = t;
    phones[c].frames = f;
    t += f;
  }
  // the speech ends on a row boundary
  if (t % tempo) phones[numphones - 1].frames += tempo - t % tempo;
  numframes = phones[numphones - 1].start + phones[numphones - 1].frames;

  numsegs = 0;
  for (c = 0; c < numphones; c++)
  {
    int r = (phones[c].start + tempo / 2) / tempo;
    if (c == 0) r = 0;
    if ((numsegs) && (r <= segs[numsegs - 1].row))
    {
      segs[numsegs - 1].last = c;
      continue;
    }
    if (r * tempo >= numframes) { segs[numsegs - 1].last = c; continue; }
    segs[numsegs].row = r;
    segs[numsegs].start = r * tempo;
    segs[numsegs].first = segs[numsegs].last = c;
    numsegs++;
  }
  for (c = 0; c < numsegs; c++)
    segs[c].end = c + 1 < numsegs ? segs[c + 1].start : numframes;
}

static void segname(const SEG *sg, char *buf, int size)
{
  int c;
  buf[0] = 0;
  for (c = sg->first; c <= sg->last; c++)
  {
    const char *n = phones[c].ph == PSILENCE ? "-" : phon[phones[c].ph].name;
    if ((int)(strlen(buf) + strlen(n) + 2) > size) break;
    if (buf[0]) strcat(buf, " ");
    strcat(buf, n);
  }
}

// Intonation: one note per phone
static void intonation(const SPEECHPARAMS *p)
{
  int c, lastword = phones[numphones - 1].word;
  for (c = 0; c < numphones; c++)
  {
    int n = p->basenote;
    if (curvoice->monotone)
    {
      phones[c].note = n;
      continue;
    }
    int isv = (phones[c].ph != PSILENCE) && (phon[phones[c].ph].type <= PD);
    if ((isv) && (phones[c].stress == 1)) n += (p->melody == 3) ? 3 : (p->melody ? 2 : 0);
    if (phones[c].word == lastword)
    {
      if (p->melody == 1) n -= (isv && phones[c].stress == 1) ? 1 : 3;
      if (p->melody == 2) n += (isv && phones[c].stress == 1) ? 4 : 1;
    }
    if (n < 12) n = 12;
    if (n > 84) n = 84;
    phones[c].note = n;
  }
  // consonants before a vowel take its note (smooth pitch)
  for (c = numphones - 2; c >= 0; c--)
  {
    if ((phones[c].ph != PSILENCE) && (phon[phones[c].ph].type > PD) && (phones[c + 1].ph != PSILENCE) &&
        (phones[c].word == phones[c + 1].word))
      phones[c].note = phones[c + 1].note;
  }
}

// Formant tracks: anchors per voiced phone, linear in between
static void formanttracks(void)
{
  int ax[MAXPHONES * 3], a1[MAXPHONES * 3], a2[MAXPHONES * 3];
  int na = 0, c, f;
  for (c = 0; c < numphones; c++)
  {
    const PHON *ph;
    int s = phones[c].start, e = s + phones[c].frames;
    if (phones[c].ph == PSILENCE) continue;
    ph = &phon[phones[c].ph];
    switch (ph->type)
    {
      case PV: case PG: case PN:
      {
        int in = (ph->type == PV) ? 2 : 1;
        if (in >= e - s) in = (e - s) / 2;
        ax[na] = s + in; a1[na] = ph->f1; a2[na] = ph->f2; na++;
        if (e - 1 > s + in) { ax[na] = e - 1; a1[na] = ph->f1; a2[na] = ph->f2; na++; }
      }
      break;
      case PD:
      ax[na] = s + 1; a1[na] = ph->f1; a2[na] = ph->f2; na++;
      ax[na] = e - 1; a1[na] = ph->f1b; a2[na] = ph->f2b; na++;
      break;
      default:
      // consonant locus at its end (the release into the next sound)
      ax[na] = e - 1; a1[na] = ph->f1 ? ph->f1 : 400; a2[na] = ph->f2; na++;
      break;
    }
  }
  if (!na)
  {
    for (f = 0; f < numframes; f++) { f1t[f] = 500; f2t[f] = 1500; }
    return;
  }
  for (f = 0; f < numframes; f++)
  {
    int k = 0;
    while ((k < na) && (ax[k] < f)) k++;
    if (k == 0) { f1t[f] = a1[0]; f2t[f] = a2[0]; }
    else if (k == na) { f1t[f] = a1[na - 1]; f2t[f] = a2[na - 1]; }
    else
    {
      int x0 = ax[k - 1], x1 = ax[k];
      if (x1 == x0) { f1t[f] = a1[k]; f2t[f] = a2[k]; }
      else
      {
        f1t[f] = a1[k - 1] + (a1[k] - a1[k - 1]) * (f - x0) / (x1 - x0);
        f2t[f] = a2[k - 1] + (a2[k] - a2[k - 1]) * (f - x0) / (x1 - x0);
      }
    }
  }
}

static void synthesize(const SPEECHPARAMS *p)
{
  int c, f;
  int g;
  for (c = 0; c < numphones; c++)
  {
    int s = phones[c].start, n = phones[c].frames, note = phones[c].note;
    const PHON *ph = phones[c].ph == PSILENCE ? NULL : &phon[phones[c].ph];
    int nv = nextvowel(c);
    int nextf2 = fscale(nv >= 0 ? phon[phones[nv].ph].f2 : 1500);
    for (f = 0; f < n; f++)
    {
      FRAME *fr = &frames[s + f];
      const FRAME *prev = (s + f) ? &frames[s + f - 1] : NULL;
      int g = s + f;
      if (!ph) { silentframe(fr, prev); continue; }
      switch (ph->type)
      {
        case PV: case PD: case PG:
        voiced(fr, f1t[g], f2t[g], note, p, g);
        break;
        case PN:
        // nasal murmur: low pass around 300 Hz, the transitions carry the place
        voiced(fr, fscale(280), fscale(1000), note, p, g);
        fr->ftype = 0x10;
        fr->cutoff = hz2cutoff(fscale(f < n - 1 ? 350 : 600));
        break;
        case PFU:
        noiseframe(fr, fscale(ph->noise), ph->noise >= 5000 ? 0x40 : 0x20, ph->noise >= 5000 ? 4 : 8);
        if (!strcmp(ph->name, "F") || !strcmp(ph->name, "TH")) fr->res = 0;
        break;
        case PFV:
        // voiced fricative: voicing with noise frames in between
        if ((f & 1) && (n > 2)) noiseframe(fr, fscale(ph->noise), 0x20, 6);
        else
        {
          voiced(fr, fscale(ph->f1), fscale(ph->f2), note, p, g);
          fr->ftype = 0x10;
          fr->cutoff = hz2cutoff(fscale(900));
        }
        break;
        case PH:
        // aspiration: breathy noise shaped like the following vowel
        noiseframe(fr, nextf2, 0x20, 4);
        break;
        case PSU: case PSV:
        {
          // closure (silence / voice bar), a short burst without resonance
          // (a resonant burst rings like a wood block) and a breathy
          // aspiration; a stop before a pause only gets a soft release
          int final = nv < 0;
          int asp = (ph->type == PSU) && (!final) ? (n >= 5 ? 2 : 1) : 0;
          int burst = n - 1 - asp;
          int alveolar = !strcmp(ph->name, "T") || !strcmp(ph->name, "D");
          int velar = !strcmp(ph->name, "K") || !strcmp(ph->name, "G");
          if (f < burst)
          {
            if (ph->type == PSV)
            {
              voiced(fr, fscale(250), fscale(800), note, p, g);
              fr->ftype = 0x10;
              fr->cutoff = hz2cutoff(fscale(250));
            }
            else silentframe(fr, prev);
          }
          else if (f == burst)
          {
            if (final) noiseframe(fr, fscale(6000), 0x40, 0);
            else if (alveolar) noiseframe(fr, fscale(ph->noise), 0x40, 2);
            else if (velar) noiseframe(fr, nextf2 + 300, 0x20, 4);
            else noiseframe(fr, fscale(1500), 0x10, 1);
          }
          else noiseframe(fr, nextf2, 0x20, 3);
        }
        break;
        case PAF:
        if (f < 2) silentframe(fr, prev);
        else noiseframe(fr, ph->noise, 0x20, 10);
        break;
      }
    }
  }
  // silent frames: filter already set like the following sound (no filter
  // jumps at the release of P, T, K)
  for (g = numframes - 2; g >= 0; g--)
  {
    if (frames[g].wave == W_SILENT)
    {
      frames[g].ftype = frames[g + 1].ftype;
      frames[g].res = frames[g + 1].res;
      frames[g].cutoff = frames[g + 1].cutoff;
      frames[g].pulse = frames[g + 1].pulse;
    }
  }
}

// ---------------------------------------------------------------------------
// Frames -> GoatTracker tables
// ---------------------------------------------------------------------------

typedef struct
{
  unsigned char l[MAX_TABLELEN], r[MAX_TABLELEN];
  int n;
} PROG;

static int progadd(PROG *pr, int l, int r)
{
  if (pr->n >= MAX_TABLELEN) return 0;
  pr->l[pr->n] = l;
  pr->r[pr->n] = r;
  pr->n++;
  return 1;
}

// Wavetable program for frames [a, b)
static void makewave(PROG *pr, int a, int b)
{
  int f = a;
  pr->n = 0;
  while (f < b)
  {
    int h = 1, rem;
    while ((f + h < b) && (frames[f + h].wave == frames[f].wave) && (frames[f + h].note == frames[f].note)) h++;
    progadd(pr, frames[f].wave, frames[f].note);
    // hold: a delay step of N waits N+1 frames, $00 = one frame without change
    rem = (f + h >= b) ? 0 : h - 1;
    while (rem > 0)
    {
      int d = rem > 16 ? 16 : rem;
      if (d == 1) progadd(pr, 0x00, 0x80);
      else progadd(pr, d - 1, 0x80);
      rem -= d;
    }
    f += h;
  }
  progadd(pr, 0xff, 0x00);
}

// Pulse program: set + modulation steps
static void makepulse(PROG *pr, int a, int b)
{
  int f = a + 1;
  pr->n = 0;
  progadd(pr, 0x80 | (frames[a].pulse >> 8), frames[a].pulse & 0xff);
  while (f < b)
  {
    int d = frames[f].pulse - frames[f - 1].pulse, len = 1;
    if ((d < -128) || (d > 127))
    {
      progadd(pr, 0x80 | (frames[f].pulse >> 8), frames[f].pulse & 0xff);
      f++;
      continue;
    }
    while ((f + len < b) && (frames[f + len].pulse - frames[f + len - 1].pulse == d) && (len < 127)) len++;
    if ((d == 0) && (f + len >= b)) break;     // holding at the end: just stop
    progadd(pr, len, d & 0xff);
    f += len;
  }
  progadd(pr, 0xff, 0x00);
}

// Filter program: parameters + cutoff, modulation steps; a new parameter set
// when mode or resonance change
static void makefilter(PROG *pr, int a, int b, int voicebit)
{
  int f = a + 1;
  pr->n = 0;
  progadd(pr, 0x80 | frames[a].ftype, (frames[a].res << 4) | voicebit);
  progadd(pr, 0x00, frames[a].cutoff);
  while (f < b)
  {
    int d, len = 1;
    if ((frames[f].ftype != frames[f - 1].ftype) || (frames[f].res != frames[f - 1].res))
    {
      progadd(pr, 0x80 | frames[f].ftype, (frames[f].res << 4) | voicebit);
      progadd(pr, 0x00, frames[f].cutoff);
      f++;
      continue;
    }
    d = frames[f].cutoff - frames[f - 1].cutoff;
    if ((d < -128) || (d > 127))
    {
      progadd(pr, 0x00, frames[f].cutoff);
      f++;
      continue;
    }
    while ((f + len < b) && (frames[f + len].cutoff - frames[f + len - 1].cutoff == d) &&
           (frames[f + len].ftype == frames[f].ftype) && (frames[f + len].res == frames[f].res) && (len < 127)) len++;
    if ((d == 0) && (f + len >= b)) break;
    if ((len == 1) && (d != 0)) progadd(pr, 0x00, frames[f].cutoff);
    else progadd(pr, len, d & 0xff);
    f += len;
  }
  progadd(pr, 0xff, 0x00);
}

// ---------------------------------------------------------------------------
// Into the song
// ---------------------------------------------------------------------------

static char listing[32768];

static void lst(const char *fmt, ...)
{
  va_list ap;
  size_t l = strlen(listing);
  va_start(ap, fmt);
  vsnprintf(listing + l, sizeof listing - l, fmt, ap);
  va_end(ap);
}

int speech_songtempo(int chn)
{
  int c, r, pt;
  for (c = -1; c < numchannels; c++)
  {
    pt = editorUndoInfo.editorInfo[c < 0 ? chn : c].epnum;
    for (r = 0; r < pattlen[pt]; r++)
    {
      unsigned char *q = &pattern[pt][r * 4];
      if ((q[2] == CMD_SETTEMPO) && ((q[3] & 0x7f) >= 2)) return q[3] & 0x7f;
    }
  }
  if ((instr[MAX_INSTR - 1].ad >= 2) && (!instr[MAX_INSTR - 1].ptr[WTBL])) return instr[MAX_INSTR - 1].ad;
  return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

// speed 0 = from the song; at least F03 (at F02 the player needs longer)
static const SPEECHPARAMS *fixparams(const SPEECHPARAMS *p, SPEECHPARAMS *q, int chn)
{
  *q = *p;
  if ((q->voice < 0) || (q->voice >= NUMVOICES)) q->voice = 0;
  curvoice = &voices[q->voice];
  if (!q->tempo) q->tempo = speech_songtempo(chn);
  if (q->tempo < 3) q->tempo = 3;
  return q;
}

int speech_analyze(const SPEECHPARAMS *p, int *rows, int *ms, char *err, int errsize)
{
  SPEECHPARAMS fixed;
  p = fixparams(p, &fixed, editorInfo.epchn);
  if (!parsetext(p->text, err, errsize)) return 0;
  durations(p);
  timeline(p);
  if (rows) *rows = numframes / p->tempo + 1;
  if (ms) *ms = numframes * 1000 / framespersec();
  return numphones;
}

const char *speech_listing(void)
{
  return listing;
}

void speech_defaults(SPEECHPARAMS *p)
{
  memset(p, 0, sizeof *p);
  p->basenote = 2 * 12 + 9;      // A-2, about 110 Hz
  p->speed = 100;
  p->melody = 1;
  p->voice = 0;
  p->tempo = 0;
  p->settempo = 0;
  p->mode = 0;
}

static int instrempty(int i)
{
  const INSTR *in = &instr[i];
  // cleared instruments keep the default gate timer and 1st frame wave
  return (!in->ad) && (!in->sr) && (!in->ptr[0]) && (!in->ptr[1]) && (!in->ptr[2]) && (!in->ptr[3]) &&
         (!in->vibdelay) && (!in->name[0]);
}

static const char *notestr(int n, char *buf)
{
  static const char *nn[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  sprintf(buf, "%s%d", nn[n % 12], n / 12);
  return buf;
}

static int samephones(int a, int b, int n)
{
  int c;
  for (c = 0; c < n; c++)
    if (phones[a + c].ph != phones[b + c].ph) return 0;
  return 1;
}

int speech_generate(const SPEECHPARAMS *p, int chn, int row, char *err, int errsize)
{
  static PROG wp[MAXPHONES], pp[MAXPHONES], fp[MAXPHONES];
  int slot[MAXPHONES], inum[MAXPHONES];
  int c, k, need = 0, tlen[3], rows, patt, voicebit, sid;
  char phs[1024], nb[16];
  SPEECHPARAMS fixed;

  p = fixparams(p, &fixed, chn);
  listing[0] = 0;
  if (!speech_phonemes(p->text, phs, sizeof phs, err, errsize)) return 0;
  if (!parsetext(p->text, err, errsize)) return 0;
  durations(p);
  timeline(p);
  if (numframes >= MAXFRAMES)
  {
    snprintf(err, errsize, "The text is too long");
    return 0;
  }
  intonation(p);
  formanttracks();
  {
    int fr;
    for (fr = 0; fr < numframes; fr++) { f1t[fr] = fscale(f1t[fr]); f2t[fr] = fscale(f2t[fr]); }
  }
  synthesize(p);

  voicebit = 1 << (chn % 3);
  sid = chn / 3;
  // programs per segment (the frames from its row to the next segment)
  for (c = 0; c < numsegs; c++)
  {
    int a = segs[c].start, b = segs[c].end;
    makewave(&wp[c], a, b);
    makepulse(&pp[c], a, b);
    makefilter(&fp[c], a, b, voicebit);
  }
  // identical sounds share one instrument
  for (c = 0; c < numsegs; c++)
  {
    slot[c] = c;
    for (k = 0; k < c; k++)
    {
      // the first sound has its own instrument (gate retrigger, see below)
      if ((k) && (slot[k] == k) && (segs[k].last - segs[k].first == segs[c].last - segs[c].first) &&
          (samephones(segs[k].first, segs[c].first, segs[c].last - segs[c].first + 1)) &&
          (wp[k].n == wp[c].n) && (!memcmp(wp[k].l, wp[c].l, wp[c].n)) && (!memcmp(wp[k].r, wp[c].r, wp[c].n)) &&
          (pp[k].n == pp[c].n) && (!memcmp(pp[k].l, pp[c].l, pp[c].n)) && (!memcmp(pp[k].r, pp[c].r, pp[c].n)) &&
          (fp[k].n == fp[c].n) && (!memcmp(fp[k].l, fp[c].l, fp[c].n)) && (!memcmp(fp[k].r, fp[c].r, fp[c].n)))
      {
        slot[c] = k;
        break;
      }
    }
    if (slot[c] == c) need++;
  }
  // space: instruments, tables, pattern rows
  {
    int free = 0;
    for (k = 1; k < MAX_INSTR - 1; k++) if (instrempty(k)) free++;
    if (free < need)
    {
      snprintf(err, errsize, "Not enough free instruments (%d needed, %d free)", need, free);
      return 0;
    }
  }
  for (k = 0; k < 3; k++)
  {
    int sum = 0;
    tlen[k] = gettablelen(k);
    for (c = 0; c < numsegs; c++)
      if (slot[c] == c) sum += (k == 0 ? wp[c].n : (k == 1 ? pp[c].n : fp[c].n));
    if (tlen[k] + sum > MAX_TABLELEN)
    {
      static const char *tn[3] = {"wave", "pulse", "filter"};
      snprintf(err, errsize, "Not enough room in the %stable (%d entries needed, %d free)", tn[k], sum, MAX_TABLELEN - tlen[k]);
      return 0;
    }
  }
  rows = numframes / p->tempo + 1;
  patt = editorUndoInfo.editorInfo[chn].epnum;
  if (row + rows > MAX_PATTROWS)
  {
    snprintf(err, errsize, "The speech needs %d rows, the pattern has room for %d from row %d", rows, MAX_PATTROWS - row, row);
    return 0;
  }

  // instruments + tables
  k = 1;
  for (c = 0; c < numsegs; c++)
  {
    INSTR *in;
    char name[MAX_INSTRNAMELEN + 8], sn[MAX_INSTRNAMELEN];
    int t;
    if (slot[c] != c) { inum[c] = inum[slot[c]]; continue; }
    while (!instrempty(k)) k++;
    inum[c] = k;
    in = &instr[k];
    memset(in, 0, sizeof *in);
    in->ad = 0x00;
    in->sr = 0xf4;
    // no hard restart, no gate off (legato), new note 1 frame before. The
    // first sound clears the gate one frame before its row, so the envelope
    // starts again even if the voice still holds a decayed note
    in->gatetimer = c ? (0xc0 | 1) : (0x80 | 1);
    in->firstwave = frames[segs[c].start].wave == W_SILENT ? 0x01 : frames[segs[c].start].wave;
    segname(&segs[c], sn, MAX_INSTRNAMELEN - 4);
    snprintf(name, sizeof name, "Say %s", sn);
    strncpy(in->name, name, MAX_INSTRNAMELEN - 1);
    for (t = 0; t < 3; t++)
    {
      PROG *pr = t == 0 ? &wp[c] : (t == 1 ? &pp[c] : &fp[c]);
      in->ptr[t] = tlen[t] + 1;
      memcpy(&ltable[t][tlen[t]], pr->l, pr->n);
      memcpy(&rtable[t][tlen[t]], pr->r, pr->n);
      tlen[t] += pr->n;
    }
    k++;
  }

  // pattern rows
  if (pattlen[patt] < row + rows)
  {
    int r;
    for (r = pattlen[patt]; r < row + rows; r++)
    {
      pattern[patt][r * 4] = REST;
      pattern[patt][r * 4 + 1] = 0;
      pattern[patt][r * 4 + 2] = 0;
      pattern[patt][r * 4 + 3] = 0;
    }
    pattlen[patt] = row + rows;
    pattern[patt][pattlen[patt] * 4] = ENDPATT;
    pattern[patt][pattlen[patt] * 4 + 1] = 0;
    pattern[patt][pattlen[patt] * 4 + 2] = 0;
    pattern[patt][pattlen[patt] * 4 + 3] = 0;
  }
  {
    int r;
    for (r = row; r < row + rows; r++)
    {
      pattern[patt][r * 4] = REST;
      pattern[patt][r * 4 + 1] = 0;
      pattern[patt][r * 4 + 2] = 0;
      pattern[patt][r * 4 + 3] = 0;
    }
  }
  for (c = 0; c < numsegs; c++)
  {
    int r = row + segs[c].row;
    pattern[patt][r * 4] = FIRSTNOTE + phones[segs[c].first].note;
    pattern[patt][r * 4 + 1] = inum[c];
  }
  pattern[patt][(row + rows - 1) * 4] = KEYOFF;
  if (p->settempo)
  {
    pattern[patt][row * 4 + 2] = CMD_SETTEMPO;
    pattern[patt][row * 4 + 3] = p->tempo;
  }

  // listing
  lst("C64 GrooveGoat speech generator\n===============================\n\n");
  lst("Text:      %s\nPhonemes:  %s\n", p->text, phs);
  lst("Speaker:   %s, base note %s (formants %d%%)\n", curvoice->name, notestr(p->basenote, nb), curvoice->formant);
  lst("Voice:     channel %d.%d (SID %d voice %d), filter on this voice only (bitmask $%X)\n", sid + 1, chn % 3 + 1, sid + 1, chn % 3 + 1, voicebit);
  lst("Timing:    song speed F%02X = %d frames per row (%d ms), tables run every frame (%d per second),\n"
      "           %d rows, %.2f s. Several short sounds can share one row / instrument.\n",
      p->tempo, p->tempo, p->tempo * 1000 / framespersec(), framespersec(), rows, (double)numframes / framespersec());
  lst("Model:     %s\n", p->mode ? "experimental" : "bandpass (resonance $F) on formant 1, pulse width follows formant 2, sawtooth for back vowels");
  lst("Filter:    the filter of SID %d is used by this voice only. Its other two voices should not use the\n"
      "           filter while the speech plays (the filter table of the speech sets it every frame).\n\n", sid + 1);
  lst("1. INSTRUMENTS\n\n");
  lst("   All: Attack/Decay $00, Sustain/Release $F4, HR/Gate timer $C1 (no hard restart, legato;\n");
  lst("   the first one $81: gate off one frame before, so the envelope restarts),\n");
  lst("   vibrato off. 1stFrame wave = waveform of the first frame.\n\n");
  lst("   Nr  Name             Wave  Pulse Filter 1stWave\n");
  for (c = 0; c < numsegs; c++)
  {
    if (slot[c] != c) continue;
    lst("   %02X  %-16s %02X    %02X    %02X     %02X\n", inum[c], instr[inum[c]].name, instr[inum[c]].ptr[0],
        instr[inum[c]].ptr[1], instr[inum[c]].ptr[2], instr[inum[c]].firstwave);
  }
  for (k = 0; k < 3; k++)
  {
    static const char *hdr[3] = {"2a. W TAB (wavetable: waveform, note; 0x = delay, FF = stop)",
                                 "2b. P TAB (pulsetable: 8X YY = set pulse $XYY, TT SS = TT frames speed SS)",
                                 "2c. F TAB (filtertable: 90/A0/B0/C0 = lowpass/bandpass/LP+BP/highpass, right = resonance/voice;\n"
                                 "    00 XX = cutoff $XX, TT SS = TT frames speed SS)"};
    lst("\n%s\n\n", hdr[k]);
    for (c = 0; c < numsegs; c++)
    {
      int e, start;
      if (slot[c] != c) continue;
      start = instr[inum[c]].ptr[k];
      lst("   ; instrument %02X (%s)\n", inum[c], instr[inum[c]].name);
      for (e = start; e > 0; e++)
      {
        lst("   %02X: %02X %02X\n", e, ltable[k][e - 1], rtable[k][e - 1]);
        if (ltable[k][e - 1] == 0xff) break;
      }
    }
  }
  lst("\n3. PATTERN (pattern %02X, channel %d.%d, from row %02X)\n\n", patt, sid + 1, chn % 3 + 1, row);
  lst("   ROW  NOTE INS CMD   SOUNDS\n");
  {
    int r;
    for (r = row; r < row + rows; r++)
    {
      unsigned char *q = &pattern[patt][r * 4];
      char ncol[8], snd[64] = "";
      if (q[0] <= LASTNOTE) notestr(q[0] - FIRSTNOTE, ncol);
      else if (q[0] == KEYOFF) strcpy(ncol, "===");
      else strcpy(ncol, "...");
      for (c = 0; c < numsegs; c++)
        if (row + segs[c].row == r) segname(&segs[c], snd, sizeof snd);
      if (q[0] == KEYOFF) strcpy(snd, "(off)");
      lst("   %02X   %-4s %02X  %X%02X   %s\n", r, ncol, q[1], q[2], q[3], snd);
    }
  }
  (void)nb;
  return rows;
}

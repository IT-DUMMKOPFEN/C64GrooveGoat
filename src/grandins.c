//
// C64 GrooveGoat - random instrument generator
//
// Creates playable GoatTracker instruments (ADSR, wave-, pulse-, filter- and
// vibrato tables) from recipes for typical C64 sounds. The recipes take the
// selected SID model into account (the 6581 filter is darker and combined
// waveforms are weak on the 6581).
//

#include "goattrk2.h"
#include "gres.h"
#include "grandins.h"
#include "gexport.h"

enum
{
  RC_BASS = 0, RC_ACIDBASS, RC_KICK, RC_SNARE, RC_HIHAT, RC_TOM, RC_WOOD, RC_GUITAR,
  RC_BRASS, RC_STRINGS, RC_LEAD, RC_PAD, RC_ARP, RC_BELL, RC_FX, NUMRECIPES
};

static const char *recipenames[NUMRECIPES] = {
  "Bass", "Acid bass", "Bass drum", "Snare", "Hi-hat", "Tom", "Woodwind", "Guitar",
  "Brass", "Strings", "Lead", "Pad", "Arpeggio", "Bell / mallet", "Effect"
};

enum { FT_AUTO = 0, FT_OFF, FT_LP, FT_BP, FT_HP, FT_LPBP, FT_NOTCH, FT_BPHP, FT_ALL, NUMFT };
static const char *filternames[NUMFT] = {
  "Auto (fits the sound)", "Off", "Low-pass", "Band-pass", "High-pass", "Low+band-pass", "Notch (low+high-pass)", "Band+high-pass", "All (LP+BP+HP)"
};
static const int filterbits[NUMFT] = {0, 0, 0x10, 0x20, 0x40, 0x30, 0x50, 0x60, 0x70};

enum { CR_AUTO = 0, CR_LOW, CR_MID, CR_HIGH, NUMCR };
static const char *cutoffnames[NUMCR] = {"Auto", "Low", "Middle", "High"};

enum { MV_AUTO = 0, MV_STATIC, MV_DOWN, MV_UP, MV_WOBBLE, NUMMV };
static const char *movenames[NUMMV] = {"Auto", "Static", "Sweep down", "Sweep up", "Wobble"};

typedef struct
{
  int recipe;
  int filtertype;    // FT_*
  int resonance;     // -1 auto, 0-15
  int cutoff;        // CR_*
  int move;          // MV_*
  int voice;         // filtered voice 0-2
} RNDSETTINGS;

static RNDSETTINGS rs = {RC_BASS, FT_AUTO, -1, CR_AUTO, MV_AUTO, 0};

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

typedef struct
{
  unsigned char l[64], r[64];
  int n;
} TBL;

typedef struct
{
  char name[MAX_INSTRNAMELEN];
  int ad, sr, firstwave, gatetimer, vibdelay;
  int vibspeed, vibdepth;     // 0 = no vibrato
  TBL t[3];                   // wave, pulse, filter
  int previewnote;            // octave offset for the preview
  int previewframes;          // how long the preview note is held
} RNDINSTR;

static RNDINSTR cur;
static int havecur = 0;

static int rnd(int a, int b)
{
  if (b <= a) return a;
  return a + rand() % (b - a + 1);
}

static int chance(int percent)
{
  return rand() % 100 < percent;
}

static int pick(const int *v, int n)
{
  return v[rand() % n];
}

static void tadd(TBL *t, int l, int r)
{
  if (t->n >= 63) return;
  t->l[t->n] = l;
  t->r[t->n] = r;
  t->n++;
}

// jump to row "row" (0-based within the table) or stop (row < 0)
static void tjump(TBL *t, int row)
{
  tadd(t, 0xff, row < 0 ? 0 : row + 1);
}

#define WAVE_TRI 0x11
#define WAVE_SAW 0x21
#define WAVE_PUL 0x41
#define WAVE_NOI 0x81
#define NOTE_KEEP 0x80
#define ABSNOTE(n) (0x80 + (n))          // n = 1..95 (C#0 .. B-7)

static int sid8580(void)
{
  return editorInfo.sidmodel ? 1 : 0;
}

// Pulse table: start width (12 bit) and optional modulation
static void pulse_static(TBL *t, int pw)
{
  tadd(t, 0x80 | ((pw >> 8) & 0x0f), pw & 0xff);
  tjump(t, -1);
}

static void pulse_sweep(TBL *t, int pw, int time, int speed, int loop)
{
  tadd(t, 0x80 | ((pw >> 8) & 0x0f), pw & 0xff);
  tadd(t, time, speed & 0xff);
  if (loop)
  {
    tadd(t, time, (-speed) & 0xff);
    tjump(t, 1);
  }
  else tjump(t, -1);
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

typedef struct
{
  int type;          // FT_*
  int res;           // 0-15
  int cutoff;        // 0-255
  int move;          // MV_* (not auto)
  int speed;         // modulation speed
  int time;          // modulation time
} FILTERSPEC;

static int cutoffrange(int range)
{
  if (sid8580())
  {
    switch (range)
    {
      case CR_LOW: return rnd(0x18, 0x40);
      case CR_MID: return rnd(0x40, 0x90);
      default: return rnd(0x90, 0xe0);
    }
  }
  // the 6581 filter opens much earlier
  switch (range)
  {
    case CR_LOW: return rnd(0x08, 0x1c);
    case CR_MID: return rnd(0x1c, 0x38);
    default: return rnd(0x38, 0x68);
  }
}

static void buildfilter(RNDINSTR *ins, FILTERSPEC *f)
{
  TBL *t = &ins->t[2];
  // apply the user's choices over the recipe
  if (rs.filtertype == FT_OFF) return;
  if (rs.filtertype != FT_AUTO) f->type = rs.filtertype;
  if (f->type == FT_OFF) return;
  if (rs.resonance >= 0) f->res = rs.resonance;
  if (rs.cutoff != CR_AUTO) f->cutoff = cutoffrange(rs.cutoff);
  if (rs.move != MV_AUTO) f->move = rs.move;
  if (f->cutoff < 1) f->cutoff = 1;
  if (f->cutoff > 0xff) f->cutoff = 0xff;
  if (!f->speed) f->speed = rnd(1, 4);
  if (!f->time) f->time = rnd(0x10, 0x30);
  if (f->time > 0x7f) f->time = 0x7f;

  tadd(t, 0x80 | filterbits[f->type], ((f->res & 15) << 4) | (1 << rs.voice));
  switch (f->move)
  {
    case MV_STATIC:
    default:
    tadd(t, 0x00, f->cutoff);
    tjump(t, -1);
    break;

    case MV_DOWN:
    {
      int start = f->cutoff + f->speed * f->time;
      if (start > 0xff)
      {
        start = 0xff;
        f->time = (start - f->cutoff) / f->speed;
        if (f->time < 1) f->time = 1;
        if (f->time > 0x7f) f->time = 0x7f;
      }
      tadd(t, 0x00, start);
      tadd(t, f->time, (-f->speed) & 0xff);
      tjump(t, -1);
    }
    break;

    case MV_UP:
    {
      int start = f->cutoff - f->speed * f->time;
      if (start < 1)
      {
        start = 1;
        f->time = (f->cutoff - start) / f->speed;
        if (f->time < 1) f->time = 1;
        if (f->time > 0x7f) f->time = 0x7f;
      }
      tadd(t, 0x00, start);
      tadd(t, f->time, f->speed & 0xff);
      tjump(t, -1);
    }
    break;

    case MV_WOBBLE:
    {
      int depth = f->speed * f->time / 2;
      int start = f->cutoff - depth;
      if (start < 1) start = 1;
      tadd(t, 0x00, start);
      tadd(t, f->time, f->speed & 0xff);
      tadd(t, f->time, (-f->speed) & 0xff);
      tjump(t, 2);
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// Recipes
// ---------------------------------------------------------------------------

static void setname(RNDINSTR *ins, const char *base)
{
  snprintf(ins->name, MAX_INSTRNAMELEN, "%.12s %02X", base, rand() & 0xff);
}

static void generate(RNDINSTR *ins)
{
  FILTERSPEC f;
  TBL *w = &ins->t[0], *p = &ins->t[1];
  int is8580 = sid8580();
  int c;

  memset(ins, 0, sizeof *ins);
  memset(&f, 0, sizeof f);
  f.type = FT_OFF;
  f.move = MV_STATIC;
  ins->firstwave = 0x09;
  ins->gatetimer = editorInfo.multiplier ? 2 * editorInfo.multiplier : 1;
  ins->previewnote = 3;
  ins->previewframes = 45;

  switch (rs.recipe)
  {
    case RC_BASS:
    {
      int v = rnd(0, 3);
      ins->ad = rnd(0, 1) << 4 | rnd(6, 9);
      ins->sr = rnd(6, 0xb) << 4 | rnd(4, 8);
      ins->previewnote = 1;
      if (v == 0)
      {
        // pulse bass with a slow width sweep
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x200, 0x500), rnd(0x20, 0x60), rnd(0x10, 0x40), 1);
        setname(ins, "Pulse Bass");
      }
      else if (v == 1)
      {
        // plucked: one frame of noise, then pulse
        tadd(w, WAVE_NOI, NOTE_KEEP);
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x300, 0x700), rnd(0x10, 0x30), rnd(0x20, 0x60), 0);
        setname(ins, "Slap Bass");
        ins->ad = 0x08 | rnd(0, 1);
      }
      else if (v == 2)
      {
        // saw bass (pulse+saw on the 8580)
        tadd(w, is8580 && chance(50) ? 0x61 : WAVE_SAW, 0);
        tjump(w, -1);
        pulse_static(p, rnd(0x400, 0x800));
        f.type = FT_LP;
        f.res = rnd(2, 7);
        f.cutoff = cutoffrange(CR_LOW);
        f.move = MV_DOWN;
        f.speed = rnd(2, 6);
        f.time = rnd(0x08, 0x18);
        setname(ins, "Saw Bass");
      }
      else
      {
        // octave pop at the start
        tadd(w, WAVE_PUL, 12);
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x200, 0x400), rnd(0x30, 0x7f), rnd(0x08, 0x20), 1);
        setname(ins, "Pop Bass");
      }
    }
    break;

    case RC_ACIDBASS:
    ins->ad = 0x0 << 4 | rnd(7, 9);
    ins->sr = rnd(8, 0xc) << 4 | rnd(3, 6);
    ins->previewnote = 2;
    tadd(w, chance(60) ? WAVE_SAW : WAVE_PUL, 0);
    tjump(w, -1);
    pulse_static(p, rnd(0x300, 0x800));
    f.type = FT_LP;
    f.res = is8580 ? rnd(0xa, 0xf) : rnd(0xc, 0xf);
    f.cutoff = cutoffrange(CR_LOW);
    f.move = MV_DOWN;
    f.speed = rnd(3, 8);
    f.time = rnd(0x0c, 0x20);
    setname(ins, "Acid Bass");
    break;

    case RC_KICK:
    {
      int start = rnd(46, 58), end = rnd(18, 30), step = rnd(2, 4);
      int wave = pick((const int[]){WAVE_TRI, WAVE_PUL, WAVE_TRI, is8580 ? WAVE_SAW : WAVE_TRI}, 4);
      ins->ad = 0x0 << 4 | rnd(7, 9);
      ins->sr = 0x00;
      ins->firstwave = 0x09;
      ins->previewframes = 20;
      if (chance(70)) tadd(w, WAVE_NOI, ABSNOTE(rnd(70, 90)));
      for (c = start; c > end; c -= step)
        tadd(w, wave, ABSNOTE(c));
      tadd(w, wave & 0xfe, ABSNOTE(end));
      tjump(w, -1);
      pulse_static(p, rnd(0x600, 0x900));
      if (chance(30))
      {
        f.type = FT_LP;
        f.res = rnd(0, 4);
        f.cutoff = cutoffrange(CR_LOW);
      }
      setname(ins, "Kick");
    }
    break;

    case RC_SNARE:
    {
      int body = rnd(40, 52);
      ins->ad = 0x0 << 4 | rnd(8, 0xa);
      ins->sr = 0x00;
      ins->previewframes = 20;
      tadd(w, WAVE_NOI, ABSNOTE(rnd(84, 94)));
      tadd(w, chance(60) ? WAVE_PUL : WAVE_TRI, ABSNOTE(body));
      if (chance(50)) tadd(w, WAVE_PUL, ABSNOTE(body - rnd(2, 5)));
      tadd(w, WAVE_NOI, ABSNOTE(rnd(70, 84)));
      tadd(w, WAVE_NOI, ABSNOTE(rnd(64, 76)));
      tjump(w, w->n - 1);
      pulse_static(p, rnd(0x300, 0x800));
      if (chance(40))
      {
        f.type = chance(50) ? FT_HP : FT_BP;
        f.res = rnd(1, 6);
        f.cutoff = cutoffrange(CR_MID);
      }
      setname(ins, "Snare");
    }
    break;

    case RC_HIHAT:
    {
      int open = chance(35);
      ins->ad = 0x0 << 4 | (open ? rnd(8, 9) : rnd(3, 6));
      ins->sr = 0x00;
      ins->previewframes = open ? 30 : 10;
      tadd(w, WAVE_NOI, ABSNOTE(rnd(88, 95)));
      tadd(w, WAVE_NOI, ABSNOTE(rnd(84, 95)));
      tjump(w, 1);
      if (chance(50))
      {
        f.type = FT_HP;
        f.res = rnd(0, 5);
        f.cutoff = cutoffrange(CR_HIGH);
      }
      setname(ins, open ? "Open HiHat" : "Closed HiHat");
    }
    break;

    case RC_TOM:
    {
      int start = rnd(48, 64), end = start - rnd(8, 16);
      ins->ad = 0x0 << 4 | rnd(8, 0xa);
      ins->sr = 0x00;
      ins->previewframes = 25;
      if (chance(50)) tadd(w, WAVE_NOI, ABSNOTE(rnd(70, 85)));
      for (c = start; c > end; c--)
        tadd(w, chance(70) ? WAVE_TRI : WAVE_PUL, ABSNOTE(c));
      tjump(w, w->n - 1);
      pulse_static(p, rnd(0x500, 0x800));
      setname(ins, "Tom");
    }
    break;

    case RC_WOOD:
    {
      int v = rnd(0, 2);
      ins->ad = rnd(2, 5) << 4 | rnd(6, 9);
      ins->sr = rnd(0xa, 0xd) << 4 | rnd(6, 9);
      ins->vibdelay = rnd(0x10, 0x28);
      ins->vibspeed = rnd(2, 4);
      ins->vibdepth = rnd(0x10, 0x30);
      ins->previewnote = 4;
      ins->previewframes = 60;
      if (v == 0)
      {
        // flute: triangle with a breathy start
        if (chance(60)) tadd(w, WAVE_NOI, NOTE_KEEP);
        tadd(w, WAVE_TRI, 0);
        tjump(w, -1);
        pulse_static(p, 0x800);
        setname(ins, "Flute");
      }
      else if (v == 1)
      {
        // clarinet: square wave, darkened
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_static(p, rnd(0x700, 0x800));
        f.type = FT_LP;
        f.res = rnd(1, 4);
        f.cutoff = cutoffrange(CR_MID);
        setname(ins, "Clarinet");
      }
      else
      {
        // oboe: narrow pulse through a band-pass
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x180, 0x300), rnd(0x40, 0x7f), rnd(0x04, 0x0c), 1);
        f.type = FT_BP;
        f.res = rnd(3, 8);
        f.cutoff = cutoffrange(CR_MID);
        setname(ins, "Oboe");
      }
    }
    break;

    case RC_GUITAR:
    {
      int v = rnd(0, 2);
      ins->previewnote = 3;
      if (v == 0)
      {
        // clean pluck
        ins->ad = 0x0 << 4 | rnd(9, 0xa);
        ins->sr = rnd(0, 6) << 4 | rnd(6, 9);
        tadd(w, WAVE_PUL, 12);
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x200, 0x400), rnd(0x20, 0x40), rnd(0x20, 0x50), 0);
        setname(ins, "Pluck Guitar");
      }
      else if (v == 1)
      {
        // distorted lead guitar: saw, resonant low-pass, vibrato
        ins->ad = 0x0 << 4 | rnd(8, 9);
        ins->sr = rnd(0xa, 0xc) << 4 | rnd(6, 9);
        ins->vibdelay = rnd(0x18, 0x30);
        ins->vibspeed = rnd(2, 3);
        ins->vibdepth = rnd(0x20, 0x50);
        tadd(w, is8580 && chance(50) ? 0x61 : WAVE_SAW, 0);
        tjump(w, -1);
        pulse_static(p, rnd(0x300, 0x600));
        f.type = FT_LP;
        f.res = rnd(6, 0xc);
        f.cutoff = cutoffrange(CR_MID);
        f.move = chance(50) ? MV_DOWN : MV_STATIC;
        setname(ins, "Dist Guitar");
      }
      else
      {
        // power chord: fast root/fifth/octave
        ins->ad = 0x0 << 4 | rnd(8, 9);
        ins->sr = rnd(8, 0xb) << 4 | rnd(6, 8);
        tadd(w, WAVE_PUL, 0);
        tadd(w, WAVE_PUL, 7);
        tadd(w, WAVE_PUL, 12);
        tjump(w, 0);
        pulse_sweep(p, rnd(0x300, 0x500), rnd(0x20, 0x40), rnd(0x10, 0x30), 1);
        setname(ins, "Power Chord");
      }
    }
    break;

    case RC_BRASS:
    ins->ad = rnd(1, 4) << 4 | rnd(7, 9);
    ins->sr = rnd(9, 0xc) << 4 | rnd(5, 8);
    ins->vibdelay = rnd(0x18, 0x30);
    ins->vibspeed = rnd(2, 4);
    ins->vibdepth = rnd(0x18, 0x38);
    ins->previewnote = 3;
    ins->previewframes = 60;
    tadd(w, chance(70) ? WAVE_SAW : WAVE_PUL, 0);
    tjump(w, -1);
    pulse_sweep(p, rnd(0x400, 0x700), rnd(0x30, 0x60), rnd(0x08, 0x20), 1);
    // the typical brass "swell": the filter opens at the start
    f.type = FT_LP;
    f.res = rnd(1, 5);
    f.cutoff = cutoffrange(chance(50) ? CR_MID : CR_HIGH);
    f.move = MV_UP;
    f.speed = rnd(2, 5);
    f.time = rnd(0x0c, 0x20);
    setname(ins, "Brass");
    break;

    case RC_STRINGS:
    ins->ad = rnd(5, 9) << 4 | rnd(8, 9);
    ins->sr = rnd(0xb, 0xd) << 4 | rnd(9, 0xb);
    ins->vibdelay = rnd(0x10, 0x20);
    ins->vibspeed = rnd(3, 5);
    ins->vibdepth = rnd(0x10, 0x20);
    ins->previewnote = 3;
    ins->previewframes = 90;
    if (chance(55))
    {
      tadd(w, WAVE_SAW, 0);
      pulse_static(p, 0x800);
    }
    else
    {
      // slow pulse width modulation gives the ensemble feeling
      tadd(w, WAVE_PUL, 0);
      pulse_sweep(p, rnd(0x200, 0x400), rnd(0x40, 0x7f), rnd(0x08, 0x18), 1);
    }
    tjump(w, -1);
    f.type = FT_LP;
    f.res = rnd(1, 4);
    f.cutoff = cutoffrange(CR_MID);
    f.move = chance(40) ? MV_WOBBLE : MV_STATIC;
    f.speed = 1;
    f.time = rnd(0x30, 0x60);
    setname(ins, "Strings");
    break;

    case RC_LEAD:
    {
      int v = rnd(0, 2);
      ins->ad = rnd(0, 1) << 4 | rnd(7, 9);
      ins->sr = rnd(8, 0xb) << 4 | rnd(5, 8);
      ins->vibdelay = rnd(0x0c, 0x24);
      ins->vibspeed = rnd(2, 4);
      ins->vibdepth = rnd(0x18, 0x48);
      ins->previewnote = 4;
      ins->previewframes = 60;
      if (v == 0)
      {
        tadd(w, WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x100, 0x400), rnd(0x20, 0x7f), rnd(0x10, 0x40), 1);
        setname(ins, "PWM Lead");
      }
      else if (v == 1)
      {
        tadd(w, WAVE_SAW, 0);
        tjump(w, -1);
        pulse_static(p, 0x800);
        if (chance(60))
        {
          f.type = FT_LP;
          f.res = rnd(4, 0xa);
          f.cutoff = cutoffrange(CR_HIGH);
          f.move = MV_DOWN;
          f.speed = rnd(1, 3);
          f.time = rnd(0x10, 0x30);
        }
        setname(ins, "Saw Lead");
      }
      else
      {
        // short octave arpeggio at the start, then the note
        tadd(w, WAVE_PUL, 12);
        tadd(w, WAVE_PUL, 0);
        tadd(w, is8580 && chance(40) ? 0x51 : WAVE_PUL, 0);
        tjump(w, -1);
        pulse_sweep(p, rnd(0x300, 0x600), rnd(0x30, 0x60), rnd(0x10, 0x30), 1);
        setname(ins, "Chip Lead");
      }
    }
    break;

    case RC_PAD:
    ins->ad = rnd(8, 0xb) << 4 | rnd(8, 0xa);
    ins->sr = rnd(0xa, 0xd) << 4 | rnd(0xa, 0xc);
    ins->vibdelay = rnd(0x20, 0x40);
    ins->vibspeed = rnd(4, 6);
    ins->vibdepth = rnd(0x08, 0x18);
    ins->previewnote = 3;
    ins->previewframes = 120;
    tadd(w, chance(50) ? WAVE_PUL : (chance(50) ? WAVE_SAW : WAVE_TRI), 0);
    tjump(w, -1);
    pulse_sweep(p, rnd(0x200, 0x600), rnd(0x60, 0x7f), rnd(0x04, 0x10), 1);
    f.type = chance(70) ? FT_LP : FT_BP;
    f.res = rnd(3, 9);
    f.cutoff = cutoffrange(CR_MID);
    f.move = MV_WOBBLE;
    f.speed = 1;
    f.time = rnd(0x40, 0x7f);
    setname(ins, "Pad");
    break;

    case RC_ARP:
    {
      static const int chords[8][4] = {
        {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 11},
        {0, 3, 7, 10}, {0, 7, 12, -1}, {0, 12, -1, -1}, {0, 4, 7, 12}
      };
      static const char *chordnames[8] = {"Major Arp", "Minor Arp", "Sus4 Arp", "Maj7 Arp", "Min7 Arp", "Fifth Arp", "Octave Arp", "Major8 Arp"};
      int ch = rnd(0, 7), wave = pick((const int[]){WAVE_PUL, WAVE_PUL, WAVE_SAW, WAVE_TRI}, 4);
      int delay = chance(30) ? 1 : 0;
      ins->ad = 0x0 << 4 | rnd(7, 9);
      ins->sr = rnd(8, 0xb) << 4 | rnd(5, 8);
      ins->previewnote = 4;
      ins->previewframes = 60;
      for (c = 0; (c < 4) && (chords[ch][c] >= 0); c++)
      {
        tadd(w, wave, chords[ch][c]);
        if (delay) tadd(w, 0x01, NOTE_KEEP);
      }
      tjump(w, 0);
      pulse_sweep(p, rnd(0x300, 0x600), rnd(0x30, 0x60), rnd(0x10, 0x30), 1);
      setname(ins, chordnames[ch]);
    }
    break;

    case RC_BELL:
    ins->ad = 0x0 << 4 | rnd(9, 0xb);
    ins->sr = 0x0 << 4 | rnd(9, 0xb);
    ins->previewnote = 5;
    ins->previewframes = 30;
    tadd(w, chance(50) ? WAVE_NOI : WAVE_PUL, 24);
    tadd(w, WAVE_TRI, 12);
    tadd(w, chance(40) && is8580 ? 0x51 : WAVE_TRI, 0);
    tjump(w, -1);
    pulse_static(p, rnd(0x100, 0x300));
    setname(ins, chance(50) ? "Bell" : "Mallet");
    break;

    case RC_FX:
    default:
    {
      int v = rnd(0, 2);
      ins->ad = 0x0 << 4 | rnd(8, 0xa);
      ins->sr = rnd(0, 8) << 4 | rnd(6, 9);
      ins->previewnote = 4;
      ins->previewframes = 40;
      if (v == 0)
      {
        // laser: falling pitch
        int start = rnd(70, 90);
        for (c = 0; c < 16; c++) tadd(w, WAVE_PUL, ABSNOTE(start - c * rnd(2, 3)));
        tjump(w, -1);
        setname(ins, "Laser");
      }
      else if (v == 1)
      {
        // siren / warble
        int a = rnd(2, 7);
        tadd(w, WAVE_TRI, 0);
        tadd(w, 0x01, NOTE_KEEP);
        tadd(w, WAVE_TRI, a);
        tadd(w, 0x01, NOTE_KEEP);
        tjump(w, 0);
        setname(ins, "Warble");
      }
      else
      {
        // noise sweep up
        int start = rnd(40, 60);
        for (c = 0; c < 12; c++) tadd(w, WAVE_NOI, ABSNOTE(start + c * 3));
        tjump(w, w->n - 1);
        f.type = FT_BP;
        f.res = rnd(6, 0xc);
        f.cutoff = cutoffrange(CR_MID);
        f.move = MV_UP;
        setname(ins, "Noise FX");
      }
      pulse_sweep(p, rnd(0x200, 0x600), rnd(0x10, 0x30), rnd(0x20, 0x60), 1);
    }
    break;
  }
  buildfilter(ins, &f);
}

// ---------------------------------------------------------------------------
// Applying an instrument to the current slot
// ---------------------------------------------------------------------------

static INSTR backupinstr;
static unsigned char backupl[MAX_TABLES][MAX_TABLELEN];
static unsigned char backupr[MAX_TABLES][MAX_TABLELEN];

static void backupslot(void)
{
  memcpy(&backupinstr, &instr[editorInfo.einum], sizeof(INSTR));
  memcpy(backupl, ltable, sizeof ltable);
  memcpy(backupr, rtable, sizeof rtable);
}

static void restoreslot(void)
{
  snd_lock();
  releasenote(editorInfo.epchn);
  memcpy(&instr[editorInfo.einum], &backupinstr, sizeof(INSTR));
  memcpy(ltable, backupl, sizeof ltable);
  memcpy(rtable, backupr, sizeof rtable);
  snd_unlock();
}

// Returns 0 if there is not enough table space
static int applyinstrument(const RNDINSTR *ins)
{
  int t, c;
  INSTR *i = &instr[editorInfo.einum];

  deleteinstrtable(editorInfo.einum);
  for (t = 0; t < 3; t++)
    if (gettablelen(t) + ins->t[t].n > MAX_TABLELEN) return 0;

  i->ad = ins->ad;
  i->sr = ins->sr;
  i->firstwave = ins->firstwave;
  i->gatetimer = ins->gatetimer;
  i->vibdelay = ins->vibspeed ? ins->vibdelay : 0;
  memset(i->name, 0, MAX_INSTRNAMELEN);
  strncpy(i->name, ins->name, MAX_INSTRNAMELEN - 1);
  for (t = 0; t < 3; t++)
  {
    const TBL *s = &ins->t[t];
    int start = gettablelen(t);
    if (!s->n)
    {
      i->ptr[t] = 0;
      continue;
    }
    for (c = 0; c < s->n; c++)
    {
      ltable[t][start + c] = s->l[c];
      rtable[t][start + c] = s->r[c];
      // jumps are relative to the start of the instrument's data
      if ((s->l[c] == 0xff) && (s->r[c])) rtable[t][start + c] = s->r[c] + start;
    }
    i->ptr[t] = start + 1;
  }
  i->ptr[STBL] = 0;
  if (ins->vibspeed)
  {
    int pos = makespeedtable((ins->vibspeed << 8) | ins->vibdepth, MST_RAW, 0);
    if (pos >= 0) i->ptr[STBL] = pos + 1;
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Description of the generated tables
// ---------------------------------------------------------------------------

static void describewave(char *buf, int size, const TBL *t)
{
  int c, n = 0;
  buf[0] = 0;
  for (c = 0; (c < t->n) && (n < size - 12); c++)
  {
    char e[16];
    if (t->l[c] == 0xff)
    {
      if (t->r[c]) sprintf(e, "loop %d", t->r[c]);
      else strcpy(e, "end");
    }
    else sprintf(e, "%02X:%02X", t->l[c], t->r[c]);
    n += snprintf(buf + n, size - n, "%s%s", c ? " " : "", e);
  }
  if (c < t->n) snprintf(buf + n, size - n, " ...");
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

void randominstrument(void)
{
  int w = 104, h = 33;
  int x0 = (MAX_COLUMNS - w) / 2, y0 = (MAX_ROWS - h) / 2;
  int sel = 0;            // 0 category, 1 filter, 2 resonance, 3 cutoff, 4 movement, 5 voice, 6..9 buttons
  int done = 0;
  int previewtimer = 0;
  int previewing = 0;
  int applied = 0;
  int nospace = 0;
  int c;

  if (!editorInfo.einum) editorInfo.einum = 1;
  stopsong();
  rs.voice = editorInfo.epchn % 3;
  backupslot();
  preview_begin();
  srand(SDL_GetTicks());

  generate(&cur);
  havecur = 1;
  restoreslot();
  applied = applyinstrument(&cur);
  nospace = !applied;
  key = rawkey = 0;

  while (!done)
  {
    char buf[200];
    int hitcat = -1, hitrow = -1, hitbtn = -1;
    int regenerate = 0, dopreview = 0;
    const char *btns[4] = {" " GS_DICE " Generate (R) ", " " GS_PLAY " Preview (SPACE) ", " " GS_CHECK " Use it (ENTER) ", " Cancel (ESC) "};
    int bx[4];

    printstatus();
    sprintf(buf, "Random instrument  " GS_BULLET "  slot %02X  " GS_BULLET "  tuned for the MOS %s", editorInfo.einum, editorInfo.sidmodel ? "8580" : "6581");
    dlg_drawframe(x0, y0, w, h, buf);

    // category grid
    printtext(x0 + 3, y0 + 2, MKCOL(sel == 0 ? 15 : 14, C_PANEL), "Sound");
    for (c = 0; c < NUMRECIPES; c++)
    {
      int cx = x0 + 3 + (c % 5) * 19;
      int cy = y0 + 3 + c / 5;
      int act = (c == rs.recipe);
      snprintf(buf, sizeof buf, " %-16s", recipenames[c]);
      printtext(cx, cy, MKCOL(act ? C_BUTTONTEXT : (sel == 0 ? 15 : 8), act ? C_BUTTONHI : (sel == 0 ? C_SELECT : C_BUTTON)), buf);
      if ((mousey == cy) && (mousex >= cx) && (mousex < cx + 17)) hitcat = c;
    }

    // filter options
    {
      static const char *labels[5] = {"SID filter", "Resonance", "Cutoff", "Filter movement", "Filtered voice"};
      for (c = 0; c < 5; c++)
      {
        int y = y0 + 7 + c;
        int isel = (sel == c + 1);
        int bg = isel ? C_SELECT : C_PANEL;
        char val[64];
        switch (c)
        {
          case 0: strcpy(val, filternames[rs.filtertype]); break;
          case 1: if (rs.resonance < 0) strcpy(val, "Auto"); else sprintf(val, "%X", rs.resonance); break;
          case 2: strcpy(val, cutoffnames[rs.cutoff]); break;
          case 3: strcpy(val, movenames[rs.move]); break;
          case 4: sprintf(val, "Voice %d (sets the filter routing bit)", rs.voice + 1); break;
        }
        printblankc(x0 + 2, y, MKCOL(8, bg), 60);
        snprintf(buf, sizeof buf, "%-17s " GS_PREV " %s " GS_NEXT, labels[c], val);
        printtext(x0 + 3, y, MKCOL(isel ? 15 : 8, bg), buf);
        if ((mousey == y) && (mousex >= x0 + 2) && (mousex < x0 + 62)) hitrow = c + 1;
      }
    }

    // result
    fillarea(x0 + 2, y0 + 13, w - 4, 14, MKCOL(8, C_PANEL), ' ');
    printtext(x0 + 3, y0 + 13, MKCOL(14, C_PANEL), "Result");
    if (havecur)
    {
      char t1[16], t2[16], t3[16];
      snprintf(buf, sizeof buf, "%s", cur.name);
      printtext(x0 + 3, y0 + 14, MKCOL(15, C_PANEL), buf);
      sprintf(buf, "AD %02X  SR %02X  1st wave %02X  vibrato %s", cur.ad, cur.sr, cur.firstwave,
        cur.vibspeed ? "yes" : "no");
      printtext(x0 + 3, y0 + 15, MKCOL(8, C_PANEL), buf);
      fillarea(x0 + 3, y0 + 16, 30, 3, MKCOL(8, C_PANEL), ' ');
      drawadsrgraph(1, x0 + 3, y0 + 16, 30, 3, cur.ad, cur.sr, MKCOL(11, C_PANEL));
      adsr_timestr(t1, adsr_attackms(cur.ad >> 4));
      adsr_timestr(t2, adsr_decayms(cur.ad & 15));
      adsr_timestr(t3, adsr_decayms(cur.sr & 15));
      sprintf(buf, "Attack %s, decay %s, sustain %X, release %s", t1, t2, cur.sr >> 4, t3);
      printtext(x0 + 35, y0 + 16, MKCOL(7, C_PANEL), buf);
      describewave(buf, 64, &cur.t[0]);
      printtext(x0 + 35, y0 + 18, MKCOL(7, C_PANEL), "Wave");
      printtext(x0 + 43, y0 + 18, MKCOL(8, C_PANEL), buf);
      describewave(buf, 58, &cur.t[1]);
      printtext(x0 + 35, y0 + 19, MKCOL(7, C_PANEL), "Pulse");
      printtext(x0 + 43, y0 + 19, MKCOL(8, C_PANEL), cur.t[1].n ? buf : "-");
      describewave(buf, 58, &cur.t[2]);
      printtext(x0 + 35, y0 + 20, MKCOL(7, C_PANEL), "Filter");
      printtext(x0 + 43, y0 + 20, MKCOL(8, C_PANEL), cur.t[2].n ? buf : "off");
      if (nospace)
        printtext(x0 + 3, y0 + 22, MKCOL(12, C_PANEL), "Not enough free table space for this instrument - delete unused table data first.");
    }
    if (isplaying())
      printtext(x0 + 3, y0 + 23, MKCOL(12, C_PANEL), "The music is playing: preview works only when the music is stopped.");
    else
      printtext(x0 + 3, y0 + 23, MKCOL(3, C_PANEL), "Each Generate creates a new variation. Preview plays the instrument on the cursor channel.");

    // buttons
    {
      int bxx = x0 + 3;
      for (c = 0; c < 4; c++)
      {
        int isel = (sel == 6 + c);
        bx[c] = bxx;
        printtext(bxx, y0 + 26, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), btns[c]);
        if ((mousey == y0 + 26) && (mousex >= bxx) && (mousex < bxx + (int)strlen(btns[c]))) hitbtn = c;
        bxx += strlen(btns[c]) + 2;
      }
    }
    printtext(x0 + 3, y0 + h - 3, MKCOL(3, C_PANEL),
      "UP/DOWN select  LEFT/RIGHT change  R generate  SPACE preview  ENTER use  ESC cancel");
    (void)bx;

    fliptoscreen();
    getkey();
    if (win_quitted) break;

    // stop the preview note after a while
    if (previewing)
    {
      previewtimer--;
      if (previewtimer <= 0)
      {
        releasenote(editorInfo.epchn);
        previewing = 0;
      }
    }

    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      if (hitcat >= 0)
      {
        rs.recipe = hitcat;
        sel = 0;
        regenerate = 1;
      }
      else if (hitrow >= 0)
      {
        sel = hitrow;
        rawkey = KEY_RIGHT;
      }
      else if (hitbtn >= 0)
      {
        sel = 6 + hitbtn;
        rawkey = KEY_ENTER;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h))
        rawkey = KEY_ESC;
    }
    if ((mouseb & MOUSEB_RIGHT) && (!prevmouseb) && (hitrow >= 0))
    {
      sel = hitrow;
      rawkey = KEY_LEFT;
    }
    if ((key == 'r') || (key == 'R')) regenerate = 1;

    switch (rawkey)
    {
      case KEY_ESC:
      done = 1;
      break;

      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_DOWN:
      case KEY_TAB:
      if (sel < 9) sel++;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      {
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        switch (sel)
        {
          case 0: rs.recipe = (rs.recipe + dir + NUMRECIPES) % NUMRECIPES; regenerate = 1; break;
          case 1: rs.filtertype = (rs.filtertype + dir + NUMFT) % NUMFT; regenerate = 1; break;
          case 2: rs.resonance += dir; if (rs.resonance < -1) rs.resonance = 15; if (rs.resonance > 15) rs.resonance = -1; regenerate = 1; break;
          case 3: rs.cutoff = (rs.cutoff + dir + NUMCR) % NUMCR; regenerate = 1; break;
          case 4: rs.move = (rs.move + dir + NUMMV) % NUMMV; regenerate = 1; break;
          case 5: rs.voice = (rs.voice + dir + 3) % 3; regenerate = 1; break;
          default:
          sel += dir;
          if (sel < 6) sel = 6;
          if (sel > 9) sel = 9;
          break;
        }
      }
      break;

      case KEY_SPACE:
      dopreview = 1;
      break;

      case KEY_ENTER:
      if (sel == 6) regenerate = 1;
      else if (sel == 7) dopreview = 1;
      else if (sel == 9) done = 1;
      else if (applied) done = 2;
      break;
    }

    if (regenerate)
    {
      generate(&cur);
      snd_lock();
      restoreslot();
      applied = applyinstrument(&cur);
      nospace = !applied;
      if (!applied) restoreslot();
      snd_unlock();
      dopreview = 1;
    }
    if ((dopreview) && (applied))
    {
      if (!isplaying())
      {
        int note = FIRSTNOTE + cur.previewnote * 12;
        preview_note(note, editorInfo.einum, editorInfo.epchn);
        previewing = 1;
        previewtimer = cur.previewframes;
      }
    }
  }

  releasenote(editorInfo.epchn);
  if (done == 2)
  {
    // apply once more on the original state so that it becomes one undo step
    void *ud;
    restoreslot();
    ud = undoinstrbegin();
    applyinstrument(&cur);
    undoinstrend(ud);
    songmodified = 1;
    snprintf(statusmessage, sizeof statusmessage, "Random instrument \"%s\" stored in slot %02X", cur.name, editorInfo.einum);
  }
  else restoreslot();
  preview_end();
  key = rawkey = 0;
  printmainscreen();
}

// Test helper (GG_RANDOMTEST=folder on the command line): renders every
// recipe for both SID models into WAV files
int randominstrument_selftest(const char *dir)
{
  int r, m, row;
  for (m = 0; m < 2; m++)
  {
    for (r = 0; r < NUMRECIPES; r++)
    {
      char name[MAX_PATHNAME];
      static const int notes[4] = {0, 4, 7, -1};
      editorInfo.sidmodel = m;
      clearsong(1, 1, 1, 1, 1);
      rs.recipe = r;
      rs.voice = 0;
      srand(r * 31 + m * 7 + 1);
      generate(&cur);
      editorInfo.einum = 1;
      if (!applyinstrument(&cur)) return 0;
      for (row = 0; row < 32; row++)
      {
        pattern[0][row * 4] = REST;
        pattern[0][row * 4 + 1] = 0;
        pattern[0][row * 4 + 2] = 0;
        pattern[0][row * 4 + 3] = 0;
      }
      for (row = 0; row < 4; row++)
      {
        int n = notes[row];
        pattern[0][row * 8 * 4] = n < 0 ? KEYOFF : FIRSTNOTE + cur.previewnote * 12 + n;
        pattern[0][row * 8 * 4 + 1] = n < 0 ? 0 : 1;
      }
      pattern[0][32 * 4] = ENDPATT;
      countpatternlengths();
      snprintf(name, sizeof name, "%s/%s_%02d_%s.wav", dir, m ? "8580" : "6581", r, cur.name);
      for (row = 0; name[row]; row++) if (name[row] == ' ') name[row] = '_';
      printf("%s: AD %02X SR %02X wave %d pulse %d filter %d rows\n", cur.name, cur.ad, cur.sr, cur.t[0].n, cur.t[1].n, cur.t[2].n);
      if (!commandlinerender(name, 0, 4, 0, 0, 1, 0)) return 0;
    }
  }
  return 1;
}

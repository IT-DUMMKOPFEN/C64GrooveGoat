// C64 GrooveGoat - beat editor (CTRL+D)
//
// Drum patterns from genre templates (gbeatcat.c) on the channel of the
// pattern cursor. The template is looped over the whole length of the
// channel's current pattern; missing drum instruments are generated.
//
// One SID voice plays one sound at a time: hits on the same row are resolved
// by priority (crash > snare > clap > kick > toms > rimshot > open hat >
// closed hat).

#include "goattrk2.h"
#include "gres.h"
#include "gbeat.h"

// ---------------------------------------------------------------------------
// Template grids
// ---------------------------------------------------------------------------

static const char rolechars[BR_NUM] = {'K', 'S', 'C', 'H', 'O', 'T', 'M', 'L', 'R', 'Y'};
static const int roleprio[BR_NUM] = {6, 8, 7, 0, 1, 5, 4, 3, 2, 9};
// pattern note per role (General MIDI drum numbers: C-3 = 36 kick ...)
static const int rolenote[BR_NUM] = {36, 38, 39, 42, 46, 48, 45, 41, 37, 49};

#define MAXHITS 512

int beat_parse(const char *grid, BEATHIT *hits, int maxhits, int *steps)
{
  int n = 0, len = -1;
  const char *s = grid;
  if (steps) *steps = 0;
  if (!grid) return 0;
  while (*s)
  {
    int role, c, l = 0;
    while (*s == ' ') s++;
    if (!*s) break;
    for (role = 0; role < BR_NUM; role++)
      if (rolechars[role] == *s) break;
    if ((role >= BR_NUM) || (s[1] != ':')) return -1;
    s += 2;
    for (c = 0; s[c] && s[c] != ' '; c++)
    {
      int lev = -1, roll = 0;
      switch (s[c])
      {
        case 'x': lev = BL_NORMAL; break;
        case 'X': lev = BL_ACCENT; break;
        case 'g': lev = BL_SOFT; break;
        case 'r': lev = BL_NORMAL; roll = 1; break;
        case '.': break;
        default: return -1;
      }
      if ((lev >= 0) && (n < maxhits))
      {
        hits[n].pos32 = c * 2;
        hits[n].role = role;
        hits[n].level = lev;
        n++;
        if ((roll) && (n < maxhits))
        {
          hits[n] = hits[n - 1];
          hits[n].pos32++;
          n++;
        }
      }
      l++;
    }
    if ((l != 16) && (l != 32)) return -1;
    if ((len >= 0) && (l != len)) return -1;
    len = l;
    s += c;
  }
  if (steps) *steps = len < 0 ? 16 : len;
  return n;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

typedef struct
{
  int genre, sub, var;
  int kit;            // -1 = the kit of the subgenre
  int rows16;         // rows per 16th note, 0 = auto (from the tempo)
  int swing;          // -1 = template, else 50 ... 75
  int fill;           // 1 = fill in the last bar
  int variation;      // 0 = original, else random seed
  int beatonly;       // preview: mute the other channels
} BEATSET;

static BEATSET bs = {0, 0, 0, -1, 0, -1, 0, 0, 1};
static int assign[BR_NUM];        // instrument per role, 0 = generate a new one
static int genslot[BR_NUM][BL_NUM];

static const int swingvalues[] = {-1, 50, 58, 62, 66, 75};
#define NUMSWING 6

static int numgenres(void)
{
  int n = 0;
  while (beatcatalog[n].name) n++;
  return n;
}

static int numsubs(int g)
{
  int n = 0;
  while ((n < BEAT_MAXSUB) && (beatcatalog[g].sub[n].name)) n++;
  return n;
}

static int numvars(int g, int s)
{
  int n = 0;
  while ((n < BEAT_MAXVAR) && (beatcatalog[g].sub[s].var[n].name)) n++;
  return n;
}

static const BEATSUBGENRE *cursub(void) { return &beatcatalog[bs.genre].sub[bs.sub]; }
static const BEATVARIANT *curvar(void) { return &cursub()->var[bs.var]; }
static int curkit(void) { return bs.kit >= 0 ? bs.kit : cursub()->kit; }
static int curswing(void) { return bs.swing >= 0 ? bs.swing : curvar()->swing; }

// ---------------------------------------------------------------------------
// Song information
// ---------------------------------------------------------------------------

static int chnpattern(int chn)
{
  return editorUndoInfo.editorInfo[chn].epnum;
}

// tempo (frames per row) of the song around the pattern: first tempo command
// in the current patterns, else the default tempo
static int songtempo(int chn)
{
  int c, r, p;
  for (c = -1; c < numchannels; c++)
  {
    p = chnpattern(c < 0 ? chn : c);
    for (r = 0; r < pattlen[p]; r++)
    {
      unsigned char *q = &pattern[p][r * 4];
      if ((q[2] == CMD_SETTEMPO) && (q[3] >= 2))
        return (q[3] & 0x7f) >= 2 ? (q[3] & 0x7f) : 6;
    }
  }
  if ((instr[MAX_INSTR - 1].ad >= 2) && (!instr[MAX_INSTR - 1].ptr[WTBL])) return instr[MAX_INSTR - 1].ad;
  return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

static int autorows16(int chn)
{
  // a 16th note of about 120 ms (125 BPM) = 6 frames at 1x speed
  int frames = editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
  int t = songtempo(chn);
  int r = (frames + t / 2) / t;
  if (r < 1) r = 1;
  if (r > 4) r = 4;
  return r;
}

static int currows16(int chn)
{
  return bs.rows16 ? bs.rows16 : autorows16(chn);
}

static int patternuses(int patt)
{
  int c, i, n = 0;
  for (c = 0; c < numchannels; c++)
    for (i = 0; i < songlen[editorInfo.esnum][c]; i++)
      if (songorder[editorInfo.esnum][c][i] == patt) n++;
  return n;
}

// ---------------------------------------------------------------------------
// Instruments: detection by name, generated drum kits
// ---------------------------------------------------------------------------

static int instrempty(int i)
{
  const INSTR *in = &instr[i];
  return (!in->ad) && (!in->sr) && (!in->ptr[0]) && (!in->ptr[1]) && (!in->ptr[2]) && (!in->ptr[3]) &&
         (!in->vibdelay) && (!in->name[0]);
}

static int contains(const char *lname, const char *w)
{
  return strstr(lname, w) != NULL;
}

// role and level of an instrument from its name, -1 = no drum
static int detectrole(int i, int *level)
{
  char n[MAX_INSTRNAMELEN + 1];
  int c;
  for (c = 0; c < MAX_INSTRNAMELEN && instr[i].name[c]; c++) n[c] = tolower((unsigned char)instr[i].name[c]);
  n[c] = 0;
  if (level) *level = contains(n, "soft") || contains(n, "ghost") ? BL_SOFT : (contains(n, " acc") ? BL_ACCENT : BL_NORMAL);
  if (!n[0]) return -1;
  if (contains(n, "crash") || contains(n, "cymbal")) return BR_CRASH;
  if (contains(n, "clap")) return BR_CLAP;
  if (contains(n, "snare") || !strncmp(n, "sd", 2)) return BR_SNARE;
  if (contains(n, "rim")) return BR_RIM;
  if (contains(n, "ohat") || (contains(n, "open") && (contains(n, "hat") || contains(n, "hh")))) return BR_OHAT;
  if (contains(n, "hat") || contains(n, "hh")) return BR_CHAT;
  if (contains(n, "tom"))
  {
    if (contains(n, "lo") || contains(n, "floor")) return BR_TOML;
    if (contains(n, "md") || contains(n, "mid")) return BR_TOMM;
    return BR_TOMH;
  }
  if (contains(n, "kick") || contains(n, "bass drum") || contains(n, "bassdrum") || !strncmp(n, "bd", 2)) return BR_KICK;
  return -1;
}

static void autoassign(void)
{
  int r, i, lev;
  for (r = 0; r < BR_NUM; r++)
  {
    assign[r] = 0;
    for (i = 1; i < MAX_INSTR - 1; i++)
    {
      if ((detectrole(i, &lev) == r) && (lev == BL_NORMAL))
      {
        assign[r] = i;
        break;
      }
    }
  }
}

typedef struct
{
  unsigned char l[32], r[32];
  int n;
} DTBL;

typedef struct
{
  char name[MAX_INSTRNAMELEN];
  int ad, sr, firstwave, gatetimer;
  DTBL w, p;
} DRUMINS;

#define W_TRI 0x11
#define W_PUL 0x41
#define W_NOI 0x81
#define W_SILENT 0xe1
#define AN(n) (0x80 + (n))

static void dadd(DTBL *t, int l, int r)
{
  if (t->n < 31)
  {
    t->l[t->n] = l;
    t->r[t->n] = r;
    t->n++;
  }
}

// jump to row (0-based) of this program, or stop (row < 0)
static void djump(DTBL *t, int row)
{
  dadd(t, 0xff, row < 0 ? 0 : row + 1);
}

static void dpulse(DTBL *t, int pw)
{
  dadd(t, 0x80 | ((pw >> 8) & 0x0f), pw & 0xff);
  djump(t, -1);
}

static void falling(DTBL *w, int wave, int from, int to, int step)
{
  int n;
  for (n = from; n > to; n -= step) dadd(w, wave, AN(n));
  dadd(w, wave, AN(to));
}

static void makedrum(DRUMINS *d, int role, int level, int kit, int tempo)
{
  DTBL *w = &d->w, *p = &d->p;
  int decay = 8, attack = 0, c;
  int tone = (kit == KIT_808 || kit == KIT_909) ? W_TRI : W_PUL;
  static const char *lev[BL_NUM] = {" soft", "", " acc"};

  memset(d, 0, sizeof *d);
  d->firstwave = 0x09;
  d->gatetimer = tempo >= 3 ? 2 : 1;
  switch (role)
  {
    case BR_KICK:
    decay = (kit == KIT_808) ? 9 : 8;
    if (kit != KIT_808) dadd(w, W_NOI, AN(kit == KIT_909 ? 84 : (kit == KIT_ROCK ? 76 : 80)));
    if (kit == KIT_808) falling(w, W_TRI, 52, 28, 3);
    else if (kit == KIT_909) falling(w, W_TRI, 50, 29, 4);
    else if (kit == KIT_ROCK) falling(w, W_PUL, 46, 26, 5);
    else falling(w, W_PUL, 48, 24, 7);
    djump(w, -1);
    dpulse(p, kit == KIT_ROCK ? 0x600 : 0x800);
    break;

    case BR_SNARE:
    decay = 8;
    dadd(w, W_NOI, AN(kit == KIT_909 ? 92 : (kit == KIT_CHIP ? 86 : 90)));
    dadd(w, tone, AN(kit == KIT_909 ? 50 : (kit == KIT_ROCK ? 48 : 44)));
    if (kit != KIT_CHIP) dadd(w, tone, AN(kit == KIT_909 ? 47 : (kit == KIT_ROCK ? 45 : 42)));
    dadd(w, W_NOI, AN(kit == KIT_909 ? 86 : (kit == KIT_CHIP ? 78 : 82)));
    dadd(w, W_NOI, AN(kit == KIT_909 ? 80 : (kit == KIT_CHIP ? 74 : 79)));
    djump(w, w->n - 1);
    dpulse(p, kit == KIT_909 ? 0x500 : 0x700);
    break;

    case BR_CLAP:
    decay = 7;
    dadd(w, W_NOI, AN(88));
    dadd(w, W_SILENT, 0x80);
    dadd(w, W_NOI, AN(88));
    dadd(w, W_SILENT, 0x80);
    dadd(w, W_NOI, AN(86));
    djump(w, w->n - 1);
    break;

    case BR_CHAT:
    case BR_OHAT:
    decay = role == BR_OHAT ? ((kit == KIT_909 || kit == KIT_ROCK) ? 8 : 7) : (kit == KIT_ROCK ? 5 : (kit == KIT_909 ? 4 : 3));
    dadd(w, W_NOI, AN(95));
    dadd(w, W_NOI, AN(92));
    djump(w, 1);
    break;

    case BR_TOMH:
    case BR_TOMM:
    case BR_TOML:
    {
      int start = role == BR_TOMH ? 54 : (role == BR_TOMM ? 47 : 40);
      decay = 9;
      if ((kit == KIT_909) || (kit == KIT_ROCK)) dadd(w, W_NOI, AN(80));
      for (c = 0; c < 5; c++) dadd(w, tone, AN(start - c * 2));
      dadd(w, tone, AN(start - 9));
      djump(w, -1);
      dpulse(p, 0x600);
    }
    break;

    case BR_RIM:
    decay = 5;
    dadd(w, W_NOI, AN(90));
    dadd(w, W_PUL, AN(72));
    dadd(w, W_PUL & 0xfe, AN(72));
    djump(w, -1);
    dpulse(p, 0x400);
    break;

    case BR_CRASH:
    decay = kit == KIT_CHIP ? 9 : 0xa;
    dadd(w, W_NOI, AN(84));
    dadd(w, W_TRI, AN(44));
    dadd(w, W_TRI, AN(36));
    dadd(w, W_NOI, AN(93));
    dadd(w, W_NOI, AN(89));
    djump(w, w->n - 1);
    break;
  }
  if (level == BL_ACCENT)
  {
    if (decay < 0xf) decay++;
  }
  d->sr = 0x00;
  if (level == BL_SOFT)
  {
    // soft (ghost) note: slow attack, the gate goes off after the first
    // table frame, so the envelope never reaches the full level
    attack = 7;
    if (decay > 1) decay--;
    d->sr = 0x03;
    for (c = 1; c < w->n; c++)
      if (w->l[c] != 0xff) w->l[c] &= 0xfe;
  }
  d->ad = (attack << 4) | decay;
  snprintf(d->name, MAX_INSTRNAMELEN, "%s %s%s", beatrolenames[role], kitnames[kit], lev[level]);
}

static int tablespace(const DRUMINS *d)
{
  return (gettablelen(WTBL) + d->w.n <= MAX_TABLELEN) && (gettablelen(PTBL) + d->p.n <= MAX_TABLELEN);
}

// write a generated drum into a free instrument slot; returns the slot or 0
static int adddrum(const DRUMINS *d)
{
  int i, t, c;
  for (i = 1; i < MAX_INSTR - 1; i++)
    if (instrempty(i)) break;
  if ((i >= MAX_INSTR - 1) || (!tablespace(d))) return 0;
  instr[i].ad = d->ad;
  instr[i].sr = d->sr;
  instr[i].firstwave = d->firstwave;
  instr[i].gatetimer = d->gatetimer;
  instr[i].vibdelay = 0;
  memset(instr[i].name, 0, MAX_INSTRNAMELEN);
  strncpy(instr[i].name, d->name, MAX_INSTRNAMELEN - 1);
  for (t = 0; t < 2; t++)
  {
    const DTBL *s = t ? &d->p : &d->w;
    int start = gettablelen(t);
    instr[i].ptr[t] = 0;
    if (!s->n) continue;
    for (c = 0; c < s->n; c++)
    {
      ltable[t][start + c] = s->l[c];
      rtable[t][start + c] = s->r[c];
      if ((s->l[c] == 0xff) && (s->r[c])) rtable[t][start + c] = s->r[c] + start;
    }
    instr[i].ptr[t] = start + 1;
  }
  instr[i].ptr[FTBL] = 0;
  instr[i].ptr[STBL] = 0;
  return i;
}

// existing instrument of a role with the wanted level (same name family)
static int findlevel(int role, int level)
{
  int i, lev;
  for (i = 1; i < MAX_INSTR - 1; i++)
    if ((detectrole(i, &lev) == role) && (lev == level)) return i;
  return 0;
}

// ---------------------------------------------------------------------------
// Rendering: template -> one event per pattern row
// ---------------------------------------------------------------------------

typedef struct
{
  int role;           // -1 = nothing
  int level;
} ROWEV;

static unsigned rngstate;
static int rnd100(void)
{
  rngstate = rngstate * 1103515245u + 12345u;
  return (rngstate >> 16) % 100;
}

// template with the random variation applied
static int varianthits(BEATHIT *hits, int *steps)
{
  int n = beat_parse(curvar()->grid, hits, MAXHITS, steps);
  int s, k, hassnare = 0, hashat = 0;
  if (n < 0) return n;
  if (!bs.variation) return n;
  rngstate = bs.variation * 7919u;
  for (k = 0; k < n; k++)
  {
    if (hits[k].role == BR_SNARE) hassnare = 1;
    if (hits[k].role == BR_CHAT) hashat = 1;
  }
  // remove some hats, double some into rolls
  for (k = 0; k < n; k++)
  {
    if ((hits[k].role == BR_CHAT) && (rnd100() < 12))
    {
      hits[k] = hits[--n];
      k--;
    }
    else if ((hits[k].role == BR_CHAT) && (!(hits[k].pos32 & 1)) && (rnd100() < 10) && (n < MAXHITS))
    {
      hits[n] = hits[k];
      hits[n].pos32++;
      n++;
    }
  }
  for (s = 0; s < *steps; s++)
  {
    int used = 0;
    for (k = 0; k < n; k++)
      if (hits[k].pos32 / 2 == s) used = 1;
    if (used || (n >= MAXHITS - 1)) continue;
    // ghost snares on free off-beat 16ths, extra kicks after a kick
    if ((hassnare) && (s & 1) && (rnd100() < 18))
    {
      hits[n].pos32 = s * 2; hits[n].role = BR_SNARE; hits[n].level = BL_SOFT; n++;
    }
    else if ((s > 0) && (rnd100() < 10))
    {
      for (k = 0; k < n; k++)
        if ((hits[k].role == BR_KICK) && (hits[k].pos32 / 2 == s - 1)) break;
      if (k < n) { hits[n].pos32 = s * 2; hits[n].role = BR_KICK; hits[n].level = BL_NORMAL; n++; }
    }
    else if ((hashat) && (!(s & 1)) && (rnd100() < 8))
    {
      hits[n].pos32 = s * 2; hits[n].role = BR_OHAT; hits[n].level = BL_NORMAL; n++;
    }
  }
  return n;
}

static void place(ROWEV *ev, int rows, int row, const BEATHIT *h)
{
  if ((row < 0) || (row >= rows)) return;
  if ((ev[row].role < 0) || (roleprio[h->role] > roleprio[ev[row].role]))
  {
    ev[row].role = h->role;
    ev[row].level = h->level;
  }
}

// events for a pattern of "rows" rows; returns 0 on a template error
static int render(ROWEV *ev, int rows, int r16)
{
  static BEATHIT hits[MAXHITS], fhits[MAXHITS];
  int n, fn = 0, steps, fsteps = 16, base, k, swingoff, fillstart = rows;
  int sw = curswing();

  n = varianthits(hits, &steps);
  if (n < 0) return 0;
  if ((bs.fill) && (cursub()->fill))
  {
    fn = beat_parse(cursub()->fill, fhits, MAXHITS, &fsteps);
    if (fn < 0) return 0;
    if (rows >= 16 * r16) fillstart = rows - 16 * r16;
  }
  // swing: odd 16th notes later (only with 2 or more rows per 16th)
  swingoff = (2 * r16 * sw + 50) / 100 - r16;
  if (swingoff < 0) swingoff = 0;
  if (swingoff >= r16) swingoff = r16 - 1;

  for (k = 0; k < rows; k++) ev[k].role = -1;
  for (base = 0; base < fillstart; base += steps * r16)
  {
    for (k = 0; k < n; k++)
    {
      int row = base + hits[k].pos32 * r16 / 2;
      if ((hits[k].pos32 / 2) & 1) row += swingoff;
      if (row < fillstart) place(ev, rows, row, &hits[k]);
    }
  }
  for (k = 0; k < fn; k++)
  {
    int row = fillstart + fhits[k].pos32 * r16 / 2;
    if ((fhits[k].pos32 / 2) & 1) row += swingoff;
    if (fhits[k].pos32 < 32) place(ev, rows, row, &fhits[k]);
  }
  return 1;
}

// Write the beat into the pattern of channel chn (whole pattern). Missing
// instruments are generated. Returns the number of rows, 0 on error.
static int writebeat(int chn, char *err, int errsize)
{
  static ROWEV ev[MAX_PATTROWS];
  int patt = chnpattern(chn), rows = pattlen[patt], r16 = currows16(chn);
  int need[BR_NUM][BL_NUM], r, l, kit = curkit(), tempo = songtempo(chn);

  if (!render(ev, rows, r16))
  {
    snprintf(err, errsize, "Template error in %s / %s", cursub()->name, curvar()->name);
    return 0;
  }
  memset(need, 0, sizeof need);
  for (r = 0; r < rows; r++)
    if (ev[r].role >= 0) need[ev[r].role][ev[r].level] = 1;

  // instruments per role and level
  for (r = 0; r < BR_NUM; r++)
  {
    for (l = 0; l < BL_NUM; l++)
    {
      genslot[r][l] = 0;
      if (!need[r][l]) continue;
      if (assign[r])
      {
        // an assigned instrument; soft / accent variants of it if they exist
        int v = (l == BL_NORMAL) ? 0 : findlevel(r, l);
        genslot[r][l] = v ? v : assign[r];
      }
      else
      {
        DRUMINS d;
        makedrum(&d, r, l, kit, tempo);
        genslot[r][l] = adddrum(&d);
        if (!genslot[r][l])
        {
          snprintf(err, errsize, "No room for the instrument \"%s\" (instruments or tables full)", d.name);
          return 0;
        }
      }
    }
  }
  // whole pattern: notes, instruments and commands are replaced; tempo
  // commands stay
  for (r = 0; r < rows; r++)
  {
    unsigned char *q = &pattern[patt][r * 4];
    q[0] = REST;
    q[1] = 0;
    if ((q[2] != CMD_SETTEMPO) && (q[2] != CMD_FUNKTEMPO))
    {
      q[2] = 0;
      q[3] = 0;
    }
    if (ev[r].role >= 0)
    {
      q[0] = FIRSTNOTE + rolenote[ev[r].role];
      q[1] = genslot[ev[r].role][ev[r].level];
    }
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Preview: song data snapshot, pattern loop, restore
// ---------------------------------------------------------------------------

static void *prevdata = NULL;
static int prevlen[MAX_PATT];
static unsigned char prevmute[MAX_CHN];
static int previewing = 0;

static void restoresong(void)
{
  snd_lock();
  songdata_restore(prevdata);
  memcpy(pattlen, prevlen, sizeof prevlen);
  snd_unlock();
}

static void stoppreview(void)
{
  int c;
  if (!previewing) return;
  snd_lock();
  stopsong();
  snd_unlock();
  while (isplaying()) SDL_Delay(5);
  restoresong();
  for (c = 0; c < MAX_CHN; c++) chn[c].mute = prevmute[c];
  songdata_free(prevdata);
  prevdata = NULL;
  previewing = 0;
}

// start the preview, or rewrite the beat while it keeps playing
static int preview(int ch, char *err, int errsize)
{
  int c, ok;
  if (!previewing)
  {
    if (isplaying()) stopsong();
    prevdata = songdata_save();
    if (!prevdata)
    {
      snprintf(err, errsize, "Out of memory");
      return 0;
    }
    memcpy(prevlen, pattlen, sizeof prevlen);
    for (c = 0; c < MAX_CHN; c++) prevmute[c] = chn[c].mute;
  }
  else restoresong();
  snd_lock();
  ok = writebeat(ch, err, errsize);
  for (c = 0; c < numchannels; c++)
    chn[c].mute = bs.beatonly ? (c != ch) : prevmute[c];
  snd_unlock();
  if (!ok)
  {
    if (previewing) stoppreview();
    else
    {
      restoresong();
      songdata_free(prevdata);
      prevdata = NULL;
    }
    return 0;
  }
  if (!previewing)
  {
    previewing = 1;
    initsongpos(editorInfo.esnum, PLAY_PATTERN, 0);
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Command line / tests
// ---------------------------------------------------------------------------

int beat_generate_cli(int chn, int genre, int sub, int var, int fill, char *err, int errsize)
{
  if ((genre < 0) || (genre >= numgenres()) || (sub < 0) || (sub >= numsubs(genre)) || (var < 0) || (var >= numvars(genre, sub)))
  {
    snprintf(err, errsize, "No such beat %d,%d,%d", genre, sub, var);
    return 0;
  }
  bs.genre = genre;
  bs.sub = sub;
  bs.var = var;
  bs.fill = fill;
  if (getenv("GG_BEATROWS")) bs.rows16 = atoi(getenv("GG_BEATROWS"));
  if (getenv("GG_BEATVARIATION")) bs.variation = atoi(getenv("GG_BEATVARIATION"));
  autoassign();
  return writebeat(chn, err, errsize);
}

// all templates must parse (beat_selftest via GG_BEATTEST)
int beat_selftest(void)
{
  static BEATHIT h[MAXHITS];
  int g, s, v, steps, fail = 0, count = 0;
  for (g = 0; g < numgenres(); g++)
    for (s = 0; s < numsubs(g); s++)
    {
      if (beat_parse(beatcatalog[g].sub[s].fill, h, MAXHITS, &steps) < 0 || steps != 16)
      {
        printf("FAIL fill %s / %s\n", beatcatalog[g].name, beatcatalog[g].sub[s].name);
        fail++;
      }
      for (v = 0; v < numvars(g, s); v++)
      {
        count++;
        if (beat_parse(beatcatalog[g].sub[s].var[v].grid, h, MAXHITS, &steps) <= 0)
        {
          printf("FAIL %s / %s / %s\n", beatcatalog[g].name, beatcatalog[g].sub[s].name, beatcatalog[g].sub[s].var[v].name);
          fail++;
        }
      }
    }
  printf("%d genres, %d beats, %d errors\n", numgenres(), count, fail);
  return !fail;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

// next / previous used instrument slot (0 = new) for the assignment list
static int nextslot(int cur, int dir)
{
  int i = cur;
  do
  {
    i += dir;
    if (i < 0) i = MAX_INSTR - 2;
    if (i > MAX_INSTR - 2) i = 0;
    if ((i == 0) || (!instrempty(i))) return i;
  } while (i != cur);
  return 0;
}

static void drawgrid(int x, int y, int w, int chn, int *usedroles)
{
  static BEATHIT hits[MAXHITS];
  static ROWEV ev[MAX_PATTROWS];
  int n, steps, k, r, s, line = 0;
  int r16 = currows16(chn);
  char buf[160];
  int used[BR_NUM];

  memset(used, 0, sizeof used);
  n = varianthits(hits, &steps);
  if (n < 0) n = 0;
  for (k = 0; k < n; k++) used[hits[k].role] = 1;

  // header: beat numbers
  printtext(x, y, MKCOL(14, C_PANEL), "Beat");
  for (s = 0; s < steps; s++)
  {
    buf[0] = (s % 4) ? '.' : '1' + (s / 4) % 4;
    buf[1] = 0;
    printtext(x + 8 + s * 2, y, MKCOL((s % 16) < 4 || ((s % 16) >= 8 && (s % 16) < 12) ? 14 : 15, C_PANEL), buf);
  }
  line = 1;
  for (r = BR_NUM - 1; r >= 0; r--)
  {
    static const int order[BR_NUM] = {BR_CHAT, BR_OHAT, BR_RIM, BR_TOML, BR_TOMM, BR_TOMH, BR_KICK, BR_CLAP, BR_SNARE, BR_CRASH};
    int role = order[r];
    if (!used[role]) continue;
    printtext(x, y + line, MKCOL(8, C_PANEL), beatrolenames[role]);
    for (s = 0; s < steps; s++)
    {
      int ch = '.', col = 11, lev = -1;
      for (k = 0; k < n; k++)
        if ((hits[k].role == role) && (hits[k].pos32 / 2 == s))
        {
          if (lev < hits[k].level) lev = hits[k].level;
          if (hits[k].pos32 & 1) ch = 'r';
        }
      if (lev >= 0)
      {
        if (ch != 'r') ch = lev == BL_ACCENT ? 'X' : (lev == BL_SOFT ? 'g' : 'x');
        col = lev == BL_ACCENT ? 15 : (lev == BL_SOFT ? 12 : 13);
      }
      buf[0] = ch;
      buf[1] = 0;
      printtext(x + 8 + s * 2, y + line, MKCOL(col, C_PANEL), buf);
    }
    line++;
  }
  // what the voice actually plays (one sound per row) for the first loop
  k = bs.fill;
  bs.fill = 0;
  r = render(ev, steps * r16 < MAX_PATTROWS ? steps * r16 : MAX_PATTROWS, r16);
  bs.fill = k;
  if (r)
  {
    printtext(x, y + line, MKCOL(14, C_PANEL), "Voice");
    for (s = 0; s < steps; s++)
    {
      int role = -1, rr;
      for (rr = s * r16; rr < (s + 1) * r16 && rr < MAX_PATTROWS; rr++)
        if ((ev[rr].role >= 0) && ((role < 0) || (roleprio[ev[rr].role] > roleprio[role]))) role = ev[rr].role;
      buf[0] = role >= 0 ? rolechars[role] : '-';
      buf[1] = 0;
      printtext(x + 8 + s * 2, y + line, MKCOL(role >= 0 ? 15 : 11, C_PANEL), buf);
    }
  }
  if (usedroles) memcpy(usedroles, used, sizeof used);
  (void)w;
}

void beatdialog(void)
{
  int w = 108, h = 38;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 2;
  int ch = editorInfo.epchn;
  char msg[160] = "";
  int msgcol = 7;
  enum { S_OPTIONS = 9, S_ROLES = S_OPTIONS + BR_NUM, S_PREVIEW = S_ROLES, S_INSERT, S_CANCEL, S_NUM };

  editorInfo.editmode = EDIT_PATTERN;
  if (isplaying()) stopsong();
  autoassign();
  key = 0;
  rawkey = 0;

  for (;;)
  {
    char buf[200], err[160];
    int c, hitrow = -1, hitbtn = -1, changed = 0;
    int patt = chnpattern(ch), rows = pattlen[patt], r16 = currows16(ch), steps = 16;
    int used[BR_NUM];
    static BEATHIT tmp[MAXHITS];
    int btnx[3] = {x0 + 3, x0 + 17, x0 + w - 13};
    const char *btntext[3] = {previewing ? " " GS_STOP " Stop     " : " " GS_PLAY " Preview  ", " Insert ", "  Cancel  "};

    beat_parse(curvar()->grid, tmp, MAXHITS, &steps);
    printstatus();
    dlg_drawframe(x0, y0, w, h, "Beat editor");

    // options (left)
    for (c = 0; c < S_OPTIONS; c++)
    {
      int isel = (c == sel), bg = isel ? C_SELECT : C_PANEL, ry = y0 + 2 + c;
      switch (c)
      {
        case 0: sprintf(buf, "Genre        " GS_PREV " %s " GS_NEXT "  %d/%d", beatcatalog[bs.genre].name, bs.genre + 1, numgenres()); break;
        case 1: sprintf(buf, "Subgenre     " GS_PREV " %s " GS_NEXT "  %d/%d", cursub()->name, bs.sub + 1, numsubs(bs.genre)); break;
        case 2: sprintf(buf, "Beat         " GS_PREV " %s " GS_NEXT "  %d/%d", curvar()->name, bs.var + 1, numvars(bs.genre, bs.sub)); break;
        case 3:
          if (bs.kit < 0) sprintf(buf, "Drum sounds  " GS_PREV " Auto (%s) " GS_NEXT, kitnames[cursub()->kit]);
          else sprintf(buf, "Drum sounds  " GS_PREV " %s " GS_NEXT, kitnames[bs.kit]);
          break;
        case 4:
          if (bs.rows16) sprintf(buf, "Rows / 16th  " GS_PREV " %d " GS_NEXT, bs.rows16);
          else sprintf(buf, "Rows / 16th  " GS_PREV " Auto (%d, tempo %d) " GS_NEXT, autorows16(ch), songtempo(ch));
          break;
        case 5:
          if (bs.swing < 0) sprintf(buf, "Swing        " GS_PREV " Beat (%s) " GS_NEXT, curvar()->swing > 50 ? "shuffle" : "straight");
          else if (bs.swing == 50) sprintf(buf, "Swing        " GS_PREV " Straight " GS_NEXT);
          else sprintf(buf, "Swing        " GS_PREV " %d%% " GS_NEXT, bs.swing);
          if ((curswing() > 50) && (r16 < 2)) strcat(buf, " (needs 2+ rows)");
          break;
        case 6: sprintf(buf, "Fill         " GS_PREV " %s " GS_NEXT, bs.fill ? "In the last bar" : "Off"); break;
        case 7:
          if (bs.variation) sprintf(buf, "Variation    " GS_PREV " #%d " GS_NEXT "  (R = new)", bs.variation);
          else sprintf(buf, "Variation    " GS_PREV " Original " GS_NEXT "  (R = new)");
          break;
        case 8: sprintf(buf, "Preview      " GS_PREV " %s " GS_NEXT, bs.beatonly ? "Beat only" : "With the other voices"); break;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), 58);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + 60)) hitrow = c;
    }

    // grid
    drawgrid(x0 + 3, y0 + 13, w - 6, ch, used);

    // instruments (right)
    printtext(x0 + 63, y0 + 2 - 1 + 0, MKCOL(14, C_PANEL), "");
    for (c = 0; c < BR_NUM; c++)
    {
      int s = S_OPTIONS + c, isel = (s == sel), bg = isel ? C_SELECT : C_PANEL, ry = y0 + 2 + c;
      char nm[MAX_INSTRNAMELEN + 1];
      if (assign[c])
      {
        memcpy(nm, instr[assign[c]].name, MAX_INSTRNAMELEN);
        nm[MAX_INSTRNAMELEN] = 0;
        sprintf(buf, "%-6s " GS_PREV " %02X %-15s " GS_NEXT, beatrolenames[c], assign[c], nm);
      }
      else sprintf(buf, "%-6s " GS_PREV " New: %s %s " GS_NEXT, beatrolenames[c], beatrolenames[c], kitnames[curkit()]);
      printblankc(x0 + 62, ry, MKCOL(8, bg), w - 64);
      printtext(x0 + 63, ry, MKCOL(isel ? 15 : (used[c] ? 8 : 3), bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 62) && (mousex < x0 + w - 2)) hitrow = s;
    }

    // target
    {
      int y = y0 + 27;
      int uses = patternuses(patt);
      printtext(x0 + 3, y, MKCOL(14, C_PANEL), "Target:");
      sprintf(buf, "Channel %d.%d, pattern %02X: %d rows = %d.%d bars (%d rows per 16th). The whole pattern is replaced.",
              ch / 3 + 1, ch % 3 + 1, patt, rows, rows / (16 * r16), (rows % (16 * r16)) * 4 / (16 * r16), r16);
      printtext(x0 + 12, y, MKCOL(8, C_PANEL), buf);
      if (uses > 1)
      {
        sprintf(buf, "The pattern is used %d times in the orderlist: the beat changes in all places.", uses);
        printtext(x0 + 12, y + 1, MKCOL(10, C_PANEL), buf);
      }
      printtext(x0 + 3, y + 2, MKCOL(3, C_PANEL),
                "x hit  X accent  g ghost  r roll. One sound per row: crash > snare > clap > kick > toms > rim > hats.");
      if (msg[0]) printtext(x0 + 3, y + 4, MKCOL(msgcol, C_PANEL), msg);
    }

    for (c = 0; c < 3; c++)
    {
      int isel = (sel == S_PREVIEW + c);
      printtext(btnx[c], y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), btntext[c]);
      if ((mousey == y0 + h - 2) && (mousex >= btnx[c]) && (mousex < btnx[c] + (int)strlen(btntext[c]))) hitbtn = c;
    }
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL),
              "UP/DOWN select, LEFT/RIGHT change, SPACE/F1 preview on/off, R variation, ENTER insert, ESC cancel");

    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      stoppreview();
      return;
    }

    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn >= 0)
      {
        sel = S_PREVIEW + hitbtn;
        rawkey = KEY_ENTER;
      }
      else if (hitrow >= 0)
      {
        sel = hitrow;
        rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        if (ui_arrowat(mousex, mousey) < 0) rawkey = KEY_LEFT;
        if (ui_arrowat(mousex, mousey) > 0) rawkey = KEY_RIGHT;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h))
      {
        stoppreview();
        break;
      }
    }

    if ((rawkey == KEY_F1) || (rawkey == KEY_SPACE))
    {
      sel = sel >= S_PREVIEW ? S_PREVIEW : sel;
      if (previewing) stoppreview();
      else if (preview(ch, err, sizeof err)) msg[0] = 0;
      else { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
      continue;
    }
    if (rawkey == KEY_R)
    {
      bs.variation = (bs.variation % 999) + 1 + (rand() % 50);
      changed = 1;
    }

    switch (rawkey)
    {
      case KEY_ESC:
      stoppreview();
      goto done;

      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_DOWN:
      case KEY_TAB:
      if (sel < S_NUM - 1) sel++;
      break;

      case KEY_PGUP:
      case KEY_PGDN:
      bs.var = (bs.var + (rawkey == KEY_PGDN ? 1 : -1) + numvars(bs.genre, bs.sub)) % numvars(bs.genre, bs.sub);
      bs.variation = 0;
      changed = 1;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      {
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        if (sel >= S_PREVIEW)
        {
          sel += dir;
          if (sel < S_PREVIEW) sel = S_PREVIEW;
          if (sel >= S_NUM) sel = S_NUM - 1;
          break;
        }
        changed = 1;
        switch (sel)
        {
          case 0:
          bs.genre = (bs.genre + dir + numgenres()) % numgenres();
          bs.sub = 0;
          bs.var = 0;
          bs.variation = 0;
          break;
          case 1:
          bs.sub = (bs.sub + dir + numsubs(bs.genre)) % numsubs(bs.genre);
          bs.var = 0;
          bs.variation = 0;
          break;
          case 2:
          bs.var = (bs.var + dir + numvars(bs.genre, bs.sub)) % numvars(bs.genre, bs.sub);
          bs.variation = 0;
          break;
          case 3:
          bs.kit += dir;
          if (bs.kit < -1) bs.kit = KIT_NUM - 1;
          if (bs.kit >= KIT_NUM) bs.kit = -1;
          break;
          case 4:
          bs.rows16 = (bs.rows16 + dir + 5) % 5;
          break;
          case 5:
          {
            int i;
            for (i = 0; i < NUMSWING; i++)
              if (swingvalues[i] == bs.swing) break;
            i = (i + dir + NUMSWING) % NUMSWING;
            bs.swing = swingvalues[i];
          }
          break;
          case 6: bs.fill ^= 1; break;
          case 7:
          bs.variation += dir;
          if (bs.variation < 0) bs.variation = 0;
          break;
          case 8: bs.beatonly ^= 1; break;
          default:
          if ((sel >= S_OPTIONS) && (sel < S_ROLES))
            assign[sel - S_OPTIONS] = nextslot(assign[sel - S_OPTIONS], dir);
          break;
        }
      }
      break;

      case KEY_ENTER:
      if (sel == S_CANCEL)
      {
        stoppreview();
        goto done;
      }
      if (sel == S_PREVIEW)
      {
        if (previewing) stoppreview();
        else if (!preview(ch, err, sizeof err)) { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
        break;
      }
      // insert (one undo step)
      {
        void *ed;
        int r;
        stoppreview();
        ed = undoinstrbegin();
        undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
        snd_lock();
        r = writebeat(ch, err, sizeof err);
        snd_unlock();
        undoinstrend(ed);
        if (!r)
        {
          snprintf(msg, sizeof msg, "%s", err);
          msgcol = 10;
          break;
        }
        songmodified = 1;
        autoassign();
        snprintf(buf, sizeof buf, "Beat \"%s / %s\" written to channel %d.%d, pattern %02X (CTRL+D)", cursub()->name, curvar()->name,
                 ch / 3 + 1, ch % 3 + 1, patt);
        setstatusmessage(buf);
        goto done;
      }
    }
    if ((changed) && (previewing))
    {
      if (!preview(ch, err, sizeof err)) { snprintf(msg, sizeof msg, "%s", err); msgcol = 10; }
    }
  }
done:
  key = 0;
  rawkey = 0;
  printmainscreen();
}

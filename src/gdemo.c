//
// C64 GrooveGoat - generator of the bundled multi-SID demo song
//
// Builds "GrooveGoat Demo" (4 SIDs, 12 voices) from instruments of the
// instrument library and saves it as a .ggt file:
//   groovegoat --makedemo=demo.ggt
//

#include "goattrk2.h"

#define DEMO_SECTIONS 6
#define SECTION_ROWS 64
#define BAR_ROWS 16
#define TOTALROWS (DEMO_SECTIONS * SECTION_ROWS)
#define DEMO_CHN 12

enum
{
  I_BASS = 1, I_KICK, I_SNARE, I_HAT, I_ARPMIN, I_ARPMAJ, I_PAD, I_LEAD, I_ECHO, I_BELL, I_STRINGS, I_PAD2
};

static const char *demoinstr[][2] =
{
  {"8580/Bass", "bass_fat.ins"},
  {"8580/Drums/Kick", "kick_909_deep.ins"},
  {"8580/Drums/Snare", "snare_909.ins"},
  {"8580/Drums/HiHat", "hat_noise_8_206.ins"},
  {"8580/Arpeggio", "chip_fast_min.ins"},
  {"8580/Arpeggio", "chip_fast_maj.ins"},
  {"8580/Pad", "pwm_pad.ins"},
  {"8580/Synth Lead", "hubbard_lead.ins"},
  {"8580/Synth Lead", "glass_lead.ins"},
  {"8580/Mallet", "bell.ins"},
  {"8580/Strings", "strings.ins"},
  {"8580/Pad", "soft_pad.ins"},
};

static unsigned char tnote[DEMO_CHN][TOTALROWS];
static unsigned char tinstr[DEMO_CHN][TOTALROWS];

// Note number: octave 0-7, semitone 0-11 (C = 0)
#define N(o, s) (FIRSTNOTE + (o) * 12 + (s))
enum { C = 0, Cs, D, Ds, E, F_, Fs, G, Gs, A, As, B };

// Chords of the progression Am - F - C - G (root, third, fifth in octave 3)
static const int chordroot[4] = {A, F_, C, G};
static const int chordminor[4] = {1, 0, 0, 0};

static void put(int ch, int row, int note, int ins)
{
  // Arrangement over the SIDs (stereo: SID 1 left, SID 2 right, SID 3 and
  // 4 nearer to the center): SID 1 lead, bell, pad fifth / SID 2 arpeggio
  // & pads / SID 3 bass & drums / SID 4 lead echo, strings, arpeggio echo
  static const int chmap[DEMO_CHN] = {6, 7, 8, 3, 4, 5, 0, 9, 1, 2, 10, 11};
  if ((row < 0) || (row >= TOTALROWS)) return;
  ch = chmap[ch];
  tnote[ch][row] = note;
  tinstr[ch][row] = ins;
}

static int sectionhas(int section, int part)
{
  // Parts: 0 pads, 1 arp, 2 bass, 3 drums, 4 hihat, 5 lead, 6 bell, 7 strings, 8 arp echo
  static const unsigned short mask[DEMO_SECTIONS] =
  {
    (1 << 0) | (1 << 1) | (1 << 7),                                     // intro
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 8),    // verse
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 8),
    0x1ff,                                                              // chorus
    0x1ff,
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 7),               // breakdown
  };
  return (mask[section] >> part) & 1;
}

static void composedemo(void)
{
  int s, bar, r;
  // Lead melody over 4 bars: {row, octave, semitone}, row -1 = key off
  static const int melody[][3] =
  {
    {0, 4, A}, {4, 5, C}, {6, 4, B}, {8, 4, A}, {12, 4, E},
    {16, 4, F_}, {20, 4, A}, {24, 5, C}, {26, 4, B}, {28, 4, A},
    {32, 4, G}, {36, 4, E}, {40, 4, G}, {44, 5, C},
    {48, 4, B}, {52, 5, D}, {56, 4, B}, {60, 4, G}, {62, 4, A},
  };
  // Bell counter melody (high chord tones on the offbeats)
  static const int bellnotes[4][4] =
  {
    {E, A, C, A}, {C, F_, A, F_}, {E, G, C, G}, {D, G, B, G}
  };
  // Bass riff relative to the chord root (row, semitone offset)
  static const int bassriff[][2] = {{0, 0}, {3, 0}, {6, 12}, {8, 0}, {10, 7}, {12, 0}, {14, 12}};

  memset(tnote, REST, sizeof tnote);
  memset(tinstr, 0, sizeof tinstr);

  for (s = 0; s < DEMO_SECTIONS; s++)
  {
    int base = s * SECTION_ROWS;
    for (bar = 0; bar < 4; bar++)
    {
      int b = base + bar * BAR_ROWS;
      int root = chordroot[bar];
      int third = root + (chordminor[bar] ? 3 : 4);
      int fifth = root + 7;
      int k;

      // Pads: root, third, fifth (SID 2 voices 2-3, SID 4 voice 1)
      if (sectionhas(s, 0))
      {
        put(4, b, N(3, 0) + root, I_PAD);
        put(5, b, N(3, 0) + third, I_PAD);
        put(9, b, N(3, 0) + fifth, I_PAD2);
        put(4, b + 15, KEYOFF, 0);
        put(5, b + 15, KEYOFF, 0);
        put(9, b + 15, KEYOFF, 0);
      }
      // Arpeggio (SID 2 voice 1), echo an octave higher (SID 4 voice 3)
      if (sectionhas(s, 1))
      {
        int ins = chordminor[bar] ? I_ARPMIN : I_ARPMAJ;
        put(3, b, N(4, 0) + root, ins);
        put(3, b + 6, N(4, 0) + root, ins);
        put(3, b + 12, N(4, 0) + root, ins);
        put(3, b + 15, KEYOFF, 0);
      }
      if (sectionhas(s, 8))
      {
        int ins = chordminor[bar] ? I_ARPMIN : I_ARPMAJ;
        put(11, b + 3, N(5, 0) + root, ins);
        put(11, b + 9, N(5, 0) + root, ins);
        put(11, b + 14, KEYOFF, 0);
      }
      // Bass (SID 1 voice 1)
      if (sectionhas(s, 2))
      {
        for (k = 0; k < 7; k++)
          put(0, b + bassriff[k][0], N(1, 0) + root + bassriff[k][1] + (root < E ? 12 : 0), I_BASS);
      }
      // Drums (SID 1 voice 2): kick 0, 8, 10 / snare 4, 12
      if (sectionhas(s, 3))
      {
        put(1, b + 0, N(4, C), I_KICK);
        put(1, b + 4, N(4, C), I_SNARE);
        put(1, b + 8, N(4, C), I_KICK);
        put(1, b + 10, N(4, C), I_KICK);
        put(1, b + 12, N(4, C), I_SNARE);
        if (bar == 3)
        {
          put(1, b + 14, N(4, C), I_SNARE);
          put(1, b + 15, N(4, C), I_SNARE);
        }
      }
      // Hihat (SID 1 voice 3)
      if (sectionhas(s, 4))
      {
        for (r = 2; r < BAR_ROWS; r += 4)
          put(2, b + r, N(4, C), I_HAT);
      }
      // Bell counter melody (SID 3 voice 3)
      if (sectionhas(s, 6))
      {
        for (k = 0; k < 4; k++)
          put(8, b + 2 + k * 4, N(5, 0) + bellnotes[bar][k], I_BELL);
      }
      // Strings (SID 4 voice 2): long chord root two octaves up
      if (sectionhas(s, 7))
      {
        put(10, b, N(5, 0) + third, I_STRINGS);
        put(10, b + 8, N(5, 0) + fifth, 0);
        put(10, b + 15, KEYOFF, 0);
      }
    }
    // Lead (SID 3 voice 1) and its echo 3 rows later (SID 3 voice 2)
    if (sectionhas(s, 5))
    {
      int k;
      int up = (s == 4) ? 12 : 0;
      for (k = 0; k < (int)(sizeof melody / sizeof melody[0]); k++)
      {
        put(6, base + melody[k][0], N(melody[k][1], melody[k][2]) + up, I_LEAD);
        put(7, base + melody[k][0] + 3, N(melody[k][1], melody[k][2]) + up, I_ECHO);
      }
      put(6, base + 63, KEYOFF, 0);
    }
  }
}

// Find an identical pattern or create a new one
static int makepattern(int ch, int start, int *numpatt)
{
  unsigned char data[BAR_ROWS * 4 + 4];
  int r, p;
  unsigned char lastins = 0;

  for (r = 0; r < BAR_ROWS; r++)
  {
    unsigned char ins = tinstr[ch][start + r];
    data[r * 4] = tnote[ch][start + r];
    data[r * 4 + 1] = 0;
    // write the instrument on the first note of the pattern and on changes
    if ((ins) && (ins != lastins))
    {
      data[r * 4 + 1] = ins;
      lastins = ins;
    }
    data[r * 4 + 2] = 0;
    data[r * 4 + 3] = 0;
  }
  data[BAR_ROWS * 4] = ENDPATT;
  data[BAR_ROWS * 4 + 1] = data[BAR_ROWS * 4 + 2] = data[BAR_ROWS * 4 + 3] = 0;

  for (p = 0; p < *numpatt; p++)
    if (!memcmp(pattern[p], data, BAR_ROWS * 4 + 4)) return p;
  if (*numpatt >= MAX_PATT) return 0;
  memcpy(pattern[*numpatt], data, BAR_ROWS * 4 + 4);
  return (*numpatt)++;
}

int makedemo(const char *outname, const char *libdir)
{
  char cwd[MAX_PATHNAME];
  int c, ch, numpatt = 0;
  unsigned i;

  getcwd(cwd, MAX_PATHNAME);
  initchannels();
  clearsong(1, 1, 1, 1, 1);
  setnumsids(4);
  editorInfo.sidmodel = 1;
  editorInfo.multiplier = 1;
  editorInfo.ntsc = 0;
  editorInfo.adparam = 0x0f00;

  // Instruments from the library
  for (i = 0; i < sizeof demoinstr / sizeof demoinstr[0]; i++)
  {
    char path[MAX_PATHNAME];
    snprintf(path, MAX_PATHNAME, "%s/%s", libdir, demoinstr[i][0]);
    if (chdir(path))
    {
      fprintf(stderr, "error: instrument folder %s not found\n", path);
      return 0;
    }
    strcpy(instrfilename, demoinstr[i][1]);
    editorInfo.einum = i + 1;
    loadinstrument();
    if (!instr[i + 1].ptr[WTBL])
    {
      fprintf(stderr, "error: could not load %s/%s\n", path, demoinstr[i][1]);
      return 0;
    }
  }
  chdir(cwd);

  // Patterns & orderlists
  composedemo();
  for (ch = 0; ch < DEMO_CHN; ch++)
  {
    int pos = 0;
    for (c = 0; c < TOTALROWS / BAR_ROWS; c++)
      songorder[0][ch][pos++] = makepattern(ch, c * BAR_ROWS, &numpatt);
    songorder[0][ch][pos] = LOOPSONG;
    songorder[0][ch][pos + 1] = 4;  // loop to the first verse
    songlen[0][ch] = pos;
  }
  countpatternlengths();

  strcpy(songname, "GrooveGoat Demo");
  strcpy(authorname, "IT-Dummkopfen");
  strcpy(copyrightname, "2026 C64 GrooveGoat");
  strncpy(songfilename, outname, MAX_FILENAME - 1);
  if (!savesong())
  {
    fprintf(stderr, "error: %s\n", statusmessage);
    return 0;
  }
  printf("%s: %d patterns, %d instruments, %d SIDs\n", outname, numpatt, (int)(sizeof demoinstr / sizeof demoinstr[0]), numsids);
  return 1;
}

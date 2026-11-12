#ifndef GBEAT_H
#define GBEAT_H

// C64 GrooveGoat - beat editor: drum patterns from genre templates on the
// channel of the pattern cursor (one SID voice, one drum sound at a time)

// Drum roles. Letters in the template grids: K S C H O T M L R Y
enum
{
  BR_KICK = 0, BR_SNARE, BR_CLAP, BR_CHAT, BR_OHAT, BR_TOMH, BR_TOMM, BR_TOML, BR_RIM, BR_CRASH, BR_NUM
};

// Levels: ghost (soft), normal, accent
enum { BL_SOFT = 0, BL_NORMAL, BL_ACCENT, BL_NUM };

// Drum kits (sound of the generated instruments)
enum { KIT_CHIP = 0, KIT_808, KIT_909, KIT_ROCK, KIT_NUM };

#define BEAT_MAXVAR 6
#define BEAT_MAXSUB 4

// A beat template is a drum machine grid: one line per role, one character
// per 16th note ("." rest, "x" hit, "X" accent, "g" ghost / soft, "r" roll =
// two 32nd notes), 16 (one bar) or 32 (two bars) characters, for example
//   "K:x...x...x...x... S:....x.......x... H:..x...x...x...x."
typedef struct
{
  const char *name;
  const char *grid;
  int swing;            // 50 = straight, 58 ... 75 = shuffle (16th pairs)
} BEATVARIANT;

typedef struct
{
  const char *name;
  int kit;
  const char *fill;     // one bar, used for the "fill in the last bar" option
  BEATVARIANT var[BEAT_MAXVAR];
} BEATSUBGENRE;

typedef struct
{
  const char *name;
  BEATSUBGENRE sub[BEAT_MAXSUB];
} BEATGENRE;

extern const BEATGENRE beatcatalog[];
extern const char *kitnames[KIT_NUM];
extern const char *beatrolenames[BR_NUM];

// One event of a parsed grid: position in 32nd notes, role, level
typedef struct
{
  int pos32;
  int role;
  int level;
} BEATHIT;

// Parse a grid string. Returns the number of hits; *steps = length in 16ths.
int beat_parse(const char *grid, BEATHIT *hits, int maxhits, int *steps);

// The parts of the last composed song (set by the composer when it writes a
// song), so a whole-song beat knows the structure exactly
#define SP_MAX 64
enum { SPK_INTRO = 0, SPK_VERSE, SPK_CHORUS, SPK_BRIDGE, SPK_OUTRO };
typedef struct
{
  char title[32];
  int rows, barrows, n;
  int bars[SP_MAX], kind[SP_MAX];
  double energy[SP_MAX];
} SONGPARTS;
extern SONGPARTS songparts;

// The "Beat editor" dialog (CTRL+D)
void beatdialog(void);

// Command line / test helper: genre, subgenre, variant (indexes) into the
// pattern of channel chn. Returns rows written, 0 on error.
int beat_selftest(void);
// generated drum (role BR_*, level BL_*, kit KIT_*) in a free instrument slot, 0 = no room
int beat_newdrum(int role, int level, int kit, int tempo);
int beat_generate_cli(int chn, int genre, int sub, int var, int fill, char *err, int errsize);

#endif

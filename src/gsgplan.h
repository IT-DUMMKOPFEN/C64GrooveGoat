// C64 GrooveGoat - the song plan (composer 1.1)
//
// A song plan says what the composer should write: title, genre, key, tempo,
// mood, the theme and the parts with their length, energy and chords. It is
// made by the composer itself (the built-in plan) or pasted from any AI
// chatbot, and is kept as JSON text (.plan).
#ifndef GSGPLAN_H
#define GSGPLAN_H

#include "gsgcomp.h"

#define PLAN_MAXSECS 40
#define PLAN_MAXTHEME 48

enum { PP_INTRO = 0, PP_VERSE, PP_CHORUS, PP_BRIDGE, PP_BREAK, PP_OUTRO, PP_NUM };
extern const char *plan_partname[PP_NUM];

typedef struct
{
  int part;                    // PP_...
  int bars;
  double energy;               // 0..1
  char chords[96];             // "Am . F . C G" - one chord per bar, '.' holds
  char idea[48];               // a note (shown only)
} PLANSEC;

typedef struct SONGPLAN
{
  char idea[256];              // the request in words
  char title[64], mood[64];
  char genre[32];              // style key ("" = the one of the dialog)
  int key, mode, bpm;          // -1 / -1 / 0 = from the style
  int ntheme;                  // theme: scale degrees (1 = keynote) on the 'x' of the rhythm
  int theme[PLAN_MAXTHEME];
  char rhythm[72];             // 16 steps per bar: x note, - hold, . rest
  int nsec;
  PLANSEC sec[PLAN_MAXSECS];
} SONGPLAN;

void plan_clear(SONGPLAN *p);
// the plan of a composed piece (what the composer decided)
void plan_from_comp(const COMP *c, SONGPLAN *p);
// JSON (the .plan file and the answer of a language model)
int plan_to_json(const SONGPLAN *p, char *out, int size);
// reads a plan; repairs what it can. 1 = usable; msg tells what was repaired
int plan_parse(const char *text, SONGPLAN *p, char *msg, int msgsize);
// the request to a language model
int plan_prompt(const SONGPLAN *p, const char *genre_hint, int seconds, int sids, char *out, int size);
int plan_bars(const SONGPLAN *p);
int plan_parsechords(const char *text, int key, CHORD *out, int max);   // number of bars written ('.' = the chord before)
void plan_chordname(CHORD ch, int key, char *out, int size);
int plan_mode_index(const char *name);

char *plan_readfile(const char *path);                 // the whole file, NUL-terminated (free it)
int plan_writefile(const char *path, const SONGPLAN *p);

// composer hooks (gsgcomp.c, gsgrender.c)
void plan_hook_progs(COMP *c);
void plan_hook_structure(COMP *c);
void plan_hook_theme(COMP *c);
int plan_hook_chords(COMP *c, int si, int nbars, CHORD *out);      // 1 = the plan gave the chords

#endif

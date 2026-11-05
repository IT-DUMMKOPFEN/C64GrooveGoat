#ifndef GSG_H
#define GSG_H

// C64 GrooveGoat - C port of the SIDGen core (composer, playing technique,
// layers for 4-8 SIDs, MIDI importer). SIDGen: IT-DUMMKOPFEN,
// https://github.com/IT-DUMMKOPFEN/sidgen - the data comes from
// tools/mksidgen.py (gsgdata.c).

#include <stdint.h>

// ---------------------------------------------------------------------------
// Generated data (gsgdata.c)
// ---------------------------------------------------------------------------
typedef struct
{
  const char *key, *name, *sub;
  int weight;
  const char *desc;
  const char *json;
} SGSTYLEREF;

extern const char *sg_version;
extern const int sg_numstyles;
extern const SGSTYLEREF sg_styles[];
extern const int sg_numinstr;
extern const char *sg_instrtext[];
extern const char *sg_arptext;
extern const char *sg_arpentext[];
extern const char *sg_wordtext[];
extern const char *sg_chordtypes;
extern const char *sg_filtertext[];
extern const char *sg_removed;
extern const char *sg_map8580[];
extern const char *sg_punchtext[];
extern const char *sg_spectratext[];
extern const char *sg_timbretext[];
extern const char *sg_refpitch;
extern const char *sg_catdefault;

// ---------------------------------------------------------------------------
// JSON (style data)
// ---------------------------------------------------------------------------
enum { JV_NULL = 0, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ };
typedef struct JV
{
  int type;
  double num;
  char *str;
  int n;
  struct JV **items;
  char **keys;
} JV;

JV *jv_parse(const char *s);
void jv_free(JV *v);
JV *jv_get(const JV *o, const char *key);          // NULL if missing
JV *jv_at(const JV *a, int i);
int jv_len(const JV *a);
double jv_num(const JV *v, double def);
const char *jv_str(const JV *v, const char *def);
int jv_true(const JV *v);

// ---------------------------------------------------------------------------
// Random numbers (Mersenne Twister like Python's random module)
// ---------------------------------------------------------------------------
typedef struct
{
  uint32_t mt[624];
  int mti;
} SGRNG;

void rng_seed(SGRNG *r, uint32_t seed);
void rng_seeds(SGRNG *r, const char *fmt, ...);    // seed from a text ("%d-mel-%d")
double rng_random(SGRNG *r);                       // [0, 1)
int rng_below(SGRNG *r, int n);                     // 0 .. n-1
double rng_uniform(SGRNG *r, double a, double b);
int rng_wchoice(SGRNG *r, const double *w, int n);  // index by weight

// weighted list [[value, weight], ...] in the JSON: index of the choice
int jv_wchoice(SGRNG *r, const JV *list);
// value of [[value, weight], ...] at index i
JV *jv_wval(const JV *list, int i);
const char *jv_wstr(SGRNG *r, const JV *list);      // choose, then the string value
const char *jv_choicestr(SGRNG *r, const JV *list); // plain list of strings

// ---------------------------------------------------------------------------
// Music theory
// ---------------------------------------------------------------------------
enum { Q_MAJ = 0, Q_MIN, Q_DIM, Q_AUG, Q_SUS4, Q_SUS2, Q_MAJ7, Q_MIN7, Q_DOM7, Q_POW, Q_MIN6, Q_MAJ6, Q_NUM };
extern const char *sg_qname[Q_NUM];       // maj, min ... (SIDGen names)
extern const char *sg_qlabel[Q_NUM];      // Dur, Moll ... (SIDGen labels)
extern const char *sg_qfile[Q_NUM];       // arpeggio library suffix: maj, min, dim, aug, sus4, sus2, maj7, m7, 7, 5, m6, 6
extern const int sg_qiv[Q_NUM][4];
extern const int sg_qlen[Q_NUM];
extern const char *sg_keyname[12];

typedef struct { int root, q; } CHORD;

int sg_parsechord(const char *sym, CHORD *c);       // roman numeral, relative root
int sg_qindex(const char *name);

typedef struct
{
  int key, mode;
  int steps[8], nsteps;
  int pcs;                // bit mask of the pitch classes
} SCALE;

enum { M_MAJOR = 0, M_MINOR, M_DORIAN, M_PHRYGIAN, M_LYDIAN, M_MIXOLYDIAN, M_HARMMINOR, M_PENTMAJ, M_PENTMIN, M_BLUES, M_NUM };
extern const char *sg_modename[M_NUM];
extern const char *sg_modelabel[M_NUM];
int sg_modeindex(const char *name);
void scale_init(SCALE *s, int key, int mode);
int scale_contains(const SCALE *s, int n);

// ---------------------------------------------------------------------------
// Instruments (SIDGen data: hand-made + generated per SID model)
// ---------------------------------------------------------------------------
typedef struct
{
  char name[48];
  char cat[12];
  char fam[16];
  char group[16];           // SIDGen instrument group (lead / pad families)
  char theme[12];
  int model;              // 0 hand-made, 6581 / 8580 generated
  int ad, sr, gt, fw;
  int vib[2], hasvib, vibdelay;
  char filt[24];
  int wl[64], wr[64], nw;     // wavetable program (right: 0x100 | x never)
  int pl[32], pr[32], np;     // pulse program
  char en[48];                // English name
  double level;
  int derived;                // made while composing (echo copy, octave bass, arpeggio)
} SGINS;

typedef struct
{
  char name[32];
  int ad, sr, wavebyte, speed, octave;
  int pl[32], pr[32], np;
  char filt[24];
  char en[32];
} SGARP;

typedef struct
{
  char name[24];
  int l[64], r[64], n;        // r: 0x100 | resonance = channel mask placeholder
  double scale8580;
} SGFILTER;

void sg_init(void);
const SGINS *sg_findins(const char *name);           // REMOVED applied
const SGARP *sg_findarp(const char *name);
const SGFILTER *sg_findfilter(const char *name);
void sg_addfilter(const char *name, const int *l, const int *r, int n);
const char *sg_formodel(const char *name, int model); // 8580: replacement (genlib.for_model)
const char *sg_removed_name(const char *name);       // instruments.REMOVED
double sg_punch(int model, const char *name, int *found);
int sg_mainwave(const SGINS *i);                      // like genlib.main_wave
int sg_timbrewave(const SGINS *i);                    // like timbre.main_wave
int sg_numgen(int model);
const SGINS *sg_gen(int model, int i);
// arpeggio instrument from a template and a chord quality
int sg_handcount(void);
const SGINS *sg_hand(int k);
int sg_arpcount(void);
const SGARP *sg_arpat(int k);
int sg_arpindex(const SGARP *a);
void sg_make_title(SGRNG *rng, const struct JV *st, char *out, int size);
void sg_makearp(const SGARP *t, int q, SGINS *out);
void sg_translate(const char *name, char *out, int size);   // i18n._translate
void sg_en(const char *name, char *out, int size);          // i18n.en
void sg_echoof(const SGINS *src, SGINS *out);
void sg_octavebass(const SGINS *src, SGINS *out);

// ---------------------------------------------------------------------------
// Tracks: the events of one voice (as in composer.Track)
// ---------------------------------------------------------------------------
enum { EV_NONE = 0, EV_NOTE, EV_OFF };
enum { CD_PLAIN = 0, CD_SPD, CD_FPROG };
typedef struct
{
  unsigned char cmd, kind;     // command 0-F; data: plain byte, speed table entry, filter program
  int a, b;                    // plain: a = byte; spd: left, right; fprog: b = voice mask (-1 = this voice)
  char fname[24];
} SGCMD;

typedef struct
{
  unsigned char type;
  signed char note;
  short ins;                   // index into the composer's instruments
  short dur;                   // -1 = none
  unsigned char hascmd;
  SGCMD cmd;
} SGEV;

typedef struct
{
  int rows;
  SGEV *ev;
  unsigned char *hascmds;
  SGCMD *cmds;                 // commands on the rows after a note
} SGTRACK;

void tr_init(SGTRACK *t, int rows);
void tr_free(SGTRACK *t);
void tr_grow(SGTRACK *t, int rows);
void tr_note(SGTRACK *t, int row, int note, int ins, int dur);
void tr_notecmd(SGTRACK *t, int row, int note, int ins, int dur, const SGCMD *c);
void tr_off(SGTRACK *t, int row);
void tr_merge_low(SGTRACK *t, const SGTRACK *low, int guard, int drumlen);
void tr_finalize(SGTRACK *t, int total);
int tr_busy(const SGTRACK *t, int row, int drumlen);

#endif

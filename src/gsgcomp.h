#ifndef GSGCOMP_H
#define GSGCOMP_H

// C64 GrooveGoat - the SIDGen composer (composer.py, layers.py) in C

#include "gsg.h"

enum { SEC_INTRO = 0, SEC_A, SEC_B, SEC_C, SEC_OUTRO, SEC_A2, SEC_B2, SEC_C2, SEC_NUM };
extern const char *sg_secname[SEC_NUM];
int sec_base(int s);

// drum letters per row (bit mask)
enum { DL_K = 1, DL_S = 2, DL_C = 4, DL_O = 8, DL_H = 16, DL_R = 32, DL_T = 64, DL_P = 128, DL_X = 256 };
int dl_bit(char c);

enum { SK_KICK = 0, SK_SNARE, SK_HAT, SK_OHAT, SK_CLAP, SK_CRASH, SK_TOM, SK_RIDE, SK_PERC, SK_NUM };
extern const char *sg_kitcat[SK_NUM];

#define SG_MAXSECS 96
#define SG_MAXINS 160
#define SG_MAXTRACKS 24

typedef struct { int row, note, dur; } RND;          // (row, note, duration)
typedef struct { RND *v; int n, cap; } RNDLIST;
typedef struct { int pos, d, isnote; } RCELL;         // rhythm: (pos, len, 'n'/'r')
typedef struct { int pos, d, strong, dlt; } MNOTE;    // motif note
typedef struct { MNOTE v[96]; int n; } MOTIF;
typedef struct { int on, offs, acc; } RIFFSTEP;

typedef struct
{
  int mode2, drums, bass, lead, mode2b, mode3;   // modes: index into sg_modes2[]; drums: 0 full 1 hats 2 kick 3 none
} ARR;

enum { MD_ARP_HOLD = 0, MD_ARP_RHYTHM, MD_PAD, MD_ECHO, MD_COUNTER, MD_BROKEN, MD_HARMONY, MD_FIFTHS, MD_NUM };
extern const char *sg_modes2[MD_NUM];
int md_index(const char *s);

typedef struct COMPS
{
  // the style (merged sub-style)
  JV *st;
  int styleidx;              // index into sg_styles
  char stylekey[24];
  unsigned seed;
  SGRNG rng, rng_mel, rng_ins;
  SGRNG *cur;                // self.rng (switched to rng_mel while making the melody)
  double target;
  char title[64];
  int model, voices, sids;
  int want_key, want_mode, want_bpm;
  char want_lead[48];
  int mel_var, ins_var;
  int variants, expressive, compact, jingle;

  // choices
  int spb, bt;
  JV *L;                     // lead settings (merged)
  double L_rest, L_leap, L_repeat, L_pickup, L_ornament;
  int L_lo, L_hi, L_motif_bars, L_callresp;
  int tempo[2];
  double bar_sec, bpm;
  int key, mode;
  SCALE scale;
  char layout[48];
  int dance_kb;              // 2 SIDs: layout starts with KB
  CHORD progs[SEC_NUM][16];
  int nprog[SEC_NUM];
  CHORD tonic;
  int islong;
  int chord_bars[SEC_NUM];

  // instruments (names)
  char i_bass[48], i_kickbass[48], i_lead[48], i_lead_b[48], t_arp[48], t_stab[48], t_pad[48];
  char i_counter[48], i_broken[48], i_lead_v[48], i_harm[48], t_arp2[48];
  char kit[SK_NUM][48];
  char stab[40];
  char filter_role[8], filter_tpl[24];
  char bass_pat[SEC_NUM][16];
  int echo_delay;
  JV *drum_pat[3];           // A, B, C

  // layers (4-8 SIDs)
  char i_bass2[48], i_sub[48], i_lead2[48], i_lead3[48], i_padv[48], i_pad2v[48], i_strings[48];
  char i_counter2[48], i_harm2[48], i_broken2[48], t_arp3[48], i_kick2[48], i_clap[48];
  int layer_bass_shift;
  char track_roles[SG_MAXTRACKS][32];

  // structure
  int secs[SG_MAXSECS], secbars[SG_MAXSECS], nsecs;
  int total_bars, total_rows;
  double seconds;

  // material
  int motif_have[SEC_NUM][9];
  MOTIF motifs[SEC_NUM][9][3];
  int last_lead_note;
  RIFFSTEP riffs[SEC_NUM][16];
  ARR arr[SEC_NUM];

  // rendering
  SGTRACK tracks[SG_MAXTRACKS];
  int ntracks;
  CHORD *chord_rows;
  int nchordrows;
  RNDLIST lead_rows, bass_rows, echo_rows;
  int *drum_rows;            // letters per row (whole piece)
  int nshared;
  int pad_prev[4], npad_prev, str_prev;

  // instruments used (in order)
  SGINS ins[SG_MAXINS];
  int numins;
  int arp_tpl[SG_MAXINS], arp_q[SG_MAXINS];   // arpeggio source (template index or -1)
  int key_change_row, key_change_by, nstops, stops[4];
  int expr_stats[6];
  int expressed, placed, merged, track_order[SG_MAXTRACKS];
  struct COMPS *fixed;          // subtunes: the choices of the title music
  int hasvar;                   // i_lead_v, i_harm, t_arp2 were chosen             // glide scoop fall vib bslide sweep
  char err[160];
} COMP;

void comp_setvariations(COMP *c, int mel_var, int ins_var);
COMP *comp_new(int styleidx, unsigned seed, double seconds, int sids, int voices, int model);
void comp_free(COMP *c);
int comp_compose(COMP *c);          // compose the tracks (1 = ok)
int comp_use(COMP *c, const char *name);
int comp_use_arp(COMP *c, const char *tpl, int q);
int comp_bass_root(int pc);
void comp_bass_bar(COMP *c, const char *pat, CHORD chord, CHORD nxt, int bar_idx, const RIFFSTEP *riff, RND *out, int *n);
void comp_acid_riff(COMP *c, RIFFSTEP *riff);
int comp_chord_pcs(CHORD c);
int comp_chord_scale(COMP *c, CHORD ch);
void comp_render_mode2(COMP *c, SGTRACK *t, int row0, int nbars, CHORD *chords, int mode, RND *lead, int nlead, int last,
                       const char *stab, int tpl2);
void rl_add(RNDLIST *l, int row, int note, int dur);

// layers (gsglayers.c)
void comp_pick_layer_instruments(COMP *c);
void comp_layer_roles(COMP *c);
void comp_render_layers(COMP *c, int sec, int nbars, CHORD *chords, int row0, int first, int last, RND *lead, int nlead,
                        int *bassev_note, int *bassev_dur, int *drums);

const JV *comp_style(COMP *c, const char *key);

// SIDGen style helpers
JV *style_parse(int styleidx);
int style_count(void);
int style_numgenres(void);
int style_genre_first(int g);       // first variant index of a genre
int style_genre_count(int g);

#endif

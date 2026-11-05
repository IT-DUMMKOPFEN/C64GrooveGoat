// C64 GrooveGoat - MIDI import
#ifndef GSGMIDI_H
#define GSGMIDI_H

#include "gsgcomp.h"

enum { ACC_AUTO = 0, ACC_ARP, ACC_STABS, ACC_PADS, ACC_BROKEN, ACC_COUNTER };
enum { HARM_NORMAL = 0, HARM_CAREFUL, HARM_LOOSE };

#define CMD_SETTEMPO_ 0x0f
#define CMD_FUNKTEMPO_ 0x0e

typedef struct
{
  int variant;                 // 0 = first conversion, 1.. = "convert again"
  int voices, sids, model;
  int accomp, harmony;
  char melody[16], bass[16];   // "" = auto, "none" (bass), "track:channel"
  int drums_off;
  double max_seconds;          // 0 = whole file
  char title[64];
  char basename[64];           // file name without the extension (title fallback)
  // hints of the enhanced calculation (the composed 8-SID piece)
  int hints;
  int hint_q, hint_tempo[2], hint_dance;          // hint_dance -1 = keep
  char hint_bass[48], hint_kickbass[48], hint_arp[48], hint_pad[48];
  char hint_kit[8][48];                           // kick snare clap hat ohat crash ride perc ("" = keep)
  int nhint_lead, hint_lead_prog[128];
  char hint_lead_name[128][48];
  int nhint_gm, hint_gm_note[128];
  char hint_gm_ins[128][48];
  int nhint_roles, hint_role[64];                 // 0 harm, 1 bass, 2 mel, 3 mel2
  char hint_role_name[64][48];
} MIDIOPTS;

typedef struct
{
  int nparts;
  int track[64], channel[64], program[64], role[64], notes[64];
  char name[64][28];
  int drum_notes, q, bar_rows, nbars, timesig[2], dance;
  double midi_bpm, midi_seconds;
  int harm_beats, harm_changed, harm_simplified, harm_rests, level_commands;
  char title[64];
} MIDIINFO;

void midiopts_default(MIDIOPTS *o);
// a composer-like result (kind 1) for sg_buildsongs; NULL and err on failure
COMP *sg_import_midi(const unsigned char *data, int len, const MIDIOPTS *opts, MIDIINFO *info, char *err, int errsize);

// enhanced SID calculation: big composed for 8 SIDs -> piece for 1 or 2 SIDs
COMP *sg_enhance(COMP *big, int sids, int voices, char *err, int errsize);

#endif

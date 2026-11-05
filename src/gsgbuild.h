#ifndef GSGBUILD_H
#define GSGBUILD_H

#include "gsgcomp.h"

// write the composed piece into the song in memory (writeto = 0: only check)
int sg_buildsong(COMP *c, int writeto, char *err, int errsize);
// several subtunes (title, in-game, game over) sharing instruments and patterns
int sg_buildsongs(COMP **subs, int nsubs, int writeto, char *err, int errsize);

// command line: --compose=STYLE[/SUB],SEED,SECONDS,SIDS[,MODEL,SUBTUNES,VOICES,ENHANCED]
// enhanced: 1-2 SIDs composed for 8 SIDs and calculated down (no subtunes)
COMP *song_make(int styleidx, unsigned seed, double seconds, int sids, int voices, int model, char *err, int errsize, int subtunes,
                  int enhanced);
void composedialog(void);
void midiimportdialog(void);       // CTRL+F: MIDI file -> new song
int compose_cli(const char *spec, char *err, int errsize);
int midi_cli(const char *spec, char *err, int errsize);

// one composer instrument into the song (free slot, tables appended); 0 = error
int sg_addinstrument(const SGINS *i, int chmask, int gt, char *err, int errsize);

#endif

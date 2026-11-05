#ifndef GSGEXPR_H
#define GSGEXPR_H

#include "gsgcomp.h"

int sg_freq(int n);
int sg_quant_speed(double v);
int sg_slide_ok(const SGINS *i);
void sg_sweep_name(const char *kind, double frames, int model, char *out, int size);
void sg_express(COMP *c);          // playing technique (before the song build)
void sg_song_ideas(COMP *c);       // key change, stop (after rendering)

#endif

// C64 GrooveGoat - SIDGen layers for 4, 6 and 8 SIDs (port of layers.py)

#include <stdio.h>
#include <string.h>
#include "gsgcomp.h"

void comp_pick_layer_instruments(COMP *c) { (void)c; }
void comp_layer_roles(COMP *c) { (void)c; }
void comp_render_layers(COMP *c, int sec, int nbars, CHORD *chords, int row0, int first, int last, RND *lead, int nlead,
                        int *bn, int *bd, int *drums)
{
  (void)c; (void)sec; (void)nbars; (void)chords; (void)row0; (void)first; (void)last; (void)lead; (void)nlead;
  (void)bn; (void)bd; (void)drums;
}

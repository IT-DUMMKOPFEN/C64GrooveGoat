#ifndef GSGBUILD_H
#define GSGBUILD_H

#include "gsgcomp.h"

// write the composed piece into the song in memory (writeto = 0: only check)
int sg_buildsong(COMP *c, int writeto, char *err, int errsize);

// command line: --compose=STYLE[/SUB],SEED,SECONDS,SIDS[,MODEL]
COMP *sidgen_make(int styleidx, unsigned seed, double seconds, int sids, int voices, int model, char *err, int errsize, int dumpfirst);
int sidgen_compose_cli(const char *spec, char *err, int errsize);

#endif

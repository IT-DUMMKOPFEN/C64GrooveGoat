#ifndef GSGTIMBRE_H
#define GSGTIMBRE_H

#include "gsgcomp.h"

int tb_isdrum(const char *cat);
double tb_similarity(const SGINS *a, const SGINS *b, int pa, int pb);
double tb_similarity_names(const char *a, const char *b);
int tb_alternatives(const char *name, const char **exclude, int nex, const char **out, int max);
const char *tb_distinct(const char *name, const char **used, int nused, int k);
void tb_place_tracks(COMP *c, SGTRACK *tracks, int total, int fixed_empty, int *order, double *before, double *after);

#endif

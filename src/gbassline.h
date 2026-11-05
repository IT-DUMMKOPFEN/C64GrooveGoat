#ifndef GBASSLINE_H
#define GBASSLINE_H

// Bass & Arps (CTRL+P): bassline and chord parts from the chords of the song
void basslinedialog(void);
// command line: --bassline=PART,STYLE,FIGURE,ARPMODE,SEED (cursor voice: GG_BASSCHN)
int bassline_cli(const char *spec, char *err, int errsize);

#endif

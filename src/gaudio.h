#ifndef GAUDIO_H
#define GAUDIO_H

#include <SDL.h>

#define MONO 0
#define STEREO 1
#define SIXTEENBIT 2

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength);
void snd_uninit(void);
void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples));
void snd_lock(void);
void snd_unlock(void);

extern void (*snd_player)(void);
extern int snd_sndinitted;
extern int snd_bpmtempo;
extern int snd_bpmcount;
extern unsigned snd_mixmode;
extern unsigned snd_mixrate;

#endif

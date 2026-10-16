//
// C64 GrooveGoat - audio output (SDL2), based on the BME sound module
//
// The audio callback calls the "player" routine at the song's frame rate
// (snd_bpmtempo, in "BPM" units like the original) and a custom mixer that
// renders the SID output in between.
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL.h>
#include "gaudio.h"

void (*snd_player)(void) = NULL;
int snd_sndinitted = 0;
int snd_bpmcount = 0;
int snd_bpmtempo = 125;
unsigned snd_mixmode = 0;
unsigned snd_mixrate = 44100;

static void (*snd_custommixer)(Sint32 *dest, unsigned samples) = NULL;
static Sint32 *snd_clipbuffer = NULL;
static unsigned snd_clipsize = 0;
static SDL_AudioDeviceID snd_device = 0;

static void snd_mixer(void *userdata, Uint8 *stream, int len);

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength)
{
  SDL_AudioSpec desired, obtained;
  int bits = 0;

  snd_uninit();

  memset(&desired, 0, sizeof desired);
  desired.freq = mixrate;
  desired.format = AUDIO_S16SYS;
  desired.channels = (mixmode & STEREO) ? 2 : 1;
  desired.samples = bufferlength * mixrate / 1000;
  for (;;)
  {
    desired.samples >>= 1;
    if (!desired.samples) break;
    bits++;
  }
  desired.samples = 1 << bits;
  if (desired.samples < 256) desired.samples = 256;
  desired.callback = snd_mixer;
  desired.userdata = NULL;

  snd_bpmcount = 0;

  snd_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!snd_device)
  {
    fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return 0;
  }

  snd_mixmode = SIXTEENBIT;
  if (obtained.channels == 2) snd_mixmode |= STEREO;
  snd_mixrate = obtained.freq;

  snd_clipsize = obtained.samples * 2 * 4;
  snd_clipbuffer = (Sint32 *)malloc(snd_clipsize * sizeof(Sint32));
  if (!snd_clipbuffer)
  {
    snd_uninit();
    return 0;
  }
  snd_sndinitted = 1;
  SDL_PauseAudioDevice(snd_device, 0);
  return 1;
}

void snd_uninit(void)
{
  if (snd_device)
  {
    SDL_CloseAudioDevice(snd_device);
    snd_device = 0;
  }
  snd_sndinitted = 0;
  free(snd_clipbuffer);
  snd_clipbuffer = NULL;
}

void snd_lock(void)
{
  if (snd_device) SDL_LockAudioDevice(snd_device);
}

void snd_unlock(void)
{
  if (snd_device) SDL_UnlockAudioDevice(snd_device);
}

void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples))
{
  snd_custommixer = custommixer;
}

static void snd_mixer(void *userdata, Uint8 *stream, int len)
{
  int channels = (snd_mixmode & STEREO) ? 2 : 1;
  unsigned mixsamples = len / (2 * channels);
  unsigned clipsamples = mixsamples * channels;
  Sint32 *clipptr;
  Sint16 *dest = (Sint16 *)stream;
  unsigned c;

  if ((!snd_clipbuffer) || (clipsamples > snd_clipsize))
  {
    memset(stream, 0, len);
    return;
  }
  memset(snd_clipbuffer, 0, clipsamples * sizeof(Sint32));
  clipptr = snd_clipbuffer;

  while (mixsamples)
  {
    unsigned musicsamples;
    if ((!snd_bpmcount) && (snd_player))
    {
      snd_player();
      snd_bpmcount = ((snd_mixrate * 5) >> 1) / snd_bpmtempo;
    }
    musicsamples = mixsamples;
    if ((snd_player) && (musicsamples > (unsigned)snd_bpmcount)) musicsamples = snd_bpmcount;
    if (snd_player) snd_bpmcount -= musicsamples;
    if (snd_custommixer) snd_custommixer(clipptr, musicsamples);
    clipptr += musicsamples * channels;
    mixsamples -= musicsamples;
  }

  for (c = 0; c < clipsamples; c++)
  {
    int sample = snd_clipbuffer[c];
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    dest[c] = sample;
  }
}

//
// C64 GrooveGoat - audio output (SDL2)
//
// The SID emulation is rendered by a separate thread into a ring buffer some
// milliseconds ahead of playback. The SDL audio callback (which runs in the
// realtime thread of the sound system, e.g. PipeWire/PulseAudio on Linux)
// only copies finished samples, so it never takes long, even when the sound
// server uses very small periods (for example while a browser plays video).
// Heavy SID emulation inside the callback caused short dropouts ("clicks").
//
// The render thread calls the "player" routine at the song's frame rate
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
unsigned snd_mixrate = 48000;

static void (*snd_custommixer)(Sint32 *dest, unsigned samples) = NULL;
static SDL_AudioDeviceID snd_device = 0;

// Render thread & ring buffer (interleaved stereo or mono 16-bit samples)
#define RENDERCHUNK 256
static SDL_Thread *renderthread = NULL;
static SDL_mutex *rendermutex = NULL;   // protects the player & SID emulation
static SDL_cond *rendercond = NULL;
static SDL_mutex *condmutex = NULL;
static volatile int renderquit = 0;
static Sint16 *ring = NULL;
static unsigned ringframes = 0;          // capacity in frames
static volatile unsigned ringread = 0;   // frame counters (wrap around)
static volatile unsigned ringwrite = 0;
static unsigned leadframes = 0;          // how far ahead the thread renders
static int channels = 2;
static Sint32 *chunkbuf = NULL;
static Sint16 lastsample[2] = {0, 0};
static volatile unsigned underruns = 0;

static void snd_mixer(void *userdata, Uint8 *stream, int len);
static int snd_renderthread(void *data);

static void renderchunk(Sint16 *dest, unsigned frames)
{
  unsigned mixsamples = frames;
  Sint32 *clipptr = chunkbuf;
  unsigned c;

  memset(chunkbuf, 0, frames * channels * sizeof(Sint32));
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
  for (c = 0; c < frames * channels; c++)
  {
    int sample = chunkbuf[c];
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    dest[c] = sample;
  }
}

static int snd_renderthread(void *data)
{
  Sint16 tmp[RENDERCHUNK * 2];
  (void)data;
  SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

  while (!renderquit)
  {
    unsigned filled = ringwrite - ringread;
    if (filled + RENDERCHUNK <= leadframes)
    {
      unsigned c, pos;
      SDL_LockMutex(rendermutex);
      renderchunk(tmp, RENDERCHUNK);
      SDL_UnlockMutex(rendermutex);
      pos = ringwrite % ringframes;
      for (c = 0; c < RENDERCHUNK; c++)
      {
        memcpy(&ring[((pos + c) % ringframes) * channels], &tmp[c * channels], channels * sizeof(Sint16));
      }
      SDL_MemoryBarrierRelease();
      ringwrite += RENDERCHUNK;
    }
    else
    {
      // Wait until the audio callback has consumed some samples
      SDL_LockMutex(condmutex);
      if ((!renderquit) && (ringwrite - ringread + RENDERCHUNK > leadframes))
        SDL_CondWaitTimeout(rendercond, condmutex, 5);
      SDL_UnlockMutex(condmutex);
    }
  }
  return 0;
}

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength)
{
  SDL_AudioSpec desired, obtained;
  unsigned c;

  snd_uninit();

#ifdef SDL_HINT_AUDIO_DEVICE_APP_NAME
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, "C64 GrooveGoat");
#endif
#ifdef SDL_HINT_AUDIO_DEVICE_STREAM_NAME
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_NAME, "SID playback");
#endif
#ifdef SDL_HINT_APP_NAME
  SDL_SetHint(SDL_HINT_APP_NAME, "C64 GrooveGoat");
#endif

  memset(&desired, 0, sizeof desired);
  desired.freq = mixrate;
  desired.format = AUDIO_S16SYS;
  desired.channels = (mixmode & STEREO) ? 2 : 1;
  // Small device periods (about 21 ms at 48 kHz): the callback only copies
  desired.samples = 1024;
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
  channels = obtained.channels;

  // Render ahead: the configured buffer length, but at least two device
  // periods (bufferlength is in milliseconds)
  if (bufferlength < 20) bufferlength = 20;
  if (bufferlength > 1000) bufferlength = 1000;
  leadframes = bufferlength * snd_mixrate / 1000;
  if (leadframes < obtained.samples * 2u + RENDERCHUNK) leadframes = obtained.samples * 2 + RENDERCHUNK;
  leadframes = (leadframes + RENDERCHUNK - 1) / RENDERCHUNK * RENDERCHUNK;
  ringframes = leadframes + RENDERCHUNK * 4;

  ring = (Sint16 *)calloc(ringframes * channels, sizeof(Sint16));
  chunkbuf = (Sint32 *)calloc(RENDERCHUNK * channels, sizeof(Sint32));
  rendermutex = SDL_CreateMutex();
  condmutex = SDL_CreateMutex();
  rendercond = SDL_CreateCond();
  if ((!ring) || (!chunkbuf) || (!rendermutex) || (!condmutex) || (!rendercond))
  {
    snd_uninit();
    return 0;
  }
  // Start with one device period of silence so that the first callbacks
  // never run dry
  ringread = 0;
  ringwrite = obtained.samples;
  for (c = 0; c < 2; c++) lastsample[c] = 0;
  renderquit = 0;
  renderthread = SDL_CreateThread(snd_renderthread, "SID render", NULL);
  if (!renderthread)
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
  if (renderthread)
  {
    renderquit = 1;
    if (rendercond) SDL_CondSignal(rendercond);
    SDL_WaitThread(renderthread, NULL);
    renderthread = NULL;
  }
  if (rendercond) SDL_DestroyCond(rendercond);
  if (condmutex) SDL_DestroyMutex(condmutex);
  if (rendermutex) SDL_DestroyMutex(rendermutex);
  rendercond = NULL;
  condmutex = NULL;
  rendermutex = NULL;
  snd_sndinitted = 0;
  free(ring);
  free(chunkbuf);
  ring = NULL;
  chunkbuf = NULL;
}

// Locks the player & SID emulation against the render thread
void snd_lock(void)
{
  if (rendermutex) SDL_LockMutex(rendermutex);
}

void snd_unlock(void)
{
  if (rendermutex) SDL_UnlockMutex(rendermutex);
}

void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples))
{
  snd_custommixer = custommixer;
}

unsigned snd_getunderruns(void)
{
  return underruns;
}

// SDL audio callback: copy rendered samples from the ring buffer
static void snd_mixer(void *userdata, Uint8 *stream, int len)
{
  Sint16 *dest = (Sint16 *)stream;
  unsigned frames = len / (2 * channels);
  unsigned avail, c, n;
  (void)userdata;

  if (!ring)
  {
    memset(stream, 0, len);
    return;
  }
  avail = ringwrite - ringread;
  SDL_MemoryBarrierAcquire();
  n = frames < avail ? frames : avail;
  for (c = 0; c < n; c++)
  {
    unsigned pos = ((ringread + c) % ringframes) * channels;
    dest[c * channels] = ring[pos];
    if (channels == 2) dest[c * channels + 1] = ring[pos + 1];
  }
  if (n)
  {
    lastsample[0] = dest[(n - 1) * channels];
    if (channels == 2) lastsample[1] = dest[(n - 1) * channels + 1];
  }
  // Underrun (should not happen): hold the last sample value, which is
  // inaudible, instead of jumping to zero, which would click
  for (c = n; c < frames; c++)
  {
    dest[c * channels] = lastsample[0];
    if (channels == 2) dest[c * channels + 1] = lastsample[1];
  }
  if (n < frames) underruns++;
  ringread += n;

  // Wake up the render thread
  if (rendercond) SDL_CondSignal(rendercond);
}

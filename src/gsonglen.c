//
// C64 GrooveGoat - song length calculation
//
// Plays a subtune silently with the editor playroutine until every channel
// has reached its loop point (or the song stops) and returns the length in
// frames. Used for the PRG player screen.
//

#include "goattrk2.h"

#define MAXSONGFRAMES (50 * 60 * 30)

int calcsonglength(int subtune)
{
  return calcsonglengthex(subtune, NULL);
}

// As calcsonglength(); *stopped is set to 1 if the song ends with a stop
// command instead of looping
int calcsonglengthex(int subtune, int *stopped)
{
  CHN chnbackup[MAX_CHN];
  SIDFILT filtbackup[MAX_SIDS];
  unsigned char regbackup[MAX_SIDS][NUMSIDREGS];
  int sbsonginit, sblastsonginit, sbpsnum, sbfollow, sbframes;
  int tmin = timemin, tsec = timesec, tframe = timeframe;
  int frames = 0;
  int looped[MAX_CHN];
  int c;
  int maxframes;
  int mult = editorInfo.multiplier ? editorInfo.multiplier : 1;

  if (stopped) *stopped = 0;
  if (!songvalid(subtune)) return 0;

  snd_lock();
  memcpy(chnbackup, chn, sizeof chn);
  memcpy(filtbackup, sidfilt, sizeof sidfilt);
  memcpy(regbackup, sidreg, sizeof regbackup);
  sbsonginit = songinit;
  sblastsonginit = lastsonginit;
  sbpsnum = psnum;
  sbfollow = followplay;
  sbframes = playframes;

  for (c = 0; c < MAX_CHN; c++)
  {
    chn[c].mute = 0;
    looped[c] = -1;
  }
  psnum = subtune;
  songinit = PLAY_BEGINNING;
  playroutine();
  maxframes = MAXSONGFRAMES * mult;
  while (frames < maxframes)
  {
    int done = 1;
    playroutine();
    frames++;
    if (songinit == PLAY_STOPPED)
    {
      if (stopped) *stopped = 1;
      break;
    }
    for (c = 0; c < numchannels; c++)
    {
      if ((looped[c] < 0) && (chn[c].looped)) looped[c] = frames;
      if (looped[c] < 0) done = 0;
    }
    if (done) break;
  }

  memcpy(chn, chnbackup, sizeof chn);
  memcpy(sidfilt, filtbackup, sizeof sidfilt);
  memcpy(sidreg, regbackup, sizeof regbackup);
  songinit = sbsonginit;
  lastsonginit = sblastsonginit;
  psnum = sbpsnum;
  followplay = sbfollow;
  playframes = sbframes;
  timemin = tmin;
  timesec = tsec;
  timeframe = tframe;
  snd_unlock();
  return frames;
}

// The frame on which every orderlist entry of a subtune starts first
// (-1 = never played), per channel - for the time column of the orderlist
// editor. Returns the length in frames.
int calcentryframes(int subtune, int frames[MAX_CHN][MAX_SONGLEN + 2])
{
  CHN chnbackup[MAX_CHN];
  SIDFILT filtbackup[MAX_SIDS];
  unsigned char regbackup[MAX_SIDS][NUMSIDREGS];
  int sbsonginit, sblastsonginit, sbpsnum, sbfollow, sbframes;
  int tmin = timemin, tsec = timesec, tframe = timeframe;
  int n = 0, c, d, last[MAX_CHN], looped[MAX_CHN];
  int mult = editorInfo.multiplier ? editorInfo.multiplier : 1;

  for (c = 0; c < MAX_CHN; c++)
    for (d = 0; d < MAX_SONGLEN + 2; d++) frames[c][d] = -1;
  if (!songvalid(subtune)) return 0;
  snd_lock();
  memcpy(chnbackup, chn, sizeof chn);
  memcpy(filtbackup, sidfilt, sizeof sidfilt);
  memcpy(regbackup, sidreg, sizeof regbackup);
  sbsonginit = songinit;
  sblastsonginit = lastsonginit;
  sbpsnum = psnum;
  sbfollow = followplay;
  sbframes = playframes;
  for (c = 0; c < MAX_CHN; c++)
  {
    chn[c].mute = 0;
    last[c] = -1;
    looped[c] = 0;
  }
  psnum = subtune;
  songinit = PLAY_BEGINNING;
  playroutine();
  while (n < MAXSONGFRAMES * mult)
  {
    int done = 1;
    playroutine();
    if (songinit == PLAY_STOPPED) break;
    for (c = 0; c < numchannels; c++)
    {
      int sp = chn[c].songptr;
      if ((sp != last[c]) && (sp > 0) && (sp - 1 < MAX_SONGLEN + 2) && (frames[c][sp - 1] < 0)) frames[c][sp - 1] = n;
      last[c] = sp;
      if (chn[c].looped) looped[c] = 1;
      if (!looped[c]) done = 0;
    }
    n++;
    if (done) break;
  }
  memcpy(chn, chnbackup, sizeof chn);
  memcpy(sidfilt, filtbackup, sizeof sidfilt);
  memcpy(sidreg, regbackup, sizeof regbackup);
  songinit = sbsonginit;
  lastsonginit = sblastsonginit;
  psnum = sbpsnum;
  followplay = sbfollow;
  playframes = sbframes;
  timemin = tmin;
  timesec = tsec;
  timeframe = tframe;
  snd_unlock();
  return n;
}

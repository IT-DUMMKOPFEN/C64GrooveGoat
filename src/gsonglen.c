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
    if (songinit == PLAY_STOPPED) break;
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

//
// GOATTRACKER v2 playroutine (editor playback)
//
// C64 GrooveGoat: extended to up to 8 SIDs / 24 channels. Every SID has its
// own filter and master volume state; tempo commands affect all channels.
//

#define GPLAY_C

#include "goattrk2.h"

unsigned char freqtbllo[] = {
  0x17,0x27,0x39,0x4b,0x5f,0x74,0x8a,0xa1,0xba,0xd4,0xf0,0x0e,
  0x2d,0x4e,0x71,0x96,0xbe,0xe8,0x14,0x43,0x74,0xa9,0xe1,0x1c,
  0x5a,0x9c,0xe2,0x2d,0x7c,0xcf,0x28,0x85,0xe8,0x52,0xc1,0x37,
  0xb4,0x39,0xc5,0x5a,0xf7,0x9e,0x4f,0x0a,0xd1,0xa3,0x82,0x6e,
  0x68,0x71,0x8a,0xb3,0xee,0x3c,0x9e,0x15,0xa2,0x46,0x04,0xdc,
  0xd0,0xe2,0x14,0x67,0xdd,0x79,0x3c,0x29,0x44,0x8d,0x08,0xb8,
  0xa1,0xc5,0x28,0xcd,0xba,0xf1,0x78,0x53,0x87,0x1a,0x10,0x71,
  0x42,0x89,0x4f,0x9b,0x74,0xe2,0xf0,0xa6,0x0e,0x33,0x20,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

unsigned char freqtblhi[] = {
  0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,
  0x04,0x04,0x04,0x05,0x05,0x05,0x06,0x06,0x06,0x07,0x07,0x08,
  0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,0x0d,0x0d,0x0e,0x0f,0x10,
  0x11,0x12,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1d,0x1f,0x20,
  0x22,0x24,0x27,0x29,0x2b,0x2e,0x31,0x34,0x37,0x3a,0x3e,0x41,
  0x45,0x49,0x4e,0x52,0x57,0x5c,0x62,0x68,0x6e,0x75,0x7c,0x83,
  0x8b,0x93,0x9c,0xa5,0xaf,0xb9,0xc4,0xd0,0xdd,0xea,0xf8,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

CHN chn[MAX_CHN];
SIDFILT sidfilt[MAX_SIDS];
EDITOR_UNDO_INFO editorUndoInfo;

unsigned char funktable[2];
int psnum = 0;
int songinit = PLAY_STOPPED;
int lastsonginit = 0;
int startpattpos = 0;
int playframes = 0;

// Register of SID s, offset r (voice registers are r + 7 * voice)
#define SREG(c, r) sidreg[(c) / 3][(r) + 7 * ((c) % 3)]

void sequencer(int c, CHN *cptr);

static void setalltempo(unsigned char t)
{
  int c;
  for (c = 0; c < numchannels; c++) chn[c].tempo = t;
}

static int defaulttempo(void)
{
  if (editorInfo.multiplier)
    return 6 * editorInfo.multiplier - 1;
  else
    return 6 - 1;
}

void initchannels(void)
{
  int c;
  CHN *cptr = &chn[0];

  memset(chn, 0, sizeof chn);

  for (c = 0; c < MAX_CHN; c++)
  {
    chn[c].trans = 0;
    chn[c].instr = 1;
    cptr->tempo = defaulttempo();
    cptr++;
  }
  for (c = 0; c < MAX_SIDS; c++)
  {
    memset(&sidfilt[c], 0, sizeof(SIDFILT));
    sidfilt[c].masterfader = 0x0f;
  }

  if (editorInfo.multiplier)
  {
    funktable[0] = 9 * editorInfo.multiplier - 1;
    funktable[1] = 6 * editorInfo.multiplier - 1;
  }
  else
  {
    funktable[0] = 9 - 1;
    funktable[1] = 6 - 1;
  }
}

void initsong(int num, int mode)
{
  sound_suspend();
  songinit = PLAY_STOPPED;
  psnum = num;
  songinit = mode;
  startpattpos = 0;
  sound_flush();
}

void initsongpos(int num, int mode, int pattpos)
{
  sound_suspend();
  songinit = PLAY_STOPPED;
  psnum = num;
  songinit = mode;
  startpattpos = pattpos;
  sound_flush();
}

void stopsong(void)
{
  if (songinit != PLAY_STOPPED)
  {
    sound_suspend();
    songinit = PLAY_STOP;
    sound_flush();
  }
}

void rewindsong(void)
{
  if (lastsonginit == PLAY_BEGINNING) lastsonginit = PLAY_POS;
  initsong(psnum, lastsonginit);
}

void playtestnote(int note, int ins, int chnnum)
{
  if (note == KEYON) return;
  if ((note == REST) || (note == KEYOFF))
  {
    releasenote(chnnum);
    return;
  }

  if (!(instr[ins].gatetimer & 0x40))
  {
    chn[chnnum].gate = 0xfe; // Keyoff
    if (!(instr[ins].gatetimer & 0x80))
    {
      SREG(chnnum, 0x5) = editorInfo.adparam>>8; // Hardrestart
      SREG(chnnum, 0x6) = editorInfo.adparam&0xff;
    }
  }

  chn[chnnum].instr = ins;
  chn[chnnum].newnote = note;
  if (songinit == PLAY_STOPPED)
  {
    chn[chnnum].tick = (instr[ins].gatetimer & 0x3f)+1;
    chn[chnnum].gatetimer = instr[ins].gatetimer & 0x3f;
  }
}

// ---------------------------------------------------------------------------
// Instrument preview (library, file selector, random instrument generator)
//
// Every preview starts from a clean SID: the filter, the master volume and
// all voice registers of the SID are reset, so that the state left by the
// previous instrument (for example a filter routing all voices through a
// low cutoff, a pulse width of 0 or a muted voice) can not make the next
// instrument inaudible. The state of the editor is restored afterwards.
// ---------------------------------------------------------------------------

static unsigned char previewmute[MAX_CHN];
static SIDFILT previewfilt[MAX_SIDS];
static int previewsaved = 0;

void preview_begin(void)
{
  int c;
  for (c = 0; c < MAX_CHN; c++) previewmute[c] = chn[c].mute;
  memcpy(previewfilt, sidfilt, sizeof previewfilt);
  previewsaved = 1;
}

void preview_end(void)
{
  int c;
  if (!previewsaved) return;
  snd_lock();
  for (c = 0; c < MAX_CHN; c++)
  {
    chn[c].mute = previewmute[c];
    if (songinit == PLAY_STOPPED) chn[c].gate = 0xfe;
  }
  if (songinit == PLAY_STOPPED) memcpy(sidfilt, previewfilt, sizeof previewfilt);
  snd_unlock();
  previewsaved = 0;
}

void preview_note(int note, int ins, int chnnum)
{
  int s, c;
  if ((chnnum < 0) || (chnnum >= numchannels)) chnnum = 0;
  snd_lock();
  // a song that was just stopped: finish stopping it now
  if (songinit == PLAY_STOP) playroutine();
  if (songinit != PLAY_STOPPED)
  {
    snd_unlock();
    return;
  }
  s = chnnum / 3;

  // voices of this SID: silent, default pulse width
  for (c = s * 3; c < s * 3 + 3; c++)
  {
    CHN *cptr = &chn[c];
    cptr->gate = 0xfe;
    cptr->wave = 0;
    cptr->freq = 0;
    cptr->pulse = 0x800;
    cptr->ptr[WTBL] = 0;
    cptr->ptr[PTBL] = 0;
    cptr->pulsetime = 0;
    cptr->wavetime = 0;
    cptr->vibtime = 0;
    cptr->vibdelay = 0;
    cptr->command = 0;
    cptr->cmddata = 0;
    cptr->newcommand = 0;
    cptr->newcmddata = 0;
    cptr->newnote = 0;
    cptr->trans = 0;
  }
  chn[chnnum].mute = 0;
  // jam mode tempo; must be longer than the instrument's gate timer
  chn[chnnum].tempo = defaulttempo();
  if (chn[chnnum].tempo <= (instr[ins].gatetimer & 0x3f))
    chn[chnnum].tempo = (instr[ins].gatetimer & 0x3f) + 1;
  // filter off, full volume, all registers cleared
  memset(&sidfilt[s], 0, sizeof(SIDFILT));
  sidfilt[s].masterfader = 0x0f;
  memset(sidreg[s], 0, NUMSIDREGS);
  sidreg[s][0x18] = 0x0f;
  // legato instruments (1st frame wave 00) would keep the gate closed
  if (!instr[ins].firstwave) instr[ins].firstwave = 0xff;
  playtestnote(note, ins, chnnum);
  snd_unlock();
}

void releasenote(int chnnum)
{
  chn[chnnum].gate = 0xfe;
}

void mutechannel(int chnnum)
{
  if ((chnnum < 0) || (chnnum >= MAX_CHN)) return;
  chn[chnnum].mute ^= 1;
}

// Solo: mute all others; if already soloed, unmute everything
void solochannel(int chnnum)
{
  int c;
  int alreadysolo = 1;
  if ((chnnum < 0) || (chnnum >= numchannels)) return;
  for (c = 0; c < numchannels; c++)
  {
    if ((c == chnnum) && (chn[c].mute)) alreadysolo = 0;
    if ((c != chnnum) && (!chn[c].mute)) alreadysolo = 0;
  }
  for (c = 0; c < numchannels; c++)
  {
    if (alreadysolo) chn[c].mute = 0;
    else chn[c].mute = (c != chnnum);
  }
}

int isplaying(void)
{
  return (songinit != PLAY_STOPPED);
}

static void stopiftablejump(int table, unsigned char ptr)
{
  if (ptr)
  {
    // Stop the song in case of jumping into a jump
    if (ltable[table][ptr-1] == 0xff)
      stopsong();
  }
}

static void setfilterptr(SIDFILT *f, unsigned char ptr)
{
  f->filterptr = ptr;
  f->filtertime = 0;
  stopiftablejump(FTBL, ptr);
}

static void filterexec(int s)
{
  SIDFILT *f = &sidfilt[s];

  if (f->filterptr)
  {
    // Filter jump
    if (ltable[FTBL][f->filterptr-1] == 0xff)
    {
      f->filterptr = rtable[FTBL][f->filterptr-1];
      if (!f->filterptr) goto FILTERSTOP;
    }

    if (!f->filtertime)
    {
      // Filter set
      if (ltable[FTBL][f->filterptr-1] >= 0x80)
      {
        f->filtertype = ltable[FTBL][f->filterptr-1] & 0x70;
        f->filterctrl = rtable[FTBL][f->filterptr-1];
        f->filterptr++;
        // Can be combined with cutoff set
        if (ltable[FTBL][f->filterptr-1] == 0x00)
        {
          f->filtercutoff = rtable[FTBL][f->filterptr-1];
          f->filterptr++;
        }
      }
      else
      {
        // New modulation step
        if (ltable[FTBL][f->filterptr-1])
          f->filtertime = ltable[FTBL][f->filterptr-1];
        else
        {
          // Cutoff set
          f->filtercutoff = rtable[FTBL][f->filterptr-1];
          f->filterptr++;
        }
      }
    }
    // Filter modulation
    if (f->filtertime)
    {
      f->filtercutoff += rtable[FTBL][f->filterptr-1];
      f->filtertime--;
      if (!f->filtertime) f->filterptr++;
    }
  }
  FILTERSTOP:
  sidreg[s][0x15] = 0x00;
  sidreg[s][0x16] = f->filtercutoff;
  sidreg[s][0x17] = f->filterctrl;
  sidreg[s][0x18] = f->filtertype | f->masterfader;
}

static unsigned short calcspeed(CHN *cptr, unsigned char param)
{
  unsigned short speed = 0;
  if (param)
  {
    speed = (ltable[STBL][param-1] << 8) | rtable[STBL][param-1];
  }
  if (speed >= 0x8000)
  {
    speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
    speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
    speed >>= rtable[STBL][param-1];
  }
  return speed;
}

static void toneporta(CHN *cptr, unsigned char param)
{
  unsigned short targetfreq = freqtbllo[cptr->note] | (freqtblhi[cptr->note] << 8);
  unsigned short speed = 0;

  if (!param)
  {
    cptr->freq = targetfreq;
    cptr->vibtime = 0;
  }
  else
  {
    speed = calcspeed(cptr, param);
    if (cptr->freq < targetfreq)
    {
      cptr->freq += speed;
      if (cptr->freq > targetfreq)
      {
        cptr->freq = targetfreq;
        cptr->vibtime = 0;
      }
    }
    if (cptr->freq > targetfreq)
    {
      cptr->freq -= speed;
      if (cptr->freq < targetfreq)
      {
        cptr->freq = targetfreq;
        cptr->vibtime = 0;
      }
    }
  }
}

static void vibrato(CHN *cptr, unsigned char param)
{
  unsigned short speed = 0;
  unsigned char cmpvalue = 0;

  if (param)
  {
    cmpvalue = ltable[STBL][param-1];
    speed = rtable[STBL][param-1];
  }
  if (cmpvalue >= 0x80)
  {
    cmpvalue &= 0x7f;
    speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
    speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
    speed >>= rtable[STBL][param-1];
  }

  if ((cptr->vibtime < 0x80) && (cptr->vibtime > cmpvalue))
    cptr->vibtime ^= 0xff;
  cptr->vibtime += 0x02;
  if (cptr->vibtime & 0x01)
    cptr->freq -= speed;
  else
    cptr->freq += speed;
}

void playroutine(void)
{
  INSTR *iptr;
  CHN *cptr = &chn[0];
  int c;

  if (songinit == PLAY_STOP)
    followplay = 0;

  if ((songinit > 0) && (songinit < PLAY_STOPPED))
  {
    lastsonginit = songinit;

    for (c = 0; c < MAX_SIDS; c++)
    {
      sidfilt[c].filterctrl = 0;
      sidfilt[c].filterptr = 0;
    }

    resettime();
    playframes = 0;

    if ((songinit == 0x02) || (songinit == 0x03))
    {
      for (c = 0; c < numchannels; c++)
      {
        if (editorUndoInfo.editorInfo[c].espos >= songlen[psnum][c])
          songinit = 0x01;
      }
    }

    for (c = 0; c < numchannels; c++)
    {
      cptr->songptr = 0;
      cptr->command = 0;
      cptr->cmddata = 0;
      cptr->newcommand = 0;
      cptr->newcmddata = 0;
      cptr->advance = 1;
      cptr->wave = 0;
      cptr->ptr[WTBL] = 0;
      cptr->newnote = 0;
      cptr->repeat = 0;
      cptr->looped = 0;
      cptr->tick = defaulttempo();
      cptr->gatetimer = instr[1].gatetimer & 0x3f;
      cptr->pattptr = 0x7fffffff;
      if (cptr->tempo < 2) cptr->tempo = 0;

      switch (songinit)
      {
        case PLAY_BEGINNING:
        if (editorInfo.multiplier)
        {
          funktable[0] = 9* editorInfo.multiplier-1;
          funktable[1] = 6* editorInfo.multiplier-1;
        }
        else
        {
          funktable[0] = 9-1;
          funktable[1] = 6-1;
        }
        cptr->tempo = defaulttempo();
        if ((instr[MAX_INSTR-1].ad >= 2) && (!(instr[MAX_INSTR-1].ptr[WTBL])))
          cptr->tempo = instr[MAX_INSTR-1].ad - 1;
        cptr->trans = 0;
        cptr->instr = 1;
        sequencer(c, cptr);
        break;

        case PLAY_PATTERN:
        cptr->advance = 0;
        cptr->pattptr = startpattpos * 4;
        cptr->pattnum = editorUndoInfo.editorInfo[c].epnum;
        if (cptr->pattptr >= (unsigned)(pattlen[cptr->pattnum] * 4))
          cptr->pattptr = 0;
        break;

        case PLAY_POS:
        cptr->songptr = editorUndoInfo.editorInfo[c].espos;
        sequencer(c, cptr);
        break;
      }
      cptr++;
    }
    if (songinit != PLAY_STOP)
      songinit = 0;
    else
      songinit = PLAY_STOPPED;
    if (!songvalid(psnum))
      songinit = PLAY_STOPPED; // Zero length song

    startpattpos = 0;
  }
  else
  {
    for (c = 0; c < numsids; c++)
      filterexec(c);

    for (c = 0; c < numchannels; c++)
    {
      SIDFILT *filt = &sidfilt[c / 3];
      iptr = &instr[cptr->instr];

      // Reset tempo in jammode
      if ((songinit == PLAY_STOPPED) && (cptr->tempo < 2))
        cptr->tempo = defaulttempo();

      // Decrease tick
      cptr->tick--;
      if (!cptr->tick) goto TICK0;

      // Tick N
      // Reload counter
      if (cptr->tick >= 0x80)
      {
        if (cptr->tempo >= 2)
          cptr->tick = cptr->tempo;
        else
        {
          // Set funktempo, switch between 2 values
          cptr->tick = funktable[cptr->tempo];
          cptr->tempo ^= 1;
        }
        // Check for illegally high gatetimer and stop the song in this case
        if (cptr->gatetimer > cptr->tick)
          stopsong();
      }
      goto WAVEEXEC;

      // Tick 0
      TICK0:
      // Advance in sequencer
      sequencer(c, cptr);

      // Get gatetimer compare-value
      cptr->gatetimer = iptr->gatetimer & 0x3f;

      // New note init
      if (cptr->newnote)
      {
        cptr->note = cptr->newnote-FIRSTNOTE;
        cptr->command = 0;
        cptr->vibdelay = iptr->vibdelay;
        cptr->cmddata = iptr->ptr[STBL];
        if (cptr->newcommand != CMD_TONEPORTA)
        {
          if (iptr->firstwave)
          {
            if (iptr->firstwave >= 0xfe) cptr->gate = iptr->firstwave;
            else
            {
              cptr->wave = iptr->firstwave;
              cptr->gate = 0xff;
            }
          }

          cptr->ptr[WTBL] = iptr->ptr[WTBL];
          stopiftablejump(WTBL, cptr->ptr[WTBL]);

          if (iptr->ptr[PTBL])
          {
            cptr->ptr[PTBL] = iptr->ptr[PTBL];
            cptr->pulsetime = 0;
            stopiftablejump(PTBL, cptr->ptr[PTBL]);
          }
          if (iptr->ptr[FTBL])
            setfilterptr(filt, iptr->ptr[FTBL]);
          SREG(c, 0x5) = iptr->ad;
          SREG(c, 0x6) = iptr->sr;
        }
      }

      // Tick 0 effects

      switch (cptr->newcommand)
      {
        case CMD_DONOTHING:
        cptr->command = 0;
        cptr->cmddata = iptr->ptr[STBL];
        break;

        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        cptr->vibtime = 0;
        cptr->command = cptr->newcommand;
        cptr->cmddata = cptr->newcmddata;
        break;

        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        cptr->command = cptr->newcommand;
        cptr->cmddata = cptr->newcmddata;
        break;

        case CMD_SETAD:
        SREG(c, 0x5) = cptr->newcmddata;
        break;

        case CMD_SETSR:
        SREG(c, 0x6) = cptr->newcmddata;
        break;

        case CMD_SETWAVE:
        cptr->wave = cptr->newcmddata;
        break;

        case CMD_SETWAVEPTR:
        cptr->ptr[WTBL] = cptr->newcmddata;
        cptr->wavetime = 0;
        stopiftablejump(WTBL, cptr->ptr[WTBL]);
        break;

        case CMD_SETPULSEPTR:
        cptr->ptr[PTBL] = cptr->newcmddata;
        cptr->pulsetime = 0;
        stopiftablejump(PTBL, cptr->ptr[PTBL]);
        break;

        case CMD_SETFILTERPTR:
        setfilterptr(filt, cptr->newcmddata);
        break;

        case CMD_SETFILTERCTRL:
        filt->filterctrl = cptr->newcmddata;
        if (!filt->filterctrl) filt->filterptr = 0;
        break;

        case CMD_SETFILTERCUTOFF:
        filt->filtercutoff = cptr->newcmddata;
        break;

        case CMD_SETMASTERVOL:
        if (cptr->newcmddata < 0x10)
          filt->masterfader = cptr->newcmddata;
        break;

        case CMD_FUNKTEMPO:
        if (cptr->newcmddata)
        {
          funktable[0] = ltable[STBL][cptr->newcmddata-1]-1;
          funktable[1] = rtable[STBL][cptr->newcmddata-1]-1;
        }
        setalltempo(0);
        break;

        case CMD_SETTEMPO:
        {
          unsigned char newtempo = cptr->newcmddata & 0x7f;

          if (newtempo >= 3) newtempo--;
          if (cptr->newcmddata >= 0x80)
            cptr->tempo = newtempo;
          else
            setalltempo(newtempo);
        }
        break;
      }
      if (cptr->newnote)
      {
        cptr->newnote = 0;
        if (cptr->newcommand != CMD_TONEPORTA) goto NEXTCHN;
      }

      WAVEEXEC:
      if (cptr->ptr[WTBL])
      {
        unsigned char wave = ltable[WTBL][cptr->ptr[WTBL]-1];
        unsigned char note = rtable[WTBL][cptr->ptr[WTBL]-1];

        if (wave > WAVELASTDELAY)
        {
          // Normal waveform values
          if (wave < WAVESILENT) cptr->wave = wave;
          // Values without waveform selected
          if ((wave >= WAVESILENT) && (wave <= WAVELASTSILENT)) cptr->wave = wave & 0xf;
          // Command execution from wavetable
          if ((wave >= WAVECMD) && (wave <= WAVELASTCMD))
          {
            unsigned char param = rtable[WTBL][cptr->ptr[WTBL]-1];
            switch (wave & 0xf)
            {
              case CMD_DONOTHING:
              case CMD_SETWAVEPTR:
              case CMD_FUNKTEMPO:
              stopsong();
              break;

              case CMD_PORTAUP:
              cptr->freq += calcspeed(cptr, param);
              break;

              case CMD_PORTADOWN:
              cptr->freq -= calcspeed(cptr, param);
              break;

              case CMD_TONEPORTA:
              toneporta(cptr, param);
              break;

              case CMD_VIBRATO:
              vibrato(cptr, param);
              break;

              case CMD_SETAD:
              SREG(c, 0x5) = param;
              break;

              case CMD_SETSR:
              SREG(c, 0x6) = param;
              break;

              case CMD_SETWAVE:
              cptr->wave = param;
              break;

              case CMD_SETPULSEPTR:
              cptr->ptr[PTBL] = param;
              cptr->pulsetime = 0;
              stopiftablejump(PTBL, cptr->ptr[PTBL]);
              break;

              case CMD_SETFILTERPTR:
              setfilterptr(filt, param);
              break;

              case CMD_SETFILTERCTRL:
              filt->filterctrl = param;
              if (!filt->filterctrl) filt->filterptr = 0;
              break;

              case CMD_SETFILTERCUTOFF:
              filt->filtercutoff = param;
              break;

              case CMD_SETMASTERVOL:
              if (param < 0x10)
                filt->masterfader = param;
              break;
            }
          }
        }
        else
        {
          // Wavetable delay
          if (cptr->wavetime != wave)
          {
            cptr->wavetime++;
            goto TICKNEFFECTS;
          }
        }

        cptr->wavetime = 0;
        cptr->ptr[WTBL]++;
        // Wavetable jump
        if (ltable[WTBL][cptr->ptr[WTBL]-1] == 0xff)
        {
          cptr->ptr[WTBL] = rtable[WTBL][cptr->ptr[WTBL]-1];
        }

        if ((wave >= WAVECMD) && (wave <= WAVELASTCMD))
          goto PULSEEXEC;

        if (note != 0x80)
        {
          if (note < 0x80)
            note += cptr->note;
          note &= 0x7f;
          cptr->freq = freqtbllo[note] | (freqtblhi[note]<<8);
          cptr->vibtime = 0;
          cptr->lastnote = note;
          goto PULSEEXEC;
        }
      }

      // Tick N command
      TICKNEFFECTS:
      if ((!editorInfo.optimizerealtime) || (cptr->tick))
      {
        switch(cptr->command)
        {
          case CMD_PORTAUP:
          cptr->freq += calcspeed(cptr, cptr->cmddata);
          break;

          case CMD_PORTADOWN:
          cptr->freq -= calcspeed(cptr, cptr->cmddata);
          break;

          case CMD_DONOTHING:
          if ((!cptr->cmddata) || (!cptr->vibdelay))
            break;
          if (cptr->vibdelay > 1)
          {
            cptr->vibdelay--;
            break;
          }
          // fallthrough
          case CMD_VIBRATO:
          vibrato(cptr, cptr->cmddata);
          break;

          case CMD_TONEPORTA:
          toneporta(cptr, cptr->cmddata);
          break;
        }
      }

      PULSEEXEC:
      if (editorInfo.optimizepulse)
      {
        if ((songinit != PLAY_STOPPED) && (cptr->tick == cptr->gatetimer)) goto GETNEWNOTES;
      }

      if (cptr->ptr[PTBL])
      {
        // Skip pulse when sequencer has been executed
        if (editorInfo.optimizepulse)
        {
          if ((!cptr->tick) && (!cptr->pattptr)) goto NEXTCHN;
        }

        // Pulsetable jump
        if (ltable[PTBL][cptr->ptr[PTBL]-1] == 0xff)
        {
          cptr->ptr[PTBL] = rtable[PTBL][cptr->ptr[PTBL]-1];
          if (!cptr->ptr[PTBL]) goto PULSEEXEC;
        }

        if (!cptr->pulsetime)
        {
          // Set pulse
          if (ltable[PTBL][cptr->ptr[PTBL]-1] >= 0x80)
          {
            cptr->pulse = (ltable[PTBL][cptr->ptr[PTBL]-1] & 0xf) << 8;
            cptr->pulse |= rtable[PTBL][cptr->ptr[PTBL]-1];
            cptr->ptr[PTBL]++;
          }
          else
          {
            cptr->pulsetime = ltable[PTBL][cptr->ptr[PTBL]-1];
          }
        }
        // Pulse modulation
        if (cptr->pulsetime)
        {
          unsigned char speed = rtable[PTBL][cptr->ptr[PTBL]-1];
          if (speed < 0x80)
          {
            cptr->pulse += speed;
            cptr->pulse &= 0xfff;
          }
          else
          {
            cptr->pulse += speed;
            cptr->pulse -= 0x100;
            cptr->pulse &= 0xfff;
          }
          cptr->pulsetime--;
          if (!cptr->pulsetime) cptr->ptr[PTBL]++;
        }
      }
      if ((songinit == PLAY_STOPPED) || (cptr->tick != cptr->gatetimer)) goto NEXTCHN;

      // New notes processing
      GETNEWNOTES:
      {
        unsigned char newnote;

        newnote = pattern[cptr->pattnum][cptr->pattptr];
        if (pattern[cptr->pattnum][cptr->pattptr+1])
          cptr->instr = pattern[cptr->pattnum][cptr->pattptr+1];
        cptr->newcommand = pattern[cptr->pattnum][cptr->pattptr+2];
        cptr->newcmddata = pattern[cptr->pattnum][cptr->pattptr+3];
        cptr->pattptr += 4;
        if (pattern[cptr->pattnum][cptr->pattptr] == ENDPATT)
          cptr->pattptr = 0x7fffffff;

        if (newnote == KEYOFF)
          cptr->gate = 0xfe;
        if (newnote == KEYON)
          cptr->gate = 0xff;
        if (newnote <= LASTNOTE)
        {
          cptr->newnote = newnote+cptr->trans;
          if ((cptr->newcommand) != CMD_TONEPORTA)
          {
            if (!(instr[cptr->instr].gatetimer & 0x40))
            {
              cptr->gate = 0xfe;
              if (!(instr[cptr->instr].gatetimer & 0x80))
              {
                SREG(c, 0x5) = editorInfo.adparam>>8;
                SREG(c, 0x6) = editorInfo.adparam&0xff;
              }
            }
          }
        }
      }
      NEXTCHN:
      if (cptr->mute)
        SREG(c, 0x4) = cptr->wave = 0x08;
      else
      {
        SREG(c, 0x0) = cptr->freq & 0xff;
        SREG(c, 0x1) = cptr->freq >> 8;
        SREG(c, 0x2) = cptr->pulse & 0xfe;
        SREG(c, 0x3) = cptr->pulse >> 8;
        SREG(c, 0x4) = cptr->wave & cptr->gate;
      }
      cptr++;
    }
  }
  if (songinit != PLAY_STOPPED)
  {
    incrementtime();
    playframes++;
  }
}

void sequencer(int c, CHN *cptr)
{
  if ((songinit != PLAY_STOPPED) && (cptr->pattptr == 0x7fffffff))
  {
    cptr->pattptr = startpattpos * 4;
    if (!cptr->advance) goto SEQDONE;
    // Song loop
    if (songorder[psnum][c][cptr->songptr] == LOOPSONG)
    {
      cptr->songptr = songorder[psnum][c][cptr->songptr+1];
      cptr->looped++;
      if (cptr->songptr >= songlen[psnum][c])
      {
        stopsong();
        cptr->songptr = 0;
        goto SEQDONE;
      }
    }
    // Transpose
    if ((songorder[psnum][c][cptr->songptr] >= TRANSDOWN) && (songorder[psnum][c][cptr->songptr] < LOOPSONG))
    {
      cptr->trans = songorder[psnum][c][cptr->songptr]-TRANSUP;
      cptr->songptr++;
    }
    // Repeat
    if ((songorder[psnum][c][cptr->songptr] >= REPEAT) && (songorder[psnum][c][cptr->songptr] < TRANSDOWN))
    {
      cptr->repeat = songorder[psnum][c][cptr->songptr]-REPEAT;
      cptr->songptr++;
    }
    // Pattern number
    cptr->pattnum = songorder[psnum][c][cptr->songptr];
    if (cptr->repeat)
      cptr->repeat--;
    else
      cptr->songptr++;

    // Check for illegal pattern now
    if (cptr->pattnum >= MAX_PATT)
    {
      stopsong();
      cptr->pattnum = 0;
    }
    if (cptr->pattptr >= (unsigned)(pattlen[cptr->pattnum] * 4))
      cptr->pattptr = 0;

    // Check for playback endpos
    if ((lastsonginit != PLAY_BEGINNING) && (editorUndoInfo.editorInfo[c].esend > 0) && (editorUndoInfo.editorInfo[c].esend > editorUndoInfo.editorInfo[c].espos) && (cptr->songptr > editorUndoInfo.editorInfo[c].esend) && (editorUndoInfo.editorInfo[c].espos < songlen[psnum][c]))
      cptr->songptr = editorUndoInfo.editorInfo[c].espos;
  }
  SEQDONE: {}
}

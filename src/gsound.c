//
// GOATTRACKER sound routines
//
// C64 GrooveGoat: stereo output of up to 8 emulated SIDs. HardSID/CatWeasel
// hardware output is still supported, but only for the first SID.
//

#define GSOUND_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include "goattrk2.h"

// General / reSID output
int playspeed;
int usehardsid = 0;
int usecatweasel = 0;
int initted = 0;
int firsttimeinit = 1;
unsigned framerate = PALFRAMERATE;
Sint16 *buffer = NULL;
FILE *writehandle = NULL;
int stereooutput = 1;

void sound_playrout(void);
void sound_mixer(Sint32 *dest, unsigned samples);
Uint32 sound_timer(Uint32 interval, void *param);
static SDL_TimerID soundtimer = 0;

#ifdef __WIN32__

// Win32 HardSID output
typedef void (CALLBACK* lpWriteToHardSID)(Uint8 DeviceID, Uint8 SID_reg, Uint8 Data);
typedef Uint8 (CALLBACK* lpReadFromHardSID)(Uint8 DeviceID, Uint8 SID_reg);
typedef void (CALLBACK* lpInitHardSID_Mapper)(void);
typedef void (CALLBACK* lpMuteHardSID_Line)(int Mute);
lpWriteToHardSID WriteToHardSID = NULL;
lpReadFromHardSID ReadFromHardSID = NULL;
lpInitHardSID_Mapper InitHardSID_Mapper = NULL;
lpMuteHardSID_Line MuteHardSID_Line = NULL;
HINSTANCE hardsiddll = 0;
int dll_initialized = FALSE;
void InitHardDLL(void);

// Win32 CatWeasel MK3 PCI output
#define SID_SID_PEEK_POKE   CTL_CODE(FILE_DEVICE_SOUND,0x0800UL + 1,METHOD_BUFFERED,FILE_ANY_ACCESS)
HANDLE catweaselhandle;

#else

// Unix HardSID & CatWeasel output
int hardsidfd = -1;
int catweaselfd = -1;

#endif

int sound_init(unsigned b, unsigned mr, unsigned writer, unsigned hardsid, unsigned model, unsigned ntsc, unsigned multiplier, unsigned catweasel, unsigned interpolate, unsigned customclockrate)
{
  int c;

  sound_uninit();

  if (multiplier)
  {
    if (ntsc)
    {
      framerate = NTSCFRAMERATE * multiplier;
      snd_bpmtempo = 150 * multiplier;
    }
    else
    {
      framerate = PALFRAMERATE * multiplier;
      snd_bpmtempo = 125 * multiplier;
    }
  }
  else
  {
    if (ntsc)
    {
      framerate = NTSCFRAMERATE / 2;
      snd_bpmtempo = 150 / 2;
    }
    else
    {
      framerate = PALFRAMERATE / 2;
      snd_bpmtempo = 125 / 2;
    }
  }

  if (hardsid)
  {
    #ifdef __WIN32__
    InitHardDLL();
    if (dll_initialized)
    {
      usehardsid = hardsid;
      for (c = 0; c < NUMSIDREGS; c++)
      {
        sidreg[0][c] = 0;
        WriteToHardSID(usehardsid-1, c, 0x00);
      }
      MuteHardSID_Line(FALSE);
    }
    else return 0;
    #else
    char filename[80];
    sprintf(filename, "/dev/sid%d", hardsid-1);
    hardsidfd = open(filename, O_WRONLY, S_IRUSR|S_IWUSR);
    if (hardsidfd >= 0)
    {
      usehardsid = hardsid;
      for (c = 0; c < NUMSIDREGS; c++)
      {
        Uint32 dataword = c << 8;
        write(hardsidfd, &dataword, 4);
      }
    }
    else return 0;
    #endif
    soundtimer = SDL_AddTimer(1000 / framerate, sound_timer, NULL);
    goto SOUNDOK;
  }

  if (catweasel)
  {
    #ifdef __WIN32__
    catweaselhandle = CreateFile("\\\\.\\SID6581_1", GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ, 0L,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0L);
    if (catweaselhandle == INVALID_HANDLE_VALUE)
      return 0;
    #else
    catweaselfd = open("/dev/sid", O_WRONLY);
    if (catweaselfd < 0)
      catweaselfd = open("/dev/misc/sid", O_WRONLY);
    if (catweaselfd < 0)
      return 0;
    #ifdef __linux__
    if (ntsc)
      ioctl(catweaselfd, CWSID_IOCTL_NTSC);
    else
      ioctl(catweaselfd, CWSID_IOCTL_PAL);
    #endif
    #endif

    usecatweasel = 1;
    soundtimer = SDL_AddTimer(1000 / framerate, sound_timer, NULL);
    goto SOUNDOK;
  }

  if (!buffer) buffer = (Sint16*)malloc(MIXBUFFERSIZE * 2 * sizeof(Sint16));
  if (!buffer) return 0;

  if (writer)
    writehandle = fopen("sidaudio.raw", "wb");

  playspeed = mr;
  if (playspeed < MINMIXRATE) playspeed = MINMIXRATE;
  if (playspeed > MAXMIXRATE) playspeed = MAXMIXRATE;
  if (b < MINBUF) b = MINBUF;
  if (b > MAXBUF) b = MAXBUF;

  if (firsttimeinit)
  {
    if (!snd_init(playspeed, SIXTEENBIT|STEREO, b)) return 0;
    firsttimeinit = 0;
  }
  playspeed = snd_mixrate;
  stereooutput = (snd_mixmode & STEREO) ? 1 : 0;

  snd_lock();
  sid_init(playspeed, model, ntsc, interpolate & 1, customclockrate, interpolate >> 1, numsids);
  sid_setpanning(numsids, stereomode);
  snd_player = &sound_playrout;
  snd_setcustommixer(sound_mixer);
  initted = 1;
  snd_unlock();
  return 1;

  SOUNDOK:
  initted = 1;
  return 1;
}

void sound_uninit(void)
{
  int c;

  if (!initted) return;

  if (usehardsid || usecatweasel)
  {
    initted = 0;
    if (soundtimer)
    {
      SDL_RemoveTimer(soundtimer);
      soundtimer = 0;
    }
    SDL_Delay(50);
  }
  else
  {
    snd_lock();
    initted = 0;
    snd_setcustommixer(NULL);
    snd_player = NULL;
    snd_unlock();
  }

  if (writehandle)
  {
    fclose(writehandle);
    writehandle = NULL;
  }

  if (usehardsid)
  {
    #ifdef __WIN32__
    for (c = 0; c < NUMSIDREGS; c++)
      WriteToHardSID(usehardsid-1, c, 0x00);
    MuteHardSID_Line(TRUE);
    #else
    if (hardsidfd >= 0)
    {
      for (c = 0; c < NUMSIDREGS; c++)
      {
        Uint32 dataword = c << 8;
        write(hardsidfd, &dataword, 4);
      }
      close(hardsidfd);
      hardsidfd = -1;
    }
    #endif
    usehardsid = 0;
  }

  if (usecatweasel)
  {
    #ifdef __WIN32__
    DWORD w;
    unsigned char buf[NUMSIDREGS * 2];
    for (w = 0; w < NUMSIDREGS; w++)
    {
      buf[w*2] = 0x18 - w;
      buf[w*2+1] = 0;
    }
    DeviceIoControl(catweaselhandle, SID_SID_PEEK_POKE, buf, sizeof(buf), 0L, 0UL, &w, 0L);
    CloseHandle(catweaselhandle);
    catweaselhandle = INVALID_HANDLE_VALUE;
    #else
    if (catweaselfd >= 0)
    {
      unsigned char buf[NUMSIDREGS];
      memset(buf, 0, sizeof(buf));
      lseek(catweaselfd, 0, SEEK_SET);
      write(catweaselfd, buf, sizeof(buf));
      close(catweaselfd);
      catweaselfd = -1;
    }
    #endif
    usecatweasel = 0;
  }
}

void sound_suspend(void)
{
}

void sound_flush(void)
{
}

Uint32 sound_timer(Uint32 interval, void *param)
{
  if (!initted) return interval;
  sound_playrout();
  return interval;
}

void sound_playrout(void)
{
  int c;

  playroutine();
  if (usehardsid)
  {
    #ifdef __WIN32__
    for (c = 0; c < NUMSIDREGS; c++)
    {
      unsigned o = sid_getorder(c, editorInfo.adparam);
      WriteToHardSID(usehardsid-1, o, sidreg[0][o]);
    }
    #else
    for (c = 0; c < NUMSIDREGS; c++)
    {
      unsigned o = sid_getorder(c, editorInfo.adparam);
      Uint32 dataword = (o << 8) | sidreg[0][o];
      write(hardsidfd, &dataword, 4);
    }
    #endif
  }
  else if (usecatweasel)
  {
    #ifdef __WIN32__
    DWORD w;
    unsigned char buf[NUMSIDREGS * 2];

    for(w = 0; w < NUMSIDREGS; w++)
    {
      unsigned o = sid_getorder(w, editorInfo.adparam);

      buf[w*2] = o;
      buf[w*2+1] = sidreg[0][o];
    }
    DeviceIoControl(catweaselhandle, SID_SID_PEEK_POKE, buf, sizeof(buf), 0L, 0UL, &w, 0L);
    #else
    for (c = 0; c < NUMSIDREGS; c++)
    {
      unsigned o = sid_getorder(c, editorInfo.adparam);

      lseek(catweaselfd, o, SEEK_SET);
      write(catweaselfd, &sidreg[0][o], 1);
    }
    #endif
  }
}

void sound_mixer(Sint32 *dest, unsigned samples)
{
  unsigned c;

  if (!initted) return;
  if (samples > MIXBUFFERSIZE) return;
  if (!buffer) return;

  sid_fillbuffer(buffer, samples, editorInfo.adparam, stereooutput);
  if (stereooutput)
  {
    if (writehandle)
      fwrite(buffer, samples * 2 * sizeof(Sint16), 1, writehandle);
    for (c = 0; c < samples * 2; c++)
      dest[c] = buffer[c];
  }
  else
  {
    if (writehandle)
      fwrite(buffer, samples * sizeof(Sint16), 1, writehandle);
    for (c = 0; c < samples; c++)
      dest[c] = buffer[c];
  }
}

#ifdef __WIN32__
void InitHardDLL()
{
  if (!(hardsiddll=LoadLibrary("HARDSID.DLL"))) return;

  WriteToHardSID = (lpWriteToHardSID) GetProcAddress(hardsiddll, "WriteToHardSID");
  ReadFromHardSID = (lpReadFromHardSID) GetProcAddress(hardsiddll, "ReadFromHardSID");
  InitHardSID_Mapper = (lpInitHardSID_Mapper) GetProcAddress(hardsiddll, "InitHardSID_Mapper");
  MuteHardSID_Line = (lpMuteHardSID_Line) GetProcAddress(hardsiddll, "MuteHardSID_Line");

  if (!WriteToHardSID) return;

  InitHardSID_Mapper();
  dll_initialized = TRUE;
}
#endif

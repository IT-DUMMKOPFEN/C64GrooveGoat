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

#ifdef USE_USBSID
#include "gusbsid.h"
#endif

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

#ifdef USE_USBSID

// USBSID-Pico output, the engine frame thread runs the player
#define USBSID_POLL_MS 1000
#define USBSID_POLL_BACKOFF_MS 5000
#define SOUND_USBSIDMAXINFO 64
static volatile int useusbsid = 0;
static int usbsidlost = 0;
static int usbsidexitset = 0;
static char usbsidwait[MAX_PATHNAME];
static Uint32 usbsidnextpoll = 0;
static volatile int suspendusbsid = 0;
static int sound_usbsidopen(const char *serials);
static void sound_usbsidframe(void);
static void sound_usbsidwaitfor(const char *serials);
static int sound_usbsidwarn(void);
static int usbsidwarned = 0;

#endif

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
  #ifdef USE_USBSID
  int wantusb = 0;
  #endif

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

  #ifdef USE_USBSID
  // Open the boards, play on reSID while none is available
  usbsidwarned = 0;
  if (usbsid && !usbsidlost)
  {
    usbsid_setmap(usbsidmap);
    if (usbsid_isopen() || sound_usbsidopen(usbsidboards))
      wantusb = 1;
    else
    {
      sound_usbsidwaitfor(usbsidboards);
      setstatusmessage("USBSID-Pico not found, using reSID");
    }
  }
  #endif

  if (!buffer) buffer = (Sint16*)malloc(MIXBUFFERSIZE * 2 * sizeof(Sint16));
  if (!buffer) return 0;

  #ifdef USE_USBSID
  if (writer && !wantusb)
  #else
  if (writer)
  #endif
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
  sid_setpanning(numsids, stereowidth());
  #ifdef USE_USBSID
  useusbsid = wantusb;
  snd_player = wantusb ? NULL : &sound_playrout;
  #else
  snd_player = &sound_playrout;
  #endif
  snd_setcustommixer(sound_mixer);
  initted = 1;
  snd_unlock();

  #ifdef USE_USBSID
  if (wantusb)
  {
    // Not under snd_lock: the frame thread takes it while the engine lock is held
    usbsid_settiming(ntsc, framerate);
    // Before the first frame resolves the map with the right song SID count
    usbsid_setsidcount(numsids < 1 ? 1 : (numsids > USBSID_MAXSIDS ? USBSID_MAXSIDS : numsids));
    suspendusbsid = 0;
    if (!usbsid_start(sound_usbsidframe))
    {
      usbsid_close();
      sound_usbsidwaitfor(usbsidboards);
      snd_lock();
      useusbsid = 0;
      snd_player = &sound_playrout;
      snd_unlock();
      setstatusmessage("USBSID-Pico not found, using reSID");
    }
    else
      usbsidwarned = sound_usbsidwarn();
  }
  #endif
  return 1;

  SOUNDOK:
  initted = 1;
  return 1;
}

void sound_uninit(void)
{
  int c;

  if (!initted) return;

  #ifdef USE_USBSID
  if (useusbsid)
  {
    snd_lock();
    initted = 0;
    snd_setcustommixer(NULL);
    snd_player = NULL;
    snd_unlock();
    // Boards stay open, they close at exit
    usbsid_stop();
    useusbsid = 0;
  }
  else
  #endif
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
  #ifdef USE_USBSID
  suspendusbsid = 1;
  #endif
}

void sound_flush(void)
{
  #ifdef USE_USBSID
  suspendusbsid = 0;
  #endif
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
  #ifdef USE_USBSID
  // The boards play the sound, reSID only feeds the envelope meters
  if (useusbsid) return;
  #endif
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

#ifdef USE_USBSID

// Open the boards and close them at exit
static int sound_usbsidopen(const char *serials)
{
  if (!usbsid_open(serials)) return 0;
  if (!usbsidexitset)
  {
    atexit(usbsid_close);
    usbsidexitset = 1;
  }
  return 1;
}

// Play on reSID and wait for these boards
static void sound_usbsidwaitfor(const char *serials)
{
  snprintf(usbsidwait, sizeof usbsidwait, "%s", serials);
  usbsidlost = 1;
  usbsidnextpoll = SDL_GetTicks() + USBSID_POLL_MS;
}

// Run the player and send one frame, called by the engine frame thread
static void sound_usbsidframe(void)
{
  int c, s, n;

  snd_lock();
  if (!suspendusbsid) playroutine();

  n = numsids;
  if (n < 1) n = 1;
  if (n > USBSID_MAXSIDS) n = USBSID_MAXSIDS;
  usbsid_setsidcount(n);

  // Write in reSID path order and spacing, SID after SID per register
  for (c = 0; c < NUMSIDREGS; c++)
  {
    unsigned char o = sid_getorder(c, editorInfo.adparam);
    for (s = 0; s < n; s++)
      usbsid_write(s, o, sidreg[s][o], c * SIDWRITEDELAY);
  }
  snd_unlock();
}

int sound_usbsidpoll(void)
{
  Uint32 now;

  if (!usbsidlost)
  {
    if (!useusbsid || !usbsid_lost()) return USBSID_EVENT_NONE;

    // Wait for the same boards, the song SIDs keep their places
    usbsid_openserials(usbsidwait, sizeof usbsidwait);
    usbsid_close();
    usbsidlost = 1;
    usbsidnextpoll = SDL_GetTicks() + USBSID_POLL_MS;
    return USBSID_EVENT_LOST;
  }

  now = SDL_GetTicks();
  if ((Sint32)(now - usbsidnextpoll) < 0) return USBSID_EVENT_NONE;
  usbsidnextpoll = now + USBSID_POLL_MS;

  if (!usbsid_allattached(usbsidwait)) return USBSID_EVENT_NONE;
  if (!sound_usbsidopen(usbsidwait))
  {
    // Attached but the open fails: do not hammer the board every second
    usbsidnextpoll = now + USBSID_POLL_BACKOFF_MS;
    return USBSID_EVENT_NONE;
  }

  usbsidlost = 0;
  return USBSID_EVENT_RECONNECTED;
}

int sound_usbsidmapoff(const char *map, int n)
{
  int i = 0;
  const char *end;

  while (map && i < n)
  {
    map = strchr(map, ',');
    if (map) map++;
    i++;
  }
  if (!map) return 0;
  end = strchr(map, ',');
  return usbsid_parsetoken(map, end ? (int)(end - map) : (int)strlen(map), NULL, 0, NULL) == USBSID_TOKEN_OFF;
}

// Set the D33 status message for the open boards: missing chip first, then song SIDs
// without chip. Return 1 if a message was set.
static int sound_usbsidwarn(void)
{
  USBSID_SIDINFO sids[SOUND_USBSIDMAXINFO];
  int target[USBSID_MAXSIDS];
  char msg[80];
  unsigned missing;
  int count, n, c, have = 0, without = 0;

  count = usbsid_sidinfo(sids, SOUND_USBSIDMAXINFO);
  n = numsids;
  if (n > USBSID_MAXSIDS) n = USBSID_MAXSIDS;
  if (n < 1) return 0;
  missing = usbsid_resolvemap(usbsidmap, sids, count, target, n);

  for (c = 0; c < n; c++)
  {
    if (missing & (1u << c))
    {
      snprintf(msg, sizeof msg, "USBSID-Pico: chip for SID %d not found, using auto", c + 1);
      setstatusmessage(msg);
      return 1;
    }
  }
  for (c = 0; c < n; c++)
  {
    if (target[c] >= 0) have++;
    else if (!sound_usbsidmapoff(usbsidmap, c)) without++;
  }
  if (without)
  {
    snprintf(msg, sizeof msg, "USBSID-Pico: song uses %d SIDs, %d connected", numsids, have);
    setstatusmessage(msg);
    return 1;
  }
  return 0;
}

int sound_usbsidprobe(USBSID_SIDINFO *info, int max, char *attached, int asize)
{
  int count = 0;

  if (attached && asize > 0) attached[0] = 0;
  if (usbsid)
  {
    sound_uninit();
    usbsid_close();
  }
  usbsid_attached(attached, asize);
  if (!usbsid) return 0;

  if (sound_usbsidopen(""))
  {
    count = usbsid_sidinfo(info, info ? max : 0);
    usbsid_close();
  }
  return count;
}

int sound_usbsidapply(unsigned on, const char *boards, const char *map)
{
  char msg[80];
  char b[MAX_PATHNAME], m[MAX_PATHNAME];

  // Copy first: boards and map may be the globals themselves
  snprintf(b, sizeof b, "%s", boards ? boards : "");
  snprintf(m, sizeof m, "%s", map ? map : "");
  usbsid = on ? 1 : 0;
  snprintf(usbsidboards, sizeof usbsidboards, "%s", b);
  snprintf(usbsidmap, sizeof usbsidmap, "%s", m);

  sound_uninit();
  usbsid_close();
  usbsidlost = 0;
  do_sound_init();

  if (!useusbsid) return 0;
  if (!usbsidwarned)
  {
    snprintf(msg, sizeof msg, "USBSID-Pico: %d SID(s) connected", usbsid_totalsids());
    setstatusmessage(msg);
  }
  return 1;
}

unsigned sound_usbsidresolve(const char *map, const USBSID_SIDINFO *sids, int count, int *target, int numtargets)
{
  return usbsid_resolvemap(map, sids, count, target, numtargets);
}

int sound_usbsidtarget(int songsid, USBSID_SIDINFO *chip)
{
  USBSID_SIDINFO sids[SOUND_USBSIDMAXINFO];
  int target[USBSID_MAXSIDS];
  int count, n;

  if (chip) memset(chip, 0, sizeof *chip);
  if (songsid < 0 || songsid >= USBSID_MAXSIDS) return -1;
  if (!usbsid_isopen()) return -1;
  count = usbsid_sidinfo(sids, SOUND_USBSIDMAXINFO);
  n = numsids;
  if (n > USBSID_MAXSIDS) n = USBSID_MAXSIDS;
  if (songsid >= n) return -1;
  usbsid_resolvemap(usbsidmap, sids, count, target, n);
  if (target[songsid] < 0) return -1;
  if (chip) *chip = sids[target[songsid]];
  return target[songsid];
}

int sound_usbsidboardcount(void)
{
  char tmp[512];

  return usbsid_openserials(tmp, sizeof tmp);
}

int sound_usbsidlost(void)
{
  return usbsidlost;
}

int sound_usbsidactive(void)
{
  return useusbsid;
}

int sound_usbsidtotalsids(void)
{
  return usbsid_isopen() ? usbsid_totalsids() : 0;
}

int sound_usbsidboards(char *attached, int asize, char *inuse, int isize)
{
  int count;

  if (attached && asize > 0) attached[0] = 0;
  if (inuse && isize > 0) inuse[0] = 0;
  count = usbsid_attached(attached, asize);
  if (inuse && isize > 0) usbsid_openserials(inuse, isize);
  return count;
}

void sound_usbsidstereo(int stereo)
{
  if (usbsid_isopen()) usbsid_setstereo(stereo);
}

#else

int sound_usbsidpoll(void)
{
  return USBSID_EVENT_NONE;
}

int sound_usbsidprobe(USBSID_SIDINFO *info, int max, char *attached, int asize)
{
  if (attached && asize > 0) attached[0] = 0;
  return 0;
}

int sound_usbsidapply(unsigned on, const char *boards, const char *map)
{
  return 0;
}

unsigned sound_usbsidresolve(const char *map, const USBSID_SIDINFO *sids, int count, int *target, int numtargets)
{
  int c;

  for (c = 0; c < numtargets; c++) target[c] = -1;
  return 0;
}

int sound_usbsidtarget(int songsid, USBSID_SIDINFO *chip)
{
  if (chip) memset(chip, 0, sizeof *chip);
  return -1;
}

int sound_usbsidboardcount(void)
{
  return 0;
}

int sound_usbsidmapoff(const char *map, int n)
{
  (void)map;
  (void)n;
  return 0;
}

// The dialog is built without the engine too, so the parser needs a stub here
int usbsid_parsetoken(const char *tok, int len, char *serial, int ssize, int *slot)
{
  (void)tok;
  (void)len;
  if (serial && ssize > 0) serial[0] = 0;
  if (slot) *slot = 0;
  return USBSID_TOKEN_MALFORMED;
}

int sound_usbsidlost(void)
{
  return 0;
}

int sound_usbsidactive(void)
{
  return 0;
}

int sound_usbsidtotalsids(void)
{
  return 0;
}

int sound_usbsidboards(char *attached, int asize, char *inuse, int isize)
{
  if (attached && asize > 0) attached[0] = 0;
  if (inuse && isize > 0) inuse[0] = 0;
  return 0;
}

void sound_usbsidstereo(int stereo)
{
}

#endif

int sound_usbsidbuilt(void)
{
  #ifdef USE_USBSID
  return 1;
  #else
  return 0;
  #endif
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

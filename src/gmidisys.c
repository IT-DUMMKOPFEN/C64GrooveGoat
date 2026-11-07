// C64 GrooveGoat - MIDI input, the system part
//
//   Windows  winmm (midiIn...), linked
//   macOS    CoreMIDI + CoreFoundation, loaded at runtime with dlopen
//   Linux    ALSA sequencer (libasound.so.2), loaded at runtime with dlopen
//
// The devices deliver their messages (possibly on their own threads) into a
// small queue; the editor reads them with midisys_read() on the main thread.
// New or removed devices are found by midisys_scan(), called once a second.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "gmidisys.h"

// ---------------------------------------------------------------------------
// Message queue
// ---------------------------------------------------------------------------

#define QSIZE 512
static unsigned char queue[QSIZE][4];
static volatile int qhead = 0, qtail = 0;
static SDL_mutex *qlock = NULL;

static char names[MIDISYS_MAXDEV][MIDISYS_NAMELEN];
static int used[MIDISYS_MAXDEV];
static int numdev = 0;
static char onlyname[MIDISYS_NAMELEN] = "";
static int listennone = 0;

// Raw data as it came from the system (for the MIDI dialog and midi.log):
// what arrives before it is parsed, so that a device that "listens" but
// delivers nothing usable can be told apart from one that delivers nothing.
#define RAWRING 64
#define RAWLEN 48
static unsigned char rawbuf[RAWRING][RAWLEN];
static int rawlen[RAWRING], rawdev[RAWRING];
static volatile int rawhead = 0;
static volatile unsigned rawtotal = 0;

static void rawnote(int dev, const unsigned char *data, int len)
{
  int h;
  if (!qlock) return;
  SDL_LockMutex(qlock);
  h = rawhead;
  if (len > RAWLEN) len = RAWLEN;
  if (len < 0) len = 0;
  memcpy(rawbuf[h], data, len);
  rawlen[h] = len;
  rawdev[h] = dev;
  rawhead = (h + 1) % RAWRING;
  rawtotal++;
  SDL_UnlockMutex(qlock);
}

unsigned midisys_rawcount(void) { return rawtotal; }

// the raw entries from number 'from' on (at most the last RAWRING); returns the new count
unsigned midisys_rawdump(unsigned from, void (*out)(int dev, const unsigned char *data, int len, void *ctx), void *ctx)
{
  unsigned total, n;
  if (!qlock) return from;
  SDL_LockMutex(qlock);
  total = rawtotal;
  if (total - from > RAWRING) from = total - RAWRING;
  for (n = from; n < total; n++)
  {
    int i = (int)((rawhead + RAWRING - (total - n)) % RAWRING);
    out(rawdev[i], rawbuf[i], rawlen[i], ctx);
  }
  SDL_UnlockMutex(qlock);
  return total;
}

static void qpush(int dev, int st, int d1, int d2)
{
  int n;
  if (!qlock) return;
  SDL_LockMutex(qlock);
  n = (qhead + 1) % QSIZE;
  if (n != qtail)
  {
    queue[qhead][0] = st;
    queue[qhead][1] = d1;
    queue[qhead][2] = d2;
    queue[qhead][3] = dev;
    qhead = n;
  }
  SDL_UnlockMutex(qlock);
}

void midisys_push(int st, int d1, int d2)
{
  qpush(0, st, d1, d2);
}

int midisys_read(unsigned char *msg)
{
  int r = 0;
  if (!qlock) return 0;
  SDL_LockMutex(qlock);
  if (qtail != qhead)
  {
    memcpy(msg, queue[qtail], 4);
    qtail = (qtail + 1) % QSIZE;
    r = 1;
  }
  SDL_UnlockMutex(qlock);
  return r;
}

// Raw byte stream parser (running status, real time bytes ignored)
typedef struct
{
  unsigned char st, d[2];
  int n;
} PARSER;

#if defined(__MACOSX__) || defined(__APPLE__)
static void parsebyte(PARSER *p, int dev, unsigned char b)
{
  if (b >= 0xf8) return;                   // real time
  if (b & 0x80)
  {
    p->st = (b < 0xf0) ? b : 0;            // system messages are not used
    p->n = 0;
    return;
  }
  if (!p->st) return;
  p->d[p->n++] = b;
  {
    int need = ((p->st & 0xe0) == 0xc0) ? 1 : 2;   // program change / channel pressure
    if (p->n >= need)
    {
      qpush(dev, p->st, p->d[0], need > 1 ? p->d[1] : 0);
      p->n = 0;
    }
  }
}

#endif

static int wanted(const char *name)
{
  if (listennone) return 0;
  return (!onlyname[0]) || (!strcmp(onlyname, name));
}

int midisys_numdev(void) { return numdev; }
const char *midisys_name(int i) { return ((i >= 0) && (i < numdev)) ? names[i] : ""; }
int midisys_used(int i) { return ((i >= 0) && (i < numdev)) ? used[i] : 0; }

#if defined(__WIN32__) || defined(_WIN32)
// ---------------------------------------------------------------------------
// Windows: winmm
// ---------------------------------------------------------------------------
#include <windows.h>
#include <mmsystem.h>

static HMIDIIN handles[MIDISYS_MAXDEV];
static UINT lastcount = (UINT)-1;

static void CALLBACK wincallback(HMIDIIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2)
{
  (void)h; (void)p2;
  if (msg == MIM_DATA)
  {
    unsigned st = p1 & 0xff;
    unsigned char rb[3];
    rb[0] = st; rb[1] = (p1 >> 8) & 0xff; rb[2] = (p1 >> 16) & 0xff;
    rawnote((int)inst, rb, 3);
    if ((st >= 0x80) && (st < 0xf0)) qpush((int)inst, st, (p1 >> 8) & 0x7f, (p1 >> 16) & 0x7f);
  }
}

static void closeall(void)
{
  int c;
  for (c = 0; c < MIDISYS_MAXDEV; c++)
  {
    if (handles[c])
    {
      midiInStop(handles[c]);
      midiInReset(handles[c]);
      midiInClose(handles[c]);
      handles[c] = 0;
    }
    used[c] = 0;
  }
}

static void openall(void)
{
  UINT c, n = midiInGetNumDevs();
  closeall();
  lastcount = n;
  numdev = 0;
  for (c = 0; (c < n) && (numdev < MIDISYS_MAXDEV); c++)
  {
    MIDIINCAPSA caps;
    int i = numdev;
    if (midiInGetDevCapsA(c, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
    snprintf(names[i], MIDISYS_NAMELEN, "%s", caps.szPname);
    numdev++;
    if (!wanted(names[i])) continue;
    if (midiInOpen(&handles[i], c, (DWORD_PTR)wincallback, (DWORD_PTR)i, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
    {
      midiInStart(handles[i]);
      used[i] = 1;
    }
    else handles[i] = 0;
  }
}

static int sysinit(void) { openall(); return 1; }
static void sysquit(void) { closeall(); numdev = 0; }
static void sysreopen(void) { openall(); }
static int sysscan(void)
{
  if (midiInGetNumDevs() != lastcount)
  {
    openall();
    return 1;
  }
  return 0;
}
static void syspoll(void) {}

#elif defined(__MACOSX__) || defined(__APPLE__)
// ---------------------------------------------------------------------------
// macOS: CoreMIDI, loaded at runtime (declarations from MIDIServices.h)
// ---------------------------------------------------------------------------
#include <dlfcn.h>
#include <stdint.h>

typedef uint32_t MIDIObjectRef;
typedef MIDIObjectRef MIDIClientRef, MIDIPortRef, MIDIEndpointRef;
typedef const void *CFStringRef;
typedef int32_t OSStatus;
typedef unsigned long ItemCount;

#pragma pack(push, 4)
typedef struct
{
  uint64_t timeStamp;
  uint16_t length;
  unsigned char data[256];
} MIDIPacket;
typedef struct
{
  uint32_t numPackets;
  MIDIPacket packet[1];
} MIDIPacketList;
#pragma pack(pop)

typedef void (*MIDIReadProc)(const MIDIPacketList *pktlist, void *readRefCon, void *srcRefCon);

static OSStatus (*pMIDIClientCreate)(CFStringRef, void *, void *, MIDIClientRef *);
static OSStatus (*pMIDIInputPortCreate)(MIDIClientRef, CFStringRef, MIDIReadProc, void *, MIDIPortRef *);
static ItemCount (*pMIDIGetNumberOfSources)(void);
static MIDIEndpointRef (*pMIDIGetSource)(ItemCount);
static OSStatus (*pMIDIPortConnectSource)(MIDIPortRef, MIDIEndpointRef, void *);
static OSStatus (*pMIDIPortDisconnectSource)(MIDIPortRef, MIDIEndpointRef);
static OSStatus (*pMIDIObjectGetStringProperty)(MIDIObjectRef, CFStringRef, CFStringRef *);
static CFStringRef *pDisplayName, *pName;
static CFStringRef (*pCFStringCreateWithCString)(const void *, const char *, uint32_t);
static unsigned char (*pCFStringGetCString)(CFStringRef, char *, long, uint32_t);
static void (*pCFRelease)(const void *);

#define UTF8 0x08000100

static MIDIClientRef client = 0;
static MIDIPortRef port = 0;
static MIDIEndpointRef sources[MIDISYS_MAXDEV];
static int connected[MIDISYS_MAXDEV];
static PARSER parsers[MIDISYS_MAXDEV];
static ItemCount lastcount = (ItemCount)-1;

// The packet list as CoreMIDI documents it: numPackets (4 bytes), then the
// packets, each timeStamp (8), length (2), data; packed to 4 bytes, the next
// packet 4-byte aligned on ARM. In case a system lays it out with the natural
// 8-byte alignment instead (the first packet at offset 8), that layout is
// recognised from the first packet and used.
static int pktsane(const unsigned char *pk, const unsigned char *end)
{
  uint16_t len;
  if (pk + 10 > end) return 0;
  memcpy(&len, pk + 8, 2);
  if ((len < 1) || (len > 256)) return 0;
  return 1;
}

static int maclayout = 0;          // 0 unknown, 4 or 8: offset of the first packet

static void macread(const MIDIPacketList *list, void *readref, void *srcref)
{
  const unsigned char *base = (const unsigned char *)list;
  const unsigned char *end = base + 4 + 65536;      // a sane upper bound
  const unsigned char *pk;
  uint32_t c, num;
  int dev = (int)(intptr_t)srcref, lay;
  (void)readref;
  if ((dev < 0) || (dev >= MIDISYS_MAXDEV)) return;
  memcpy(&num, base, 4);
  rawnote(dev, base, 4 + 8 + 2 + 16);
  lay = maclayout;
  if (!lay)
  {
    if (pktsane(base + 4, end) && ((base[4 + 10] & 0x80) || (!pktsane(base + 8, end)))) lay = 4;
    else if (pktsane(base + 8, end)) lay = 8;
    else lay = 4;
    if (num) maclayout = lay;
  }
  pk = base + lay;
  for (c = 0; c < num; c++)
  {
    uint16_t len;
    int b;
    if (!pktsane(pk, end)) break;
    memcpy(&len, pk + 8, 2);
    for (b = 0; b < len; b++) parsebyte(&parsers[dev], dev, pk[10 + b]);
    if (lay == 8)
      pk = (const unsigned char *)(((uintptr_t)(pk + 10 + len) + 7) & ~(uintptr_t)7);
    else
    {
#if defined(__aarch64__) || defined(__arm__)
      pk = (const unsigned char *)(((uintptr_t)(pk + 10 + len) + 3) & ~(uintptr_t)3);
#else
      pk = pk + 10 + len;
#endif
    }
  }
}

int midisys_maclayout(void) { return maclayout; }

static void closeall(void)
{
  int c;
  for (c = 0; c < MIDISYS_MAXDEV; c++)
  {
    if (connected[c]) pMIDIPortDisconnectSource(port, sources[c]);
    connected[c] = 0;
    used[c] = 0;
  }
}

static void openall(void)
{
  ItemCount c, n;
  closeall();
  n = pMIDIGetNumberOfSources();
  lastcount = n;
  numdev = 0;
  for (c = 0; (c < n) && (numdev < MIDISYS_MAXDEV); c++)
  {
    int i = numdev;
    CFStringRef s = NULL;
    sources[i] = pMIDIGetSource(c);
    if (!sources[i]) continue;
    names[i][0] = 0;
    if ((pDisplayName) && (!pMIDIObjectGetStringProperty(sources[i], *pDisplayName, &s)) && (s))
    {
      pCFStringGetCString(s, names[i], MIDISYS_NAMELEN, UTF8);
      pCFRelease(s);
    }
    else if ((pName) && (!pMIDIObjectGetStringProperty(sources[i], *pName, &s)) && (s))
    {
      pCFStringGetCString(s, names[i], MIDISYS_NAMELEN, UTF8);
      pCFRelease(s);
    }
    if (!names[i][0]) snprintf(names[i], MIDISYS_NAMELEN, "MIDI source %d", (int)c + 1);
    numdev++;
    memset(&parsers[i], 0, sizeof(PARSER));
    if (!wanted(names[i])) continue;
    if (!pMIDIPortConnectSource(port, sources[i], (void *)(intptr_t)i))
    {
      connected[i] = 1;
      used[i] = 1;
    }
  }
}

static int sysinit(void)
{
  void *mid = dlopen("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI", RTLD_LAZY);
  void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
  CFStringRef nm;
  if ((!mid) || (!cf)) return 0;
  pMIDIClientCreate = (OSStatus (*)(CFStringRef, void *, void *, MIDIClientRef *))dlsym(mid, "MIDIClientCreate");
  pMIDIInputPortCreate = (OSStatus (*)(MIDIClientRef, CFStringRef, MIDIReadProc, void *, MIDIPortRef *))dlsym(mid, "MIDIInputPortCreate");
  pMIDIGetNumberOfSources = (ItemCount (*)(void))dlsym(mid, "MIDIGetNumberOfSources");
  pMIDIGetSource = (MIDIEndpointRef (*)(ItemCount))dlsym(mid, "MIDIGetSource");
  pMIDIPortConnectSource = (OSStatus (*)(MIDIPortRef, MIDIEndpointRef, void *))dlsym(mid, "MIDIPortConnectSource");
  pMIDIPortDisconnectSource = (OSStatus (*)(MIDIPortRef, MIDIEndpointRef))dlsym(mid, "MIDIPortDisconnectSource");
  pMIDIObjectGetStringProperty = (OSStatus (*)(MIDIObjectRef, CFStringRef, CFStringRef *))dlsym(mid, "MIDIObjectGetStringProperty");
  pDisplayName = (CFStringRef *)dlsym(mid, "kMIDIPropertyDisplayName");
  pName = (CFStringRef *)dlsym(mid, "kMIDIPropertyName");
  pCFStringCreateWithCString = (CFStringRef (*)(const void *, const char *, uint32_t))dlsym(cf, "CFStringCreateWithCString");
  pCFStringGetCString = (unsigned char (*)(CFStringRef, char *, long, uint32_t))dlsym(cf, "CFStringGetCString");
  pCFRelease = (void (*)(const void *))dlsym(cf, "CFRelease");
  if ((!pMIDIClientCreate) || (!pMIDIInputPortCreate) || (!pMIDIGetNumberOfSources) || (!pMIDIGetSource) ||
      (!pMIDIPortConnectSource) || (!pMIDIPortDisconnectSource) || (!pMIDIObjectGetStringProperty) ||
      (!pCFStringCreateWithCString) || (!pCFStringGetCString) || (!pCFRelease)) return 0;
  nm = pCFStringCreateWithCString(NULL, "C64 GrooveGoat", UTF8);
  if (pMIDIClientCreate(nm, NULL, NULL, &client)) return 0;
  if (pMIDIInputPortCreate(client, nm, macread, NULL, &port)) return 0;
  pCFRelease(nm);
  openall();
  return 1;
}

static void sysquit(void) { if (port) closeall(); numdev = 0; }
static void sysreopen(void) { if (port) openall(); }
static int sysscan(void)
{
  if ((port) && (pMIDIGetNumberOfSources() != lastcount))
  {
    openall();
    return 1;
  }
  return 0;
}
static void syspoll(void) {}

#else
// ---------------------------------------------------------------------------
// Linux: ALSA sequencer, loaded at runtime (declarations from alsa/seq.h)
// ---------------------------------------------------------------------------
#include <dlfcn.h>

typedef struct
{
  unsigned char type, flags, tag, queue;
  unsigned int time[2];
  unsigned char srcclient, srcport, dstclient, dstport;
  unsigned char data[12];
} SEQEVENT;

#define SEQ_OPEN_INPUT 2
#define SEQ_NONBLOCK 1
#define CAP_READ (1 << 0)
#define CAP_WRITE (1 << 1)
#define CAP_SUBS_READ (1 << 5)
#define CAP_SUBS_WRITE (1 << 6)
#define CAP_NO_EXPORT (1 << 7)
#define TYPE_MIDI_GENERIC (1 << 1)
#define TYPE_APPLICATION (1 << 20)
#define EV_NOTEON 6
#define EV_NOTEOFF 7
#define EV_CONTROLLER 10
#define EV_PGMCHANGE 11
#define EV_PITCHBEND 13
#define EV_CLIENT_START 60
#define EV_PORT_START 63
#define EV_PORT_EXIT 64

static int (*p_open)(void **, const char *, int, int);
static int (*p_close)(void *);
static int (*p_set_client_name)(void *, const char *);
static int (*p_create_simple_port)(void *, const char *, unsigned int, unsigned int);
static int (*p_client_id)(void *);
static int (*p_connect_from)(void *, int, int, int);
static int (*p_disconnect_from)(void *, int, int, int);
static int (*p_event_input)(void *, SEQEVENT **);
static int (*p_ci_malloc)(void **);
static void (*p_ci_free)(void *);
static void (*p_ci_set_client)(void *, int);
static int (*p_ci_get_client)(const void *);
static const char *(*p_ci_get_name)(void *);
static int (*p_query_next_client)(void *, void *);
static int (*p_pi_malloc)(void **);
static void (*p_pi_free)(void *);
static void (*p_pi_set_client)(void *, int);
static void (*p_pi_set_port)(void *, int);
static int (*p_pi_get_port)(const void *);
static unsigned int (*p_pi_get_capability)(const void *);
static const char *(*p_pi_get_name)(const void *);
static int (*p_query_next_port)(void *, void *);

static void *seq = NULL;
static int myport = -1, myclient = -1;
static unsigned char devclient[MIDISYS_MAXDEV], devport[MIDISYS_MAXDEV];
static int rescan = 0;

#define LOAD(var, name) if (!(*(void **)&var = dlsym(lib, name))) return 0

static void closeall(void)
{
  int c;
  for (c = 0; c < numdev; c++)
  {
    if (used[c]) p_disconnect_from(seq, myport, devclient[c], devport[c]);
    used[c] = 0;
  }
}

static void openall(void)
{
  void *ci, *pi;
  closeall();
  numdev = 0;
  if ((p_ci_malloc(&ci) < 0) || (p_pi_malloc(&pi) < 0)) return;
  p_ci_set_client(ci, -1);
  while ((p_query_next_client(seq, ci) >= 0) && (numdev < MIDISYS_MAXDEV))
  {
    int cl = p_ci_get_client(ci);
    if ((cl == 0) || (cl == myclient)) continue;          // system, ourselves
    p_pi_set_client(pi, cl);
    p_pi_set_port(pi, -1);
    while ((p_query_next_port(seq, pi) >= 0) && (numdev < MIDISYS_MAXDEV))
    {
      unsigned cap = p_pi_get_capability(pi);
      int i = numdev;
      if ((cap & (CAP_READ | CAP_SUBS_READ)) != (CAP_READ | CAP_SUBS_READ)) continue;
      if (cap & CAP_NO_EXPORT) continue;
      if (!strcmp(p_ci_get_name(ci), "Midi Through")) continue;
      snprintf(names[i], MIDISYS_NAMELEN, "%s", p_pi_get_name(pi));
      devclient[i] = cl;
      devport[i] = p_pi_get_port(pi);
      numdev++;
      if (!wanted(names[i])) continue;
      if (p_connect_from(seq, myport, cl, devport[i]) >= 0) used[i] = 1;
    }
  }
  p_pi_free(pi);
  p_ci_free(ci);
}

static void quieterror(const char *file, int line, const char *func, int err, const char *fmt, ...)
{
  (void)file; (void)line; (void)func; (void)err; (void)fmt;
}

static int sysinit(void)
{
  void *lib = dlopen("libasound.so.2", RTLD_LAZY);
  int (*seterrorhandler)(void *);
  if (!lib) return 0;
  // no ALSA error messages on the console (for example without /dev/snd/seq)
  *(void **)&seterrorhandler = dlsym(lib, "snd_lib_error_set_handler");
  if ((seterrorhandler) && (!getenv("GG_MIDIDEBUG"))) seterrorhandler((void *)quieterror);
  LOAD(p_open, "snd_seq_open");
  LOAD(p_close, "snd_seq_close");
  LOAD(p_set_client_name, "snd_seq_set_client_name");
  LOAD(p_create_simple_port, "snd_seq_create_simple_port");
  LOAD(p_client_id, "snd_seq_client_id");
  LOAD(p_connect_from, "snd_seq_connect_from");
  LOAD(p_disconnect_from, "snd_seq_disconnect_from");
  LOAD(p_event_input, "snd_seq_event_input");
  LOAD(p_ci_malloc, "snd_seq_client_info_malloc");
  LOAD(p_ci_free, "snd_seq_client_info_free");
  LOAD(p_ci_set_client, "snd_seq_client_info_set_client");
  LOAD(p_ci_get_client, "snd_seq_client_info_get_client");
  LOAD(p_ci_get_name, "snd_seq_client_info_get_name");
  LOAD(p_query_next_client, "snd_seq_query_next_client");
  LOAD(p_pi_malloc, "snd_seq_port_info_malloc");
  LOAD(p_pi_free, "snd_seq_port_info_free");
  LOAD(p_pi_set_client, "snd_seq_port_info_set_client");
  LOAD(p_pi_set_port, "snd_seq_port_info_set_port");
  LOAD(p_pi_get_port, "snd_seq_port_info_get_port");
  LOAD(p_pi_get_capability, "snd_seq_port_info_get_capability");
  LOAD(p_pi_get_name, "snd_seq_port_info_get_name");
  LOAD(p_query_next_port, "snd_seq_query_next_port");
  if (p_open(&seq, "default", SEQ_OPEN_INPUT, SEQ_NONBLOCK) < 0)
  {
    seq = NULL;
    return 0;
  }
  p_set_client_name(seq, "C64 GrooveGoat");
  myclient = p_client_id(seq);
  myport = p_create_simple_port(seq, "C64 GrooveGoat in", CAP_WRITE | CAP_SUBS_WRITE, TYPE_MIDI_GENERIC | TYPE_APPLICATION);
  if (myport < 0)
  {
    p_close(seq);
    seq = NULL;
    return 0;
  }
  p_connect_from(seq, myport, 0, 1);          // system announce: new / removed devices
  openall();
  return 1;
}

static void sysquit(void)
{
  if (!seq) return;
  closeall();
  p_close(seq);
  seq = NULL;
  numdev = 0;
}

static void sysreopen(void) { if (seq) openall(); }

static int devindex(int cl, int pt)
{
  int c;
  for (c = 0; c < numdev; c++)
    if ((devclient[c] == cl) && (devport[c] == pt)) return c;
  return 0;
}

static void syspoll(void)
{
  SEQEVENT *ev;
  if (!seq) return;
  while (p_event_input(seq, &ev) >= 0)
  {
    int ch, dev;
    if (!ev) continue;
    ch = ev->data[0] & 0x0f;
    dev = devindex(ev->srcclient, ev->srcport);
    {
      unsigned char rb[4];
      rb[0] = ev->type; rb[1] = ev->data[0]; rb[2] = ev->data[1]; rb[3] = ev->data[2];
      rawnote(dev, rb, 4);
    }
    switch (ev->type)
    {
      case EV_NOTEON: qpush(dev, 0x90 | ch, ev->data[1] & 0x7f, ev->data[2] & 0x7f); break;
      case EV_NOTEOFF: qpush(dev, 0x80 | ch, ev->data[1] & 0x7f, ev->data[2] & 0x7f); break;
      case EV_CONTROLLER:
      {
        unsigned param, value;
        memcpy(&param, &ev->data[4], 4);
        memcpy(&value, &ev->data[8], 4);
        qpush(dev, 0xb0 | ch, param & 0x7f, value & 0x7f);
      }
      break;
      case EV_CLIENT_START:
      case EV_PORT_START:
      case EV_PORT_EXIT:
      rescan = 1;
      break;
    }
  }
}

static int sysscan(void)
{
  if ((seq) && (rescan))
  {
    rescan = 0;
    openall();
    return 1;
  }
  return 0;
}
#endif

// ---------------------------------------------------------------------------

#if !(defined(__MACOSX__) || defined(__APPLE__))
int midisys_maclayout(void) { return 0; }
#endif

static int initdone = 0, sysok = 0;

int midisys_open(const char *only)
{
  listennone = !only;
  snprintf(onlyname, sizeof onlyname, "%s", only ? only : "");
  if (!qlock) qlock = SDL_CreateMutex();
  if (!initdone)
  {
    initdone = 1;
    sysok = sysinit();
  }
  else if (sysok) sysreopen();
  return sysok;
}

void midisys_close(void)
{
  if ((initdone) && (sysok)) sysquit();
  initdone = sysok = 0;
}

int midisys_scan(void)
{
  if (!sysok) return 0;
  return sysscan();
}

void midisys_poll(void)
{
  if (sysok) syspoll();
}

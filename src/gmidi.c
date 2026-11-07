// C64 GrooveGoat - MIDI keyboard
//
// Notes from a MIDI keyboard:
//  - EDIT MODE in the pattern editor: the note is entered at the cursor like
//    a note key of the computer keyboard (chords: one note after the other)
//  - everywhere else and in JAM MODE: the current instrument is played on all
//    voices of all SIDs, so chords can be played (a free voice is taken for
//    every key; when all voices are busy, the oldest note is replaced)
// The velocity is ignored (the SID has no volume per note). The sustain
// pedal (controller 64) holds the notes.

#include "goattrk2.h"
#include "gmidi.h"
#include "gmidisys.h"

unsigned midion = 1;
char mididevice[MAX_PATHNAME] = "";
int midi_inmain = 0;
int midi_pending = 0;

static int voicenote[MAX_CHN];     // MIDI note on the voice, -1 = free
static int voiceheld[MAX_CHN];     // released while the sustain pedal is down
static unsigned voicetime[MAX_CHN];
static unsigned voiceclock = 0;
static int sustain = 0;
static int inited = 0;
static Uint32 lastscan = 0;

#define ENTRYQ 64
static int entryq[ENTRYQ];
static int entryhead = 0, entrytail = 0;

static int lastnote = -1, lastvel = 0, lastdev = -1, numreceived = 0;

static int tonote(int midinote)
{
  int n = FIRSTNOTE + midinote - 12;      // MIDI 60 (middle C) = C-4
  if ((n < FIRSTNOTE) || (n > LASTNOTE)) return -1;
  return n;
}

// midi.log in the settings folder: the devices and the first raw data, so
// that a keyboard that is found but not heard can be diagnosed
static unsigned lograw = 0;
static int loglines = 0;

static void logdevices(const char *why)
{
  char path[MAX_PATHNAME];
  FILE *f;
  int c;
  configpath(path, "midi.log");
  f = fopen(path, loglines ? "a" : "w");
  if (!f) return;
  fprintf(f, "%s: MIDI %s, device \"%s\", %d source(s)\n", why, midion ? "on" : "off",
          mididevice[0] ? mididevice : "(all)", midisys_numdev());
  for (c = 0; c < midisys_numdev(); c++)
    fprintf(f, "  %2d %s%s\n", c, midisys_name(c), midisys_used(c) ? "   (listening)" : "");
  fclose(f);
  loglines += 2 + midisys_numdev();
}

static void lograwline(int dev, const unsigned char *data, int len, void *ctx)
{
  FILE *f = (FILE *)ctx;
  int c;
  fprintf(f, "  raw dev %d:", dev);
  for (c = 0; c < len; c++) fprintf(f, " %02X", data[c]);
  fprintf(f, "\n");
  loglines++;
}

static void lograwdata(void)
{
  char path[MAX_PATHNAME];
  FILE *f;
  if (midisys_rawcount() == lograw) return;
  if (loglines > 400)
  {
    lograw = midisys_rawcount();
    return;
  }
  configpath(path, "midi.log");
  f = fopen(path, "a");
  if (!f) return;
  lograw = midisys_rawdump(lograw, lograwline, f);
  if (midisys_maclayout()) fprintf(f, "  (packet layout %d)\n", midisys_maclayout());
  fclose(f);
}

static void reopen(void)
{
  midisys_open(midion ? mididevice : NULL);
  logdevices("open");
}

void midi_init(void)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
  {
    voicenote[c] = -1;
    voiceheld[c] = 0;
    voicetime[c] = 0;
  }
  if (getenv("GG_NOMIDI")) return;
  reopen();
  inited = 1;
}

void midi_shutdown(void)
{
  if (!inited) return;
  midisys_close();
  inited = 0;
}

void midi_inject(int status, int d1, int d2)
{
  if (!inited)
  {
    int c;
    for (c = 0; c < MAX_CHN; c++) voicenote[c] = -1;
    inited = 1;
  }
  midisys_push(status, d1, d2);
}

int midi_numdevices(void) { return midisys_numdev(); }
const char *midi_devicename(int i) { return midisys_name(i); }
int midi_deviceused(int i) { return midisys_used(i); }

int midi_active(void)
{
  int c;
  if (!midion) return 0;
  for (c = 0; c < midisys_numdev(); c++)
    if (midisys_used(c)) return 1;
  return 0;
}

static void releasevoice(int c)
{
  snd_lock();
  releasenote(c);
  snd_unlock();
  voicenote[c] = -1;
  voiceheld[c] = 0;
  voicetime[c] = ++voiceclock;
}

void midi_allnotesoff(void)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
    if (voicenote[c] >= 0) releasevoice(c);
  sustain = 0;
}

// a free voice for a new note: the current voice first, then the voice that
// is free for the longest time; with all voices busy the oldest note
static int allocvoice(void)
{
  int c, best = -1;
  unsigned bt = 0xffffffff;
  int n = numchannels;
  int cur = editorInfo.epchn;
  if (n > MAX_CHN) n = MAX_CHN;
  if ((cur < n) && (voicenote[cur] < 0) && (!chn[cur].mute)) return cur;
  for (c = 0; c < n; c++)
  {
    if ((voicenote[c] < 0) && (!chn[c].mute) && (voicetime[c] < bt))
    {
      bt = voicetime[c];
      best = c;
    }
  }
  if (best >= 0) return best;
  bt = 0xffffffff;
  for (c = 0; c < n; c++)
  {
    if ((!chn[c].mute) && (voicetime[c] < bt))
    {
      bt = voicetime[c];
      best = c;
    }
  }
  if (best < 0) best = (cur < n) ? cur : 0;
  return best;
}

static void noteon(int midinote)
{
  int n = tonote(midinote), c, v;
  if (n < 0) return;
  // the same key again: take its voice
  for (c = 0; c < MAX_CHN; c++)
  {
    if (voicenote[c] == midinote)
    {
      voicenote[c] = -1;
      voiceheld[c] = 0;
    }
  }
  v = allocvoice();
  snd_lock();
  playtestnote(n, editorInfo.einum, v);
  snd_unlock();
  voicenote[v] = midinote;
  voiceheld[v] = 0;
  voicetime[v] = ++voiceclock;

  if ((midi_inmain) && (editorInfo.editmode == EDIT_PATTERN) && (recordmode))
  {
    int nx = (entryhead + 1) % ENTRYQ;
    if (nx != entrytail)
    {
      entryq[entryhead] = n;
      entryhead = nx;
      midi_pending = 1;
    }
  }
}

static void noteoff(int midinote)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((voicenote[c] == midinote) && (!voiceheld[c]))
    {
      if (sustain) voiceheld[c] = 1;
      else releasevoice(c);
    }
  }
}

int midi_nextentry(void)
{
  int n;
  if (entrytail == entryhead)
  {
    midi_pending = 0;
    return -1;
  }
  n = entryq[entrytail];
  entrytail = (entrytail + 1) % ENTRYQ;
  if (entrytail == entryhead) midi_pending = 0;
  return n;
}

void midi_service(void)
{
  unsigned char m[4];
  Uint32 now;
  if (!inited) return;
  midisys_poll();
  now = SDL_GetTicks();
  if (now - lastscan >= 1000)
  {
    lastscan = now;
    if (midisys_scan())
    {
      midi_allnotesoff();
      logdevices("devices changed");
    }
    lograwdata();
  }
  while (midisys_read(m))
  {
    int st = m[0] & 0xf0;
    if ((st == 0x90) && (m[2] == 0)) st = 0x80;
    switch (st)
    {
      case 0x90:
      lastnote = m[1];
      lastvel = m[2];
      lastdev = m[3];
      numreceived++;
      noteon(m[1]);
      break;

      case 0x80:
      noteoff(m[1]);
      break;

      case 0xb0:
      if (m[1] == 64)
      {
        int c;
        sustain = m[2] >= 64;
        if (!sustain)
          for (c = 0; c < MAX_CHN; c++)
            if (voiceheld[c]) releasevoice(c);
      }
      if ((m[1] == 120) || (m[1] == 123)) midi_allnotesoff();
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// MIDI dialog: choose the keyboard, test it
// ---------------------------------------------------------------------------

static void lastrawtext(int dev, const unsigned char *data, int len, void *ctx)
{
  char *out = (char *)ctx;
  int c, o = 0;
  (void)dev;
  out[0] = 0;
  for (c = 0; (c < len) && (o < 72); c++) o += snprintf(out + o, 80 - o, "%02X ", data[c]);
}

void mididialog(void)
{
  int w = 84, h = 22;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 2;
  int sel = -99;
  int maxrows = h - 12;

  key = rawkey = 0;
  for (;;)
  {
    char buf[300];
    int c, n = midi_numdevices(), hit = -99, cur, rows, extra = 0, okhit = 0;
    int okx = x0 + 3, oky = y0 + h - 2;

    // the current choice: -2 off, -1 all devices, 0.. device, n = saved but missing
    if (!midion) cur = -2;
    else if (!mididevice[0]) cur = -1;
    else
    {
      cur = n;
      for (c = 0; c < n; c++)
        if (!strcmp(midi_devicename(c), mididevice)) cur = c;
    }
    if (cur == n) extra = 1;
    if (sel == -99) sel = cur;
    rows = n + extra + 2;
    if (sel < -2) sel = -2;
    if (sel > rows - 3) sel = rows - 3;

    printmainscreen();
    dlg_drawframe(x0, y0, w, h, "MIDI keyboard");
    printtext(x0 + 3, y0 + 2, MKCOL(14, C_PANEL), "Play notes with a MIDI keyboard (USB or MIDI interface).");
    dlg_printwrapped(x0 + 3, y0 + 3, w - 6, 2, MKCOL(8, C_PANEL),
      "EDIT MODE: the keys enter notes at the cursor in the pattern. JAM MODE: the instrument is played on all voices - chords work.");

    for (c = 0; (c < rows) && (c < maxrows); c++)
    {
      int item = c - 2, ry = y0 + 6 + c, isel = (sel == item), bg = isel ? C_SELECT : C_PANEL;
      const char *label;
      char lb[200];
      if (item == -2) label = "Off";
      else if (item == -1) label = "All MIDI devices";
      else if (item < n)
      {
        snprintf(lb, sizeof lb, "%.60s%s", midi_devicename(item), midi_deviceused(item) ? "   (listening)" : "");
        label = lb;
      }
      else
      {
        snprintf(lb, sizeof lb, "%.60s   (not connected)", mididevice);
        label = lb;
      }
      snprintf(buf, sizeof buf, "(%c) %s", (cur == item) ? '*' : ' ', label);
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : (cur == item ? 14 : 8), bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hit = item;
    }
    if (!n) printtext(x0 + 7, y0 + 8, MKCOL(10, C_PANEL), "No MIDI device found - connect the keyboard, it is found automatically.");

    if (lastnote >= 0)
    {
      int tn = tonote(lastnote);
      snprintf(buf, sizeof buf, "Last key: %s  (MIDI note %d, velocity %d%s%.40s)   keys received: %d",
               tn >= 0 ? notename[tn - FIRSTNOTE] : "out of range", lastnote, lastvel,
               (lastdev >= 0) && (lastdev < n) ? ", " : "", (lastdev >= 0) && (lastdev < n) ? midi_devicename(lastdev) : "", numreceived);
    }
    else snprintf(buf, sizeof buf, "Press a key on the MIDI keyboard to test it.");
    printblankc(x0 + 3, y0 + h - 4, MKCOL(8, C_PANEL), w - 6);
    printtext(x0 + 3, y0 + h - 4, MKCOL(lastnote >= 0 ? 13 : 3, C_PANEL), buf);
    // what the system delivered, parsed or not (diagnosis: midi.log in the settings folder)
    {
      static char raw[80] = "";
      static unsigned seen = 0;
      unsigned rc = midisys_rawcount();
      if (rc != seen)
      {
        seen = midisys_rawdump(rc ? rc - 1 : 0, lastrawtext, raw);
      }
      if (rc) snprintf(buf, sizeof buf, "Data from the system: %u message(s), last: %.48s", rc, raw);
      else snprintf(buf, sizeof buf, "Data from the system: none yet (details in midi.log in the settings folder)");
      printblankc(x0 + 3, y0 + h - 5, MKCOL(8, C_PANEL), w - 6);
      printtext(x0 + 3, y0 + h - 5, MKCOL(rc ? 8 : 3, C_PANEL), buf);
    }
    printtext(okx, oky, MKCOL(C_BUTTONTEXT, C_BUTTONHI), "   OK   ");
    printtext(okx + 11, oky, MKCOL(3, C_PANEL), "ENTER / SPACE choose, ESC close");
    if ((mousey == oky) && (mousex >= okx) && (mousex < okx + 8)) okhit = 1;
    fliptoscreen();
    getkey();
    if (win_quitted) break;

    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      if (okhit) break;
      if (hit >= -2)
      {
        sel = hit;
        rawkey = KEY_ENTER;
      }
    }
    if ((rawkey == KEY_ESC) || ((rawkey == KEY_ENTER) && (sel == cur) && (!(mouseb & MOUSEB_LEFT)))) break;
    switch (rawkey)
    {
      case KEY_UP: sel--; break;
      case KEY_DOWN: sel++; break;
      case KEY_ENTER:
      case KEY_SPACE:
      midi_allnotesoff();
      if (sel == -2) midion = 0;
      else
      {
        midion = 1;
        if (sel == -1) mididevice[0] = 0;
        else if (sel < n) snprintf(mididevice, MAX_PATHNAME, "%s", midi_devicename(sel));
      }
      reopen();
      break;
    }
    key = rawkey = 0;
  }
  key = rawkey = 0;
}

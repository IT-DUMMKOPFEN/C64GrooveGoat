//
// C64 GrooveGoat packer/relocator & exporter
// (based on the GoatTracker v2 packer/relocator)
//
// 1 SID:  original GoatTracker playroutine (player.s / altplayer.s)
// 2-8 SIDs: multi-SID playroutine (mplayer.s). Channel variables are indexed
//         with X = sid * 32 + voice * 7, so SID registers can be written with
//         "sta SIDBASE+reg,x" when the SIDs are $20 apart. Other layouts use
//         a register buffer ("ghost registers") copied to each SID per frame.
//
// Output formats: SID (PSID v2 for 1 SID, v3 for 2 SIDs, v5 for 4/8 SIDs),
// PRG (music only, or runnable with the GrooveGoat player screen), BIN.
//

#define GRELOC_C

#include "goattrk2.h"
#include "membuf.h"
#include "parse.h"
#include "gres.h"

char *playeroptname[] =
{
  "Buffered SID-writes",
  "Sound effect support",
  "Volume change support",
  "Store author-info",
  "Use zeropage ghostregs",
  "Disable optimization",
  "Full SID buffering"
};

char *tableleftname[] = {
  "mt_wavetbl",
  "mt_pulsetimetbl",
  "mt_filttimetbl",
  "mt_speedlefttbl"};

char *tablerightname[] = {
  "mt_notetbl",
  "mt_pulsespdtbl",
  "mt_filtspdtbl",
  "mt_speedrighttbl"};

unsigned char chnused[MAX_CHN];
unsigned char pattused[MAX_PATT];
unsigned char pattmap[MAX_PATT];
unsigned char instrused[MAX_INSTR];
unsigned char instrmap[MAX_INSTR];
unsigned char tableused[MAX_TABLES][MAX_TABLELEN+1];
unsigned char tablemap[MAX_TABLES][MAX_TABLELEN+1];
int pattoffset[MAX_PATT];
int pattsize[MAX_PATT];
int songoffset[MAX_SONGS][MAX_CHN];
int songsize[MAX_SONGS][MAX_CHN];
int tableerror;
int channels;
int fixedparams;
int simplepulse;
int firstnote;
int lastnote;
int patternlastnote;
int nofilter;
int nofiltermod;
int nopulse;
int nopulsemod;
int nowavedelay;
int norepeat;
int notrans;
int noportamento;
int notoneporta;
int novib;
int noinsvib;
int nosetad;
int nosetsr;
int nosetwave;
int nosetwaveptr;
int nosetpulseptr;
int nosetfiltptr;
int nosetfiltcutoff;
int nosetfiltctrl;
int nosetmastervol;
int nofunktempo;
int noglobaltempo;
int nochanneltempo;
int nogate;
int noeffects;
int nowavecmd;
int nofirstwavecmd;
int nocalculatedspeed;
int nonormalspeed;
int nozerospeed;

struct membuf src = STATIC_MEMBUF_INIT;
struct membuf dest = STATIC_MEMBUF_INIT;

extern unsigned prgfrontend;

// Export settings & results
typedef struct
{
  int format;
  int base;
  int zp;
  unsigned addr[MAX_SIDS];
  int ghost;          // multi-SID: use register buffer instead of direct writes
  int psidversion;
  int v5config;
  int v5start;
} EXPORTSETTINGS;

typedef struct
{
  unsigned char *data;
  int size;
  int songs;
  int playersize;
  int songtblsize, songdatasize, patttblsize, pattdatasize, instrsize, tblsize;
  int ghostaddr;
} PACKRESULT;

static char errortext[256];
static unsigned exportaddr[MAX_SIDS];

unsigned sidaddr_export(int s)
{
  return exportaddr[s];
}
static int songlengths[MAX_SONGS];

// PSID v5 multi-SID address layouts
static const int v5stdstart[7] = {0xd420, 0xd440, 0xd480, 0xd500, 0xd420, 0xd420, 0xd500};
static const int v5step1[7] = {0x20, 0x40, 0x80, 0x100, 0x20, 0x20, 0x100};
static const int v5step2[7] = {0x20, 0x40, 0x80, 0x100, 0x60, 0xe0, 0x300};
static const int v5max[7] = {15, 15, 12, 6, 15, 12, 4};
static const char *v5names[7] = {
  "+$20 steps   ($D400 $D420 $D440 ...)",
  "+$40 steps   ($D400 $D440 $D480 ...)",
  "+$80 steps   ($D400 $D480 $D500 ...)",
  "+$100 steps  ($D400 $D500 $D600 ...)",
  "+$20/+$60    ($D400 $D420 $D480 ...)",
  "+$20/+$E0    ($D400 $D420 $D500 ...)",
  "+$100/+$300  ($D400 $D500 $DE00 ...)"};

static int validsidaddr(int a)
{
  if (a & 0x1f) return 0;
  if ((a >= 0xd420) && (a <= 0xd7e0)) return 1;
  if ((a >= 0xde00) && (a <= 0xdfe0)) return 1;
  return 0;
}

// Generate SID addresses of a PSID v5 layout. Returns 0 if invalid.
static int v5addresses(int config, int start, int count, unsigned *out)
{
  int k;
  out[0] = 0xd400;
  if (count < 2) return 1;
  if (count > v5max[config]) return 0;
  out[1] = start ? start : v5stdstart[config];
  if (!validsidaddr(out[1])) return 0;
  for (k = 2; k < count; k++)
  {
    int step = ((k - 1) & 1) ? v5step2[config] : v5step1[config];
    int a = out[k - 1] + step;
    if ((a > 0xd7e0) && (a < 0xde00)) a = 0xde00;
    if (a > 0xdfe0) return 0;
    out[k] = a;
  }
  for (k = 1; k < count; k++)
  {
    int j;
    if (!validsidaddr(out[k])) return 0;
    for (j = 0; j < k; j++)
      if (out[j] == out[k]) return 0;
  }
  return 1;
}

// Can the SIDs be written directly ("sta SIDBASE+reg,x")?
static int directlayout(const unsigned *addr, int n)
{
  int c;
  for (c = 1; c < n; c++)
    if (addr[c] != addr[0] + c * 0x20) return 0;
  return 1;
}

static void seterror(const char *msg)
{
  strncpy(errortext, msg, sizeof errortext - 1);
  errortext[sizeof errortext - 1] = 0;
}

// ---------------------------------------------------------------------------
// Analysis of the song data (as in GoatTracker)
// ---------------------------------------------------------------------------

static int songs;
static int patterns;
static int instruments;
static int numlegato, numnohr, numnormal;
static int transuprange, transdownrange;

static int analysesong(void)
{
  int c, d, e;
  int tableerrortype = TYPE_NONE;
  int tableerrorcause = CAUSE_NONE;
  int tableerrorsource1 = 0;
  int tableerrorsource2 = 0;
  int freenormal, freenohr, freelegato;
  int songdatasize = 0;

  channels = numchannels;
  fixedparams = 1;
  simplepulse = 1;
  firstnote = MAX_NOTES-1;
  lastnote = 0;
  patternlastnote = 0;
  noeffects = 1;
  nogate = 1;
  nofilter = 1;
  nofiltermod = 1;
  nopulse = 1;
  nopulsemod = 1;
  nowavedelay = 1;
  nowavecmd = 1;
  norepeat = 1;
  notrans = 1;
  noportamento = 1;
  notoneporta = 1;
  novib = 1;
  noinsvib = 1;
  nosetad = 1;
  nosetsr = 1;
  nosetwave = 1;
  nosetwaveptr = 1;
  nosetpulseptr = 1;
  nosetfiltptr = 1;
  nosetfiltcutoff = 1;
  nosetfiltctrl = 1;
  nosetmastervol = 1;
  nofunktempo = 1;
  noglobaltempo = 1;
  nochanneltempo = 1;
  nofirstwavecmd = 1;
  nocalculatedspeed = 1;
  nonormalspeed = 1;
  nozerospeed = 1;
  songs = 0;
  patterns = 0;
  instruments = 0;
  numlegato = numnohr = numnormal = 0;
  transuprange = transdownrange = 0;

  memset(pattused, 0, sizeof pattused);
  memset(instrused, 0, sizeof instrused);
  memset(chnused, 0, sizeof chnused);
  memset(tableused, 0, sizeof tableused);
  memset(tablemap, 0, sizeof tablemap);
  tableerror = 0;

  // Process song-orderlists
  countpatternlengths();
  for (c = 0; c < MAX_SONGS; c++)
  {
    if (songvalid(c))
    {
      // Subtunes must be consecutive for the playroutine
      if (c != songs)
      {
        sprintf(errortext, "SUBTUNE %02X IS EMPTY BUT FOLLOWED BY OTHER SUBTUNES!", songs);
        return 0;
      }
      for (d = 0; d < numchannels; d++)
      {
        songdatasize += songlen[c][d]+2;
        for (e = 0; e < songlen[c][d]; e++)
        {
          if (songorder[c][d][e] < REPEAT)
          {
            int f;
            int num = songorder[c][d][e];

            pattused[num] = 1;
            for (f = 0; f < pattlen[num]; f++)
            {
              if ((pattern[num][f*4] != REST) || (pattern[num][f*4+1]) || (pattern[num][f*4+2]))
                chnused[d] = 1;
            }
          }
          else
          {
            if (songorder[c][d][e] >= TRANSDOWN)
            {
              notrans = 0;
              if (songorder[c][d][e] < TRANSUP)
              {
                int newtransdownrange = -(songorder[c][d][e] - TRANSUP);
                if (newtransdownrange > transdownrange) transdownrange = newtransdownrange;
              }
              else
              {
                int newtransuprange = songorder[c][d][e] - TRANSUP;
                if (newtransuprange > transuprange) transuprange = newtransuprange;
              }
            }
            else norepeat = 0;
          }
        }
        if (songorder[c][d][songlen[c][d]+1] >= songlen[c][d])
        {
          sprintf(errortext, "ILLEGAL SONG RESTART POSITION! (SUBTUNE %02X, CHANNEL %d.%d)", c, d/3+1, d%3+1);
          return 0;
        }
      }
      songs++;
    }
  }

  // Optimize amount of used channels (single SID only)
  if (numsids == 1)
  {
    if (!chnused[2])
      channels = 2;
    if ((!chnused[1]) && (!chnused[2]))
      channels = 1;
  }

  if (!songs)
  {
    seterror("NO SONGS, NO DATA TO SAVE!");
    return 0;
  }
  if (songs * numchannels > 256)
  {
    sprintf(errortext, "TOO MANY SUBTUNES FOR %d SIDS (MAXIMUM IS %d)", numsids, 256 / numchannels);
    return 0;
  }

  // Build the pattern-mapping
  // Instrument 1 is always used
  instrused[1] = 1;
  for (c = 0; c < MAX_PATT; c++)
  {
    if (pattused[c])
    {
      pattmap[c] = patterns;
      patterns++;

      // See which instruments/tablecommands are used
      for (d = 0; d < pattlen[c]; d++)
      {
        tableerror = 0;

        if ((pattern[c][d*4] == KEYOFF) || (pattern[c][d*4] == KEYON))
          nogate = 0;
        if (pattern[c][d*4+1])
          instrused[pattern[c][d*4+1]] = 1;
        if (pattern[c][d*4+2])
          noeffects = 0;
        if ((pattern[c][d*4+2] >= CMD_SETWAVEPTR) && (pattern[c][d*4+2] <= CMD_SETFILTERPTR))
          exectable(pattern[c][d*4+2] - CMD_SETWAVEPTR, pattern[c][d*4+3]);
        if ((pattern[c][d*4+2] >= CMD_PORTAUP) && (pattern[c][d*4+2] <= CMD_VIBRATO))
        {
          exectable(STBL, pattern[c][d*4+3]);
          calcspeedtest(pattern[c][d*4+3]);
        }
        if (pattern[c][d*4+2] == CMD_FUNKTEMPO)
          exectable(STBL, pattern[c][d*4+3]);
        if (pattern[c][d*4+2] == CMD_FUNKTEMPO)
        {
          nofunktempo = 0;
          noglobaltempo = 0;
        }
        if ((pattern[c][d*4+2] == CMD_SETTEMPO) && ((pattern[c][d*4+3] & 0x7f) < 3)) nofunktempo = 0;

        // See, which are the highest/lowest notes used
        if ((pattern[c][d*4] >= FIRSTNOTE) && (pattern[c][d*4] <= LASTNOTE))
        {
          int newfirstnote = pattern[c][d*4] - FIRSTNOTE - transdownrange;
          int newlastnote = pattern[c][d*4] - FIRSTNOTE + transuprange;
          if (newfirstnote < 0) newfirstnote = 0;
          if (newlastnote > MAX_NOTES-1) newlastnote = MAX_NOTES-1;

          if (newfirstnote < firstnote) firstnote = newfirstnote;
          if (newlastnote > lastnote)
          {
            patternlastnote = newlastnote;
            lastnote = newlastnote;
          }
          if (newfirstnote > lastnote)
          {
            patternlastnote = newfirstnote;
            lastnote = newfirstnote;
          }
        }
        if ((tableerror) && (!tableerrortype))
        {
          tableerrortype = tableerror;
          tableerrorcause = CAUSE_PATTERN;
          tableerrorsource1 = c;
          tableerrorsource2 = d;
        }
      }
    }
  }

  // Count amount of normal, nohr, and legato instruments
  // Also see if special first wave parameters are used
  for (c = 0; c < MAX_INSTR; c++)
  {
    if (instrused[c])
    {
      if (instr[c].gatetimer & 0x40) numlegato++;
      else
      {
        if (instr[c].gatetimer & 0x80) numnohr++;
        else numnormal++;
      }
      if ((!instr[c].firstwave) || (instr[c].firstwave >= 0xfe))
        nofirstwavecmd = 0;
    }
  }
  freenormal = 1;
  freenohr = freenormal + numnormal;
  freelegato = freenohr + numnohr;

  // Build the instrument-mapping
  for (c = 0; c < MAX_INSTR; c++)
  {
    if (instrused[c])
    {
      if (instr[c].gatetimer & 0x40) instrmap[c] = freelegato++;
      else
      {
        if (instr[c].gatetimer & 0x80) instrmap[c] = freenohr++;
        else instrmap[c] = freenormal++;
      }
      instruments++;
      for (d = 0; d < MAX_TABLES; d++)
      {
        tableerror = 0;
        exectable(d, instr[c].ptr[d]);
        if (d == STBL) calcspeedtest(instr[c].ptr[d]);
        if ((tableerror) && (!tableerrortype))
        {
          tableerrortype = tableerror;
          tableerrorcause = CAUSE_INSTRUMENT;
          tableerrorsource1 = c;
          tableerrorsource2 = d;
        }
      }
    }
  }

  // Execute tableprograms invoked from wavetable commands
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if (tableused[WTBL][c+1])
    {
      if ((ltable[WTBL][c] >= WAVECMD) && (ltable[WTBL][c] <= WAVELASTCMD))
      {
        d = -1;
        tableerror = 0;

        switch(ltable[WTBL][c] - WAVECMD)
        {
          case CMD_PORTAUP:
          case CMD_PORTADOWN:
          case CMD_TONEPORTA:
          case CMD_VIBRATO:
          d = STBL;
          calcspeedtest(rtable[WTBL][c]);
          break;

          case CMD_SETPULSEPTR:
          d = PTBL;
          nopulse = 0;
          break;

          case CMD_SETFILTERPTR:
          d = FTBL;
          nofilter = 0;
          break;

          case CMD_DONOTHING:
          case CMD_SETWAVEPTR:
          case CMD_FUNKTEMPO:
          sprintf(errortext, "ILLEGAL WAVETABLE COMMAND (ROW %02X, COMMAND %X)", c+1, ltable[WTBL][c] - WAVECMD);
          return 0;
        }

        if (d != -1) exectable(d, rtable[WTBL][c]);

        if ((tableerror) && (!tableerrortype))
        {
          tableerrortype = tableerror;
          tableerrorcause = CAUSE_WAVECMD;
          tableerrorsource1 = c+1;
          tableerrorsource2 = d;
        }
      }
    }
  }

  // Build the table-mapping
  for (c = 0; c < MAX_TABLES; c++)
  {
    int e = 1;
    for (d = 0; d < MAX_TABLELEN; d++)
    {
      if (tableused[c][d+1])
      {
        tablemap[c][d+1] = e;
        e++;
      }
    }
  }

  // Check for table errors
  if (tableerrorcause)
  {
    switch(tableerrortype)
    {
      case TYPE_JUMP:
      sprintf(errortext, "TABLE POINTER POINTS TO A JUMP! ");
      break;

      case TYPE_OVERFLOW:
      sprintf(errortext, "TABLE EXECUTION OVERFLOWS! ");
      break;

      default:
      errortext[0] = 0;
      break;
    }
    switch (tableerrorcause)
    {
      case CAUSE_PATTERN:
      sprintf(errortext + strlen(errortext), "(PATTERN %02X, ROW %02d)", tableerrorsource1, tableerrorsource2);
      break;

      case CAUSE_WAVECMD:
      sprintf(errortext + strlen(errortext), "WAVETABLE CMD (ROW %02X, ", tableerrorsource1);
      goto TABLETYPE;

      case CAUSE_INSTRUMENT:
      sprintf(errortext + strlen(errortext), "(INSTRUMENT %02X, ", tableerrorsource1);
      TABLETYPE:
      switch (tableerrorsource2)
      {
        case WTBL: strcat(errortext, "WAVE"); break;
        case PTBL: strcat(errortext, "PULSE"); break;
        case FTBL: strcat(errortext, "FILTER"); break;
      }
      strcat(errortext, ")");
      break;
    }
    return 0;
  }

  // Find duplicate ranges in tables
  for (c = 0; c < MAX_TABLES; c++)
    findtableduplicates(c);

  (void)songdatasize;
  return 1;
}

// ---------------------------------------------------------------------------
// Source generation and assembly
// ---------------------------------------------------------------------------

static int insertresource(const char *name)
{
  int len = 0;
  const unsigned char *data = res_getfile(name, &len);
  if (!data) return 0;
  membuf_append(&src, data, len);
  inserttext("\n");
  return 1;
}

// Insert a player source with placeholder lines replaced by generated code
static int insertplayer(const char *name, int ghost, unsigned *addr)
{
  int len = 0;
  const char *data = (const char *)res_getfile(name, &len);
  const char *p, *end;
  char line[256];
  int c;

  if (!data) return 0;
  p = data;
  end = data + len;
  while (p < end)
  {
    const char *e = p;
    int l;
    while ((e < end) && (*e != '\n')) e++;
    l = e - p;
    if (l > 250) l = 250;
    memcpy(line, p, l);
    line[l] = 0;
    p = e + 1;

    if (strstr(line, ";@@GHOSTCOPY@@"))
    {
      if (ghost)
      {
        int s;
        for (s = 0; s < numsids; s++)
        {
          char buf[200];
          if (editorInfo.adparam >= 0xf000)
          {
            sprintf(buf, "                ldx #$00\nmt_copyregs%d:  lda ghostregs+%d,x\n                sta $%04x,x\n                inx\n                cpx #25\n                bne mt_copyregs%d\n", s, s * 32, addr[s], s);
          }
          else
          {
            sprintf(buf, "                ldx #24\nmt_copyregs%d:  lda ghostregs+%d,x\n                sta $%04x,x\n                dex\n                bpl mt_copyregs%d\n", s, s * 32, addr[s], s);
          }
          inserttext(buf);
        }
      }
      continue;
    }
    if (strstr(line, ";@@FILTEREXEC@@"))
    {
      int s;
      for (s = 0; s < numsids; s++)
      {
        char buf[100];
        sprintf(buf, "                ldx #$%02x\n                jsr mt_filtexec\n", s * 32);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@CHANNELEXEC@@"))
    {
      for (c = 0; c < numchannels; c++)
      {
        char buf[100];
        if (c < numchannels - 1)
          sprintf(buf, "                ldx #$%02x\n                jsr mt_execchn\n", (c / 3) * 32 + (c % 3) * 7);
        else
          sprintf(buf, "                ldx #$%02x\n                jmp mt_execchn\n", (c / 3) * 32 + (c % 3) * 7);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@CHANNELINIT@@"))
    {
      for (c = 0; c < numchannels; c++)
      {
        char buf[100];
        sprintf(buf, "                ldx #$%02x\n                jsr mt_initchn\n", (c / 3) * 32 + (c % 3) * 7);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@FILTERINIT@@"))
    {
      int s;
      for (s = 0; s < numsids; s++)
      {
        char buf[100];
        sprintf(buf, "                sta SIDBASE+$%02x\n", s * 32 + 0x15);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@SETGLOBALTEMPO@@"))
    {
      for (c = 0; c < numchannels; c++)
      {
        char buf[100];
        sprintf(buf, "                sta mt_chntempo+$%02x\n", (c / 3) * 32 + (c % 3) * 7);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@SETALLVOLUME@@"))
    {
      int s;
      for (s = 0; s < numsids; s++)
      {
        char buf[100];
        sprintf(buf, "                sta mt_masterfader+$%02x\n", s * 32);
        inserttext(buf);
      }
      continue;
    }
    if (strstr(line, ";@@INITMUL@@"))
    {
      char buf[300];
      // A = subtune number, compute A * NUMCHANNELS
      sprintf(buf, "                tay\n                lda #$00\nmt_initmul:     dey\n                bmi mt_initmuldone\n                clc\n                adc #%d\n                jmp mt_initmul\nmt_initmuldone:\n", numchannels);
      inserttext(buf);
      continue;
    }
    if (strstr(line, ";@@VARIABLES@@"))
    {
      // Channel variable blocks, numsids * 32 bytes each
      static const char *blocks[5] = {"mt_chnsongptr", "mt_chnfx", "mt_chnsongnum", "mt_chnvibtime", "mt_chnad"};
      int b;
      unsigned char data[MAX_SIDS * 32];
      for (b = 0; b < 5; b++)
      {
        memset(data, 0, sizeof data);
        if (b == 2)
        {
          for (c = 0; c < numchannels; c++)
          {
            int x = (c / 3) * 32 + (c % 3) * 7;
            data[x + 0] = c;      // songnum
            data[x + 5] = 1;      // instrument
            data[x + 6] = 0xfe;   // gate
          }
          for (c = 0; c < numsids; c++)
            data[c * 32 + 21] = 0x0f; // master volume
        }
        insertlabel(blocks[b]);
        insertbytes(data, numsids * 32);
      }
      continue;
    }
    inserttext(line);
    inserttext("\n");
  }
  return 1;
}

static int packmusic(EXPORTSETTINGS *es, PACKRESULT *res)
{
  int c, d, e;
  int songdatasize = 0;
  int songtblsize = 0;
  int pattdatasize = 0;
  int patttblsize = 0;
  int instrsize = 0;
  int wavetblsize = 0;
  int pulsetblsize = 0;
  int filttblsize = 0;
  int speedtblsize = 0;
  int packedsize;
  unsigned char patttemp[512];
  unsigned char *songwork = NULL;
  unsigned char *pattwork = NULL;
  unsigned char *instrwork = NULL;
  char *playername = "player.s";
  int multi = numsids > 1;
  int ok = 0;

  memset(res, 0, sizeof(PACKRESULT));
  membuf_free(&src);
  membuf_free(&dest);

  if (!analysesong()) return 0;

  // Disable optimizations if necessary
  if (playerversion & PLAYER_NOOPTIMIZATION)
  {
    fixedparams = 0;
    if (!numlegato) numlegato++;

    simplepulse = 0;
    firstnote = 0;
    lastnote = MAX_NOTES-1;
    nogate = 0;
    noeffects = 0;
    nofilter = 0;
    nofiltermod = 0;
    nopulse = 0;
    nopulsemod = 0;
    nowavedelay = 0;
    nowavecmd = 0;
    norepeat = 0;
    notrans = 0;
    noportamento = 0;
    notoneporta = 0;
    novib = 0;
    noinsvib = 0;
    nosetad = 0;
    nosetsr = 0;
    nosetwave = 0;
    nosetwaveptr = 0;
    nosetpulseptr = 0;
    nosetfiltptr = 0;
    nosetfiltcutoff = 0;
    nosetfiltctrl = 0;
    nosetmastervol = 0;
    nofunktempo = 0;
    noglobaltempo = 0;
    nochanneltempo = 0;
    nofirstwavecmd = 0;
    nocalculatedspeed = 0;
    nonormalspeed = 0;
    nozerospeed = 0;
  }

  // Multi-SID player does not support zeropage ghostregs
  if (multi) playerversion &= ~PLAYER_ZPGHOSTREGS;

  // Make sure buffering is used if it is needed
  if ((playerversion & PLAYER_SOUNDEFFECTS) || (playerversion & PLAYER_ZPGHOSTREGS) || (playerversion & PLAYER_FULLBUFFERED))
    playerversion |= PLAYER_BUFFERED;

  // Sound effect or ghostreg players always use full 3 channels
  if ((playerversion & PLAYER_SOUNDEFFECTS) || (playerversion & PLAYER_FULLBUFFERED) || (playerversion & PLAYER_ZPGHOSTREGS))
  {
    if (!multi) channels = 3;
  }

  // Allocate memory for song-orderlists
  songtblsize = songs*numchannels*2;
  for (c = 0; c < songs; c++)
    for (d = 0; d < numchannels; d++)
      songdatasize += songlen[c][d] + 2;
  songwork = (unsigned char*)malloc(songdatasize + 16);
  if (!songwork)
  {
    seterror("OUT OF MEMORY IN PACKER/RELOCATOR!");
    goto PRCLEANUP;
  }

  // Generate songorderlists & songtable
  songdatasize = 0;
  for (c = 0; c < songs; c++)
  {
    for (d = 0; d < numchannels; d++)
    {
      songoffset[c][d] = songdatasize;
      songsize[c][d] = songlen[c][d] + 2;

      for (e = 0; e < songlen[c][d]; e++)
      {
        // Pattern
        if (songorder[c][d][e] < REPEAT)
          songwork[songdatasize++] = pattmap[songorder[c][d][e]];
        else
        {
          // Transpose
          if (songorder[c][d][e] >= TRANSDOWN)
          {
            songwork[songdatasize++] = songorder[c][d][e];
          }
          // Repeat sequence: must be swapped
          else
          {
            // See that repeat amount is more than 1
            if (songorder[c][d][e] > REPEAT)
            {
              // Insanity check that a pattern indeed follows
              if (songorder[c][d][e+1] < REPEAT)
              {
                songwork[songdatasize++] = pattmap[songorder[c][d][e+1]];
                songwork[songdatasize++] = songorder[c][d][e];
                e++;
              }
              else
                songwork[songdatasize++] = songorder[c][d][e];
            }
          }
        }
      }
      // Endmark & repeat position
      songwork[songdatasize++] = songorder[c][d][e++];
      songwork[songdatasize++] = songorder[c][d][e++];
      songsize[c][d] = songdatasize - songoffset[c][d];
    }
  }

  // Calculate total size of patterns
  for (c = 0; c < MAX_PATT; c++)
  {
    if (pattused[c])
    {
      int result = packpattern(patttemp, pattern[c], pattlen[c]);

      if (result < 0)
      {
        sprintf(errortext, "PATTERN %02X IS TOO COMPLEX (OVER 256 BYTES PACKED)!", c);
        goto PRCLEANUP;
      }
      pattdatasize += result;
    }
  }

  patttblsize = patterns*2;
  pattwork = (unsigned char*)malloc(pattdatasize + 16);
  if (!pattwork)
  {
    seterror("OUT OF MEMORY IN PACKER/RELOCATOR!");
    goto PRCLEANUP;
  }

  // This time pack the patterns for real
  pattdatasize = 0;
  d = 0;
  for (c = 0; c < MAX_PATT; c++)
  {
    if (pattused[c])
    {
      pattoffset[d] = pattdatasize;
      pattsize[d] = packpattern(&pattwork[pattdatasize], pattern[c], pattlen[c]);
      pattdatasize += pattsize[d];
      d++;
    }
  }

  // Then process instruments
  instrsize = instruments*9;
  instrwork = (unsigned char*)malloc(instrsize + 16);
  if (!instrwork)
  {
    seterror("OUT OF MEMORY IN PACKER/RELOCATOR!");
    goto PRCLEANUP;
  }

  for (c = 1; c < MAX_INSTR; c++)
  {
    if (instrused[c])
    {
      d = instrmap[c] - 1;
      instrwork[d] = instr[c].ad;
      instrwork[d+instruments] = instr[c].sr;
      instrwork[d+instruments*2] = tablemap[WTBL][instr[c].ptr[WTBL]];
      instrwork[d+instruments*3] = tablemap[PTBL][instr[c].ptr[PTBL]];
      instrwork[d+instruments*4] = tablemap[FTBL][instr[c].ptr[FTBL]];
      if (instr[c].vibdelay)
      {
        instrwork[d+instruments*5] = tablemap[STBL][instr[c].ptr[STBL]];
        instrwork[d+instruments*6] = instr[c].vibdelay - 1;
      }
      else
      {
        instrwork[d+instruments*5] = 0;
        instrwork[d+instruments*6] = 0;
      }
      instrwork[d+instruments*7] = instr[c].gatetimer & 0x3f;
      instrwork[d+instruments*8] = instr[c].firstwave;

      if (instr[c].ptr[STBL])
      {
        novib = 0;
        noinsvib = 0;
      }
      if (instr[c].ptr[PTBL])
        nopulse = 0;
      if (instr[c].ptr[FTBL])
        nofilter = 0;

      // See if all instruments use same gatetimer & firstwave parameters
      if ((instr[c].gatetimer != instr[1].gatetimer) ||
          (instr[c].firstwave != instr[1].firstwave))
        fixedparams = 0;
      // or if special firstwave commands are in use
      if ((!instr[c].firstwave) || (instr[c].firstwave >= 0xfe))
        fixedparams = 0;
    }
  }

  // Disable sameparam optimization for multispeed stability
  if (editorInfo.multiplier > 1)
  {
    fixedparams = 0;
    numlegato++;
    numnohr++;
  }

  if (fixedparams) instrsize -= instruments*2;
  if (noinsvib) instrsize -= instruments*2;
  if (nopulse) instrsize -= instruments;
  if (nofilter) instrsize -= instruments;

  // Process tables
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if (tableused[WTBL][c+1])
    {
      wavetblsize += 2;
      if ((ltable[WTBL][c] >= WAVEDELAY) && (ltable[WTBL][c] <= WAVELASTDELAY)) nowavedelay = 0;
      if ((ltable[WTBL][c] >= WAVECMD) && (ltable[WTBL][c] <= WAVELASTCMD))
      {
        nowavecmd = 0;
        noeffects = 0;
        switch (ltable[WTBL][c] - WAVECMD)
        {
          case CMD_PORTAUP:
          case CMD_PORTADOWN:
          noportamento = 0;
          break;

          case CMD_TONEPORTA:
          notoneporta = 0;
          break;

          case CMD_VIBRATO:
          novib = 0;
          break;

          case CMD_SETAD:
          nosetad = 0;
          break;

          case CMD_SETSR:
          nosetsr = 0;
          break;

          case CMD_SETWAVE:
          nosetwave = 0;
          break;

          case CMD_SETPULSEPTR:
          nosetpulseptr = 0;
          break;

          case CMD_SETFILTERPTR:
          nosetfiltptr = 0;
          break;

          case CMD_SETFILTERCUTOFF:
          nosetfiltcutoff = 0;
          break;

          case CMD_SETFILTERCTRL:
          nosetfiltctrl = 0;
          break;

          case CMD_SETMASTERVOL:
          nosetmastervol = 0;
          break;
        }
      }
      if (ltable[WTBL][c] < WAVECMD)
      {
        if (rtable[WTBL][c] <= 0x80)
        {
          int newlastnote = rtable[WTBL][c] + patternlastnote;
          if (newlastnote > MAX_NOTES - 1) newlastnote = MAX_NOTES - 1;
          if (rtable[WTBL][c] >= 0x20) firstnote = 0;
          if (newlastnote > lastnote) lastnote = newlastnote;
        }
        else
        {
          int newfirstnote = rtable[WTBL][c] & 0x7f;
          int newlastnote = rtable[WTBL][c] & 0x7f;
          if (newlastnote > MAX_NOTES - 1) newlastnote = MAX_NOTES - 1;
          if (newfirstnote < firstnote) firstnote = newfirstnote;
          if (newlastnote > lastnote) lastnote = newlastnote;
        }
      }
    }
  }
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if (tableused[PTBL][c+1])
    {
      pulsetblsize += 2;
      if ((ltable[PTBL][c] >= 0x80) && (ltable[PTBL][c] != 0xff))
      {
        if (rtable[PTBL][c] & 0xf) simplepulse = 0;
      }
      if (ltable[PTBL][c] < 0x80)
      {
        nopulsemod = 0;
        if (rtable[PTBL][c] & 0xf) simplepulse = 0;
      }
    }
  }
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if (tableused[FTBL][c+1])
    {
      filttblsize += 2;
      if (ltable[FTBL][c] < 0x80) nofiltermod = 0;
    }
  }
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if (tableused[STBL][c+1]) speedtblsize += 2;
  }
  // Zero entry of speedtable
  if ((!novib) || (!nofunktempo) || (!noportamento) || (!notoneporta))
    speedtblsize += 2;

  if (nopulse) pulsetblsize = 0;
  if (nofilter) filttblsize = 0;

  // Validate frequencytable parameters
  if (lastnote < firstnote)
    lastnote = firstnote;
  if (firstnote < 0) firstnote = 0;
  if (!nocalculatedspeed)
    lastnote++; // Calculated speeds need the next frequency value
  if (lastnote > MAX_NOTES-1) lastnote = MAX_NOTES-1;
  // For sound effect support, always use the full table
  if (playerversion & PLAYER_SOUNDEFFECTS)
  {
    firstnote = 0;
    lastnote = MAX_NOTES-1;
  }

  // Assemble (multi-SID ghost layouts need two passes: the address of the
  // register buffer is only known after the first one)
  {
    int pass;
    int sidbase = es->addr[0];
    int ghost;

    // Full SID buffering in multi-SID mode = register buffer for all SIDs
    if (multi && (playerversion & PLAYER_FULLBUFFERED)) es->ghost = 1;
    ghost = multi && es->ghost;

    for (pass = 0; pass < 1; pass++)
    {
      membuf_free(&src);
      membuf_free(&dest);

      // Insert baseaddresses
      insertdefine("base", es->base);
      insertdefine("zpbase", es->zp);
      if (ghost)
        inserttext("SIDBASE          = ghostregs\n");
      else
        insertdefine("SIDBASE", sidbase);

      // Insert conditionals
      insertdefine("SOUNDSUPPORT", (playerversion & PLAYER_SOUNDEFFECTS) ? 1 : 0);
      insertdefine("VOLSUPPORT", (playerversion & PLAYER_VOLUME) ? 1 : 0);
      insertdefine("BUFFEREDWRITES", (playerversion & PLAYER_BUFFERED) ? 1 : 0);
      if (!multi)
      {
        insertdefine("GHOSTREGS", (playerversion & (PLAYER_ZPGHOSTREGS|PLAYER_FULLBUFFERED)) ? 1 : 0);
        insertdefine("ZPGHOSTREGS", (playerversion & PLAYER_ZPGHOSTREGS) ? 1 : 0);
      }
      else
      {
        insertdefine("GHOSTREGS", ghost ? 1 : 0);
        insertdefine("ZPGHOSTREGS", 0);
        insertdefine("NUMSIDS", numsids);
      }
      insertdefine("FIXEDPARAMS", fixedparams);
      insertdefine("SIMPLEPULSE", simplepulse);
      insertdefine("PULSEOPTIMIZATION", editorInfo.optimizepulse);
      insertdefine("REALTIMEOPTIMIZATION", editorInfo.optimizerealtime);
      insertdefine("NOAUTHORINFO", (playerversion & PLAYER_AUTHORINFO) ? 0 : 1);
      insertdefine("NOEFFECTS", noeffects);
      insertdefine("NOGATE", nogate);
      insertdefine("NOFILTER", nofilter);
      insertdefine("NOFILTERMOD", nofiltermod);
      insertdefine("NOPULSE", nopulse);
      insertdefine("NOPULSEMOD", nopulsemod);
      insertdefine("NOWAVEDELAY", nowavedelay);
      insertdefine("NOWAVECMD", nowavecmd);
      insertdefine("NOREPEAT", norepeat);
      insertdefine("NOTRANS", notrans);
      insertdefine("NOPORTAMENTO", noportamento);
      insertdefine("NOTONEPORTA", notoneporta);
      insertdefine("NOVIB", novib);
      insertdefine("NOINSTRVIB", noinsvib);
      insertdefine("NOSETAD", nosetad);
      insertdefine("NOSETSR", nosetsr);
      insertdefine("NOSETWAVE", nosetwave);
      insertdefine("NOSETWAVEPTR", nosetwaveptr);
      insertdefine("NOSETPULSEPTR", nosetpulseptr);
      insertdefine("NOSETFILTPTR", nosetfiltptr);
      insertdefine("NOSETFILTCTRL", nosetfiltctrl);
      insertdefine("NOSETFILTCUTOFF", nosetfiltcutoff);
      insertdefine("NOSETMASTERVOL", nosetmastervol);
      insertdefine("NOFUNKTEMPO", nofunktempo);
      insertdefine("NOGLOBALTEMPO", noglobaltempo);
      insertdefine("NOCHANNELTEMPO", nochanneltempo);
      insertdefine("NOFIRSTWAVECMD", nofirstwavecmd);
      insertdefine("NOCALCULATEDSPEED", nocalculatedspeed);
      insertdefine("NONORMALSPEED", nonormalspeed);
      insertdefine("NOZEROSPEED", nozerospeed);

      // Insert parameters
      insertdefine("NUMCHANNELS", multi ? numchannels : channels);
      insertdefine("NUMSONGS", songs);
      insertdefine("FIRSTNOTE", firstnote);
      insertdefine("FIRSTNOHRINSTR", numnormal + 1);
      insertdefine("FIRSTLEGATOINSTR", numnormal + numnohr + 1);
      insertdefine("NUMHRINSTR", numnormal);
      insertdefine("NUMNOHRINSTR", numnohr);
      insertdefine("NUMLEGATOINSTR", numlegato);
      insertdefine("ADPARAM", editorInfo.adparam >> 8);
      insertdefine("SRPARAM", editorInfo.adparam & 0xff);
      if ((instr[MAX_INSTR-1].ad >= 2) && (!(instr[MAX_INSTR-1].ptr[WTBL])))
        insertdefine("DEFAULTTEMPO", instr[MAX_INSTR-1].ad - 1);
      else
        insertdefine("DEFAULTTEMPO", editorInfo.multiplier ? (editorInfo.multiplier*6-1) : 5);

      // Fixed firstwave & gatetimer
      if (fixedparams)
      {
        insertdefine("FIRSTWAVEPARAM", instr[1].firstwave);
        insertdefine("GATETIMERPARAM", instr[1].gatetimer & 0x3f);
      }

      // Insert source code of player
      if (!multi)
      {
        if (editorInfo.adparam >= 0xf000)
          playername = "altplayer.s";
        if (!insertresource(playername))
        {
          seterror("COULD NOT OPEN PLAYROUTINE!");
          goto PRCLEANUP;
        }
        // Modify ghostregs to not be zeropage if needed
        if ((playerversion & PLAYER_FULLBUFFERED) && (playerversion & PLAYER_ZPGHOSTREGS) == 0)
        {
          int bufsize = membuf_get_size(&src);
          char* bufdata = (char*)membuf_get(&src);
          for (c = 0; c < bufsize; c++)
          {
            if (bufdata[c] == '<')
            {
              if (memcmp(bufdata + c + 1, "ghost", 5) == 0)
                bufdata[c] = ' ';
            }
          }
        }
      }
      else
      {
        insertdefine("ALTORDER", editorInfo.adparam >= 0xf000 ? 1 : 0);
        if (!insertplayer("mplayer.s", ghost, es->addr))
        {
          seterror("COULD NOT OPEN MULTI-SID PLAYROUTINE!");
          goto PRCLEANUP;
        }
      }

      // Insert frequencytable
      insertlabel("mt_freqtbllo");
      insertbytes(&freqtbllo[firstnote], lastnote-firstnote+1);
      insertlabel("mt_freqtblhi");
      insertbytes(&freqtblhi[firstnote], lastnote-firstnote+1);

      // Insert songtable
      insertlabel("mt_songtbllo");
      for (c = 0; c < songs*numchannels; c++)
      {
        sprintf(textbuffer, "mt_song%d", c);
        insertaddrlo(textbuffer);
      }
      insertlabel("mt_songtblhi");
      for (c = 0; c < songs*numchannels; c++)
      {
        sprintf(textbuffer, "mt_song%d", c);
        insertaddrhi(textbuffer);
      }

      // Insert patterntable
      insertlabel("mt_patttbllo");
      for (c = 0; c < patterns; c++)
      {
        sprintf(textbuffer, "mt_patt%d", c);
        insertaddrlo(textbuffer);
      }
      insertlabel("mt_patttblhi");
      for (c = 0; c < patterns; c++)
      {
        sprintf(textbuffer, "mt_patt%d", c);
        insertaddrhi(textbuffer);
      }

      // Insert instruments
      insertlabel("mt_insad");
      insertbytes(&instrwork[0], instruments);
      insertlabel("mt_inssr");
      insertbytes(&instrwork[instruments], instruments);
      insertlabel("mt_inswaveptr");
      insertbytes(&instrwork[instruments*2], instruments);
      if (!nopulse)
      {
        insertlabel("mt_inspulseptr");
        insertbytes(&instrwork[instruments*3], instruments);
      }
      if (!nofilter)
      {
        insertlabel("mt_insfiltptr");
        insertbytes(&instrwork[instruments*4], instruments);
      }
      if (!noinsvib)
      {
        insertlabel("mt_insvibparam");
        insertbytes(&instrwork[instruments*5], instruments);
        insertlabel("mt_insvibdelay");
        insertbytes(&instrwork[instruments*6], instruments);
      }
      if (!fixedparams)
      {
        insertlabel("mt_insgatetimer");
        insertbytes(&instrwork[instruments*7], instruments);
        insertlabel("mt_insfirstwave");
        insertbytes(&instrwork[instruments*8], instruments);
      }

      // Insert tables
      for (c = 0; c < MAX_TABLES; c++)
      {
        if ((c == PTBL) && (nopulse)) continue;
        if ((c == FTBL) && (nofilter)) continue;

        // Write table left side
        // Extra zero for speedtable
        if ((c == STBL) && ((!novib) || (!nofunktempo) || (!noportamento) || (!notoneporta))) insertbyte(0);
        // Table label
        insertlabel(tableleftname[c]);

        // Table data
        for (d = 0; d < MAX_TABLELEN; d++)
        {
          if (tableused[c][d+1])
          {
            switch (c)
            {
              // In wavetable, convert waveform values for the playroutine
              case WTBL:
              {
                unsigned char wave = ltable[c][d];
                if ((ltable[c][d] >= WAVESILENT) && (ltable[c][d] <= WAVELASTSILENT)) wave &= 0xf;
                if ((ltable[c][d] > WAVELASTDELAY) && (ltable[c][d] <= WAVELASTSILENT) && (!nowavedelay)) wave += 0x10;
                insertbyte(wave);
              }
              break;

              case PTBL:
              if ((simplepulse) && (ltable[c][d] != 0xff) && (ltable[c][d] > 0x80))
                insertbyte(0x80);
              else
                insertbyte(ltable[c][d]);
              break;

              // In filtertable, modify passband bits
              case FTBL:
              if ((ltable[c][d] != 0xff) && (ltable[c][d] > 0x80))
                insertbyte(((ltable[c][d] & 0x70) >> 1) | 0x80);
              else
                insertbyte(ltable[c][d]);
              break;

              default:
              insertbyte(ltable[c][d]);
              break;
            }
          }
        }

        // Write table right side, remapping jumps as necessary
        // Extra zero for speedtable
        if ((c == STBL) && ((!novib) || (!nofunktempo) || (!noportamento) || (!notoneporta))) insertbyte(0);
        // Table label
        insertlabel(tablerightname[c]);

        for (d = 0; d < MAX_TABLELEN; d++)
        {
          if (tableused[c][d+1])
          {
            if ((ltable[c][d] != 0xff) || (c == STBL))
            {
              switch(c)
              {
                case WTBL:
                if ((ltable[c][d] >= WAVECMD) && (ltable[c][d] <= WAVELASTCMD))
                {
                  // Remap table-referencing commands
                  switch (ltable[c][d] - WAVECMD)
                  {
                    case CMD_PORTAUP:
                    case CMD_PORTADOWN:
                    case CMD_TONEPORTA:
                    case CMD_VIBRATO:
                    insertbyte(tablemap[STBL][rtable[c][d]]);
                    break;

                    case CMD_SETPULSEPTR:
                    insertbyte(tablemap[PTBL][rtable[c][d]]);
                    break;

                    case CMD_SETFILTERPTR:
                    insertbyte(tablemap[FTBL][rtable[c][d]]);
                    break;

                    default:
                    insertbyte(rtable[c][d]);
                    break;
                  }
                }
                else
                {
                  // For normal notes, reverse all right side high bits
                  insertbyte(rtable[c][d] ^ 0x80);
                }
                break;

                case PTBL:
                if (simplepulse)
                {
                  if (ltable[c][d] >= 0x80)
                    insertbyte((ltable[c][d] & 0x0f) | (rtable[c][d] & 0xf0));
                  else
                  {
                    int pulsespeed = rtable[c][d] >> 4;
                    if (rtable[c][d] & 0x80)
                    {
                      pulsespeed |= 0xf0;
                      pulsespeed--;
                    }
                    pulsespeed = swapnybbles(pulsespeed);
                    insertbyte(pulsespeed);
                  }
                }
                else
                  insertbyte(rtable[c][d]);
                break;

                default:
                insertbyte(rtable[c][d]);
                break;
              }
            }
            else
              insertbyte(tablemap[c][rtable[c][d]]);
          }
        }
      }

      // Insert orderlists
      for (c = 0; c < songs; c++)
      {
        for (d = 0; d < numchannels; d++)
        {
          if ((!multi) && (d >= 3)) break;
          sprintf(textbuffer, "mt_song%d", c*numchannels+d);
          insertlabel(textbuffer);
          insertbytes(&songwork[songoffset[c][d]], songsize[c][d]);
        }
      }

      // Insert patterns
      for (c = 0; c < patterns; c++)
      {
        sprintf(textbuffer, "mt_patt%d", c);
        insertlabel(textbuffer);
        insertbytes(&pattwork[pattoffset[c]], pattsize[c]);
      }

      // SID register buffer (multi-SID with a non-$20 address layout)
      // is always the last thing in the data
      if (ghost)
      {
        unsigned char zero[MAX_SIDS * 32];
        memset(zero, 0, sizeof zero);
        insertlabel("ghostregs");
        insertbytes(zero, numsids * 32);
      }

      if (getenv("GG_DEBUGASM"))
      {
        FILE *handle = fopen(getenv("GG_DEBUGASM"), "wb");
        if (handle)
        {
          fwrite(membuf_get(&src), membuf_memlen(&src), 1, handle);
          fclose(handle);
        }
      }

      // Assemble
      if (assemble(&src, &dest))
      {
        seterror("ASSEMBLY OF THE PLAYROUTINE FAILED (INTERNAL ERROR)");
        goto PRCLEANUP;
      }

      packedsize = membuf_memlen(&dest);
      if (ghost)
      {
        // The register buffer is the last thing in the data
        res->ghostaddr = es->base + packedsize - numsids * 32;
      }
    }
  }

  if (es->base + packedsize > 0x10000)
  {
    sprintf(errortext, "THE MUSIC DOES NOT FIT INTO MEMORY ($%04X-$%04X)!", es->base, es->base + packedsize - 1);
    goto PRCLEANUP;
  }

  res->size = packedsize;
  res->data = (unsigned char *)malloc(packedsize);
  if (!res->data)
  {
    seterror("OUT OF MEMORY IN PACKER/RELOCATOR!");
    goto PRCLEANUP;
  }
  memcpy(res->data, membuf_get(&dest), packedsize);
  res->songs = songs;
  res->songtblsize = songtblsize;
  res->songdatasize = songdatasize;
  res->patttblsize = patttblsize;
  res->pattdatasize = pattdatasize;
  res->instrsize = instrsize;
  res->tblsize = wavetblsize+pulsetblsize+filttblsize+speedtblsize;
  res->playersize = packedsize - songtblsize - songdatasize - patttblsize - pattdatasize - instrsize - res->tblsize;

  // Copy author info
  if (playerversion & PLAYER_AUTHORINFO)
  {
    for (c = 0; c < 32; c++)
    {
      res->data[32+c] = authorname[c];
      // Convert 0 to space
      if (res->data[32+c] == 0) res->data[32+c] = 0x20;
    }
  }
  ok = 1;

  PRCLEANUP:
  membuf_free(&src);
  membuf_free(&dest);
  if (pattwork) free(pattwork);
  if (songwork) free(songwork);
  if (instrwork) free(instrwork);
  return ok;
}

// ---------------------------------------------------------------------------
// File writers
// ---------------------------------------------------------------------------

static void cia_speedvalue(unsigned *value)
{
  if (editorInfo.multiplier)
  {
    if (editorInfo.ntsc) *value = 0x42c6 / editorInfo.multiplier;
    else *value = 0x4cc7 / editorInfo.multiplier;
  }
  else
  {
    if (editorInfo.ntsc) *value = 0x42c6 * 2;
    else *value = 0x4cc7 * 2;
  }
}

// Copy a string into a 32 byte PSID field: Windows-1252, at most 31
// characters (v5 needs a terminator) and never empty
static void psidstring(unsigned char *dest, const char *s)
{
  int c;
  memset(dest, 0, 32);
  for (c = 0; (c < 31) && (s[c]); c++) dest[c] = s[c];
  if (!dest[0])
  {
    dest[0] = '<';
    dest[1] = '?';
    dest[2] = '>';
  }
}

static int writesidfile(FILE *handle, EXPORTSETTINGS *es, PACKRESULT *res)
{
  unsigned char header[0x7c];
  int multispeed = (editorInfo.multiplier > 1) || (!editorInfo.multiplier);
  int load = multispeed ? es->base - 10 : es->base;
  int init = load;
  int play = es->base + 3;
  unsigned char speedcode[] = {0xa2,0x00,0x8e,0x04,0xdc,0xa2,0x00,0x8e,0x05,0xdc};
  int version = 2;
  unsigned flags;
  int c;

  if (numsids == 2) version = 3;
  if (numsids > 2) version = 5;

  memset(header, 0, sizeof header);
  memcpy(header, "PSID", 4);
  header[4] = 0;
  header[5] = version;
  header[6] = 0;
  header[7] = 0x7c;
  // load address 0 = in data
  header[0x0a] = init >> 8;
  header[0x0b] = init & 0xff;
  header[0x0c] = play >> 8;
  header[0x0d] = play & 0xff;
  header[0x0e] = 0;
  header[0x0f] = res->songs;
  header[0x10] = 0;
  header[0x11] = 1;
  if ((editorInfo.ntsc) || (multispeed))
  {
    header[0x12] = header[0x13] = header[0x14] = header[0x15] = 0xff;
  }
  psidstring(header + 0x16, songname);
  psidstring(header + 0x36, authorname);
  psidstring(header + 0x56, copyrightname);
  if (version == 5)
  {
    // v5 metadata: name and author need a terminator, released may use 32 chars
    for (c = 0; (c < 32) && (copyrightname[c]); c++) header[0x56 + c] = copyrightname[c];
  }

  flags = editorInfo.ntsc ? 0x08 : 0x04;
  flags |= editorInfo.sidmodel ? 0x20 : 0x10;
  if (version == 3)
    flags |= editorInfo.sidmodel ? 0x80 : 0x40;
  header[0x76] = flags >> 8;
  header[0x77] = flags & 0xff;
  header[0x78] = 0;
  header[0x79] = 0;
  if (version == 3)
  {
    header[0x7a] = (es->addr[1] >> 4) & 0xff;
    header[0x7b] = 0;
  }
  if (version == 5)
  {
    header[0x7a] = (numsids & 0x0f) | (es->v5config << 4);
    header[0x7b] = es->v5start ? ((es->v5start >> 4) & 0xff) : 0;
  }
  fwrite(header, sizeof header, 1, handle);

  // C64 data with load address
  fwrite8(handle, load & 0xff);
  fwrite8(handle, load >> 8);
  if (multispeed)
  {
    unsigned speedvalue;
    cia_speedvalue(&speedvalue);
    speedcode[1] = speedvalue & 0xff;
    speedcode[6] = speedvalue >> 8;
    fwrite(speedcode, 10, 1, handle);
  }
  fwrite(res->data, res->size, 1, handle);
  return 1;
}

// ---------------------------------------------------------------------------
// Runnable PRG with the C64 GrooveGoat player screen
// ---------------------------------------------------------------------------

static unsigned char petscii_screencode(unsigned char c)
{
  // Uppercase/graphics character set
  if ((c >= 'a') && (c <= 'z')) c = c - 'a' + 'A';
  if ((c >= 0xe0) && (c <= 0xfe)) c -= 0x20;  // Latin-1 lowercase -> uppercase
  switch (c)
  {
    case 0xc4: case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc5: c = 'A'; break;
    case 0xd6: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd8: c = 'O'; break;
    case 0xdc: case 0xd9: case 0xda: case 0xdb: c = 'U'; break;
    case 0xc9: case 0xc8: case 0xca: case 0xcb: c = 'E'; break;
    case 0xdf: c = 'S'; break;
  }
  if ((c >= 'A') && (c <= 'Z')) return c - 'A' + 1;
  if (c == '@') return 0;
  if (c == '[') return 27;
  if (c == ']') return 29;
  if ((c >= 32) && (c <= 63)) return c;
  if (c == '_') return 0x64;
  return 32;
}

typedef struct
{
  unsigned char scr[1000];
  unsigned char col[1000];
} C64SCREEN;

static void c64text(C64SCREEN *s, int x, int y, int color, const char *text)
{
  // Keep the text inside the frame (columns 1-38)
  while ((*text) && (x < 39))
  {
    if ((x >= 0) && (y >= 0) && (y < 25))
    {
      s->scr[y * 40 + x] = petscii_screencode((unsigned char)*text);
      s->col[y * 40 + x] = color;
    }
    x++;
    text++;
  }
}

static void c64char(C64SCREEN *s, int x, int y, int color, unsigned char code)
{
  if ((x < 0) || (x >= 40) || (y < 0) || (y >= 25)) return;
  s->scr[y * 40 + x] = code;
  s->col[y * 40 + x] = color;
}

#define PRG_BORDER 14
#define PRG_BG 6
#define PRG_TEXT 14
#define PRG_HILITE 1
#define PRG_VALUE 3

// Screen positions used by the frontend code (updated at runtime)
#define POS_SUBTUNE (13 * 40 + 13)
#define POS_LENGTH (13 * 40 + 31)
#define POS_TIME (14 * 40 + 13)
#define POS_RAINBOW (7 * 40 + 1)
// Logo sprites (x/y expanded, 48x42 pixels) at column 2, row 1
#define LOGOX (24 + 2 * 8)
#define LOGOY (50 + 1 * 8 - 2)

static void buildprgscreen(C64SCREEN *s, PACKRESULT *res)
{
  int c;
  char buf[64];
  static const int rainbow[5] = {2, 8, 7, 5, 14};

  memset(s->scr, 32, 1000);
  memset(s->col, PRG_TEXT, 1000);

  // Frame
  for (c = 1; c < 39; c++)
  {
    c64char(s, c, 0, PRG_TEXT, 0x40);
    c64char(s, c, 24, PRG_TEXT, 0x40);
  }
  for (c = 1; c < 24; c++)
  {
    c64char(s, 0, c, PRG_TEXT, 0x5d);
    c64char(s, 39, c, PRG_TEXT, 0x5d);
  }
  c64char(s, 0, 0, PRG_TEXT, 0x70);
  c64char(s, 39, 0, PRG_TEXT, 0x6e);
  c64char(s, 0, 24, PRG_TEXT, 0x6d);
  c64char(s, 39, 24, PRG_TEXT, 0x7d);

  c64text(s, 10, 2, PRG_HILITE, "C64 GROOVEGOAT");
  c64text(s, 10, 3, PRG_VALUE, "VERSION " PROGRAMVERSION);
  c64text(s, 10, 5, PRG_TEXT, "BY " PROGRAMAUTHOR);

  // C64 rainbow stripes
  for (c = 0; c < 38; c++)
    c64char(s, 1 + c, 7, rainbow[(c * 5) / 38], 0xa0);

  c64text(s, 2, 9, PRG_TEXT, "NAME:");
  c64text(s, 13, 9, PRG_VALUE, songname);
  c64text(s, 2, 10, PRG_TEXT, "AUTHOR:");
  c64text(s, 13, 10, PRG_VALUE, authorname);
  c64text(s, 2, 11, PRG_TEXT, "RELEASED:");
  c64text(s, 13, 11, PRG_VALUE, copyrightname);

  c64text(s, 2, 13, PRG_TEXT, "SUBTUNE:");
  sprintf(buf, "01/%02d", res->songs);
  c64text(s, 13, 13, PRG_HILITE, buf);
  c64text(s, 23, 13, PRG_TEXT, "LENGTH:");
  c64text(s, 31, 13, PRG_HILITE, "00:00");
  c64text(s, 2, 14, PRG_TEXT, "TIME:");
  c64text(s, 13, 14, PRG_HILITE, "00:00");

  c64text(s, 2, 16, PRG_TEXT, "SIDS:");
  sprintf(buf, "%d X %s  %s  %s", numsids, editorInfo.sidmodel ? "8580" : "6581", editorInfo.ntsc ? "NTSC" : "PAL",
    editorInfo.multiplier ? (editorInfo.multiplier == 1 ? "1X SPEED" : "MULTISPEED") : "25HZ");
  if (editorInfo.multiplier > 1)
    sprintf(buf, "%d X %s  %s  %dX SPEED", numsids, editorInfo.sidmodel ? "8580" : "6581", editorInfo.ntsc ? "NTSC" : "PAL", editorInfo.multiplier);
  c64text(s, 13, 16, PRG_VALUE, buf);
  c64text(s, 2, 17, PRG_TEXT, numsids > 1 ? "ADDRESSES:" : "ADDRESS:");
  for (c = 0; c < numsids; c++)
  {
    sprintf(buf, "$%04X", numsids > 1 ? sidaddr_export(c) : sidaddr_export(0));
    c64text(s, 13 + (c % 4) * 6, 17 + c / 4, PRG_VALUE, buf);
  }
  c64text(s, 2, 20, PRG_TEXT, "VOICES:");
  sprintf(buf, "%d", numchannels);
  c64text(s, 13, 20, PRG_VALUE, buf);

  c64text(s, 2, 22, PRG_TEXT, "+/- OR 1-9: SUBTUNE   SPACE: RESTART");
}

// RLE: $FF count value; everything else literal
static int rlepack(const unsigned char *in, int len, unsigned char *out)
{
  int i = 0, o = 0;
  while (i < len)
  {
    int run = 1;
    while ((i + run < len) && (in[i + run] == in[i]) && (run < 255)) run++;
    if ((run >= 4) || (in[i] == 0xff))
    {
      out[o++] = 0xff;
      out[o++] = run;
      out[o++] = in[i];
      i += run;
    }
    else
    {
      out[o++] = in[i++];
    }
  }
  out[o++] = 0xff;
  out[o++] = 0;
  return o;
}


static int buildprgfrontend(EXPORTSETTINGS *es, PACKRESULT *res, unsigned char **out, int *outsize)
{
  C64SCREEN scr;
  unsigned char packed[4200];
  int packedlen;
  unsigned speedvalue;
  int callspersec;
  int c;
  int fesize;
  unsigned char *buf;
  int total;
  char line[200];

  buildprgscreen(&scr, res);
  {
    unsigned char both[2000];
    memcpy(both, scr.scr, 1000);
    memcpy(both + 1000, scr.col, 1000);
    packedlen = rlepack(both, 2000, packed);
  }

  if (editorInfo.multiplier)
  {
    speedvalue = editorInfo.ntsc ? 0x42c6 / editorInfo.multiplier : 0x4cc7 / editorInfo.multiplier;
    callspersec = (editorInfo.ntsc ? 60 : 50) * editorInfo.multiplier;
  }
  else
  {
    speedvalue = editorInfo.ntsc ? 0x42c6 * 2 : 0x4cc7 * 2;
    callspersec = (editorInfo.ntsc ? 60 : 50) / 2;
  }

  membuf_free(&src);
  membuf_free(&dest);
  insertdefine("MUSICINIT", es->base);
  insertdefine("MUSICPLAY", es->base + 3);
  insertdefine("NUMSONGS", res->songs);
  insertdefine("TIMERLO", speedvalue & 0xff);
  insertdefine("TIMERHI", speedvalue >> 8);
  insertdefine("CALLSPERSEC", callspersec);
  // The rainbow bar is only animated at 1x and 25 Hz: with 2x and more the
  // music interrupt runs so often that the main loop misses the raster line
  // it waits for, and the animation would stutter or stop
  insertdefine("RAINBOWANIM", editorInfo.multiplier <= 1 ? 1 : 0);
  insertdefine("POS_SUBTUNE", POS_SUBTUNE);
  insertdefine("POS_LENGTH", POS_LENGTH);
  insertdefine("POS_TIME", POS_TIME);
  insertdefine("POS_RAINBOW", POS_RAINBOW);
  insertdefine("BORDERCOL", PRG_BORDER);
  insertdefine("BGCOL", PRG_BG);
  insertdefine("LOGOX", LOGOX);
  insertdefine("LOGOY", LOGOY);
  if (!insertresource("frontend.s"))
  {
    seterror("COULD NOT OPEN THE PLAYER FRONTEND!");
    return 0;
  }
  // Song length table: 5 screen codes per subtune
  insertlabel("lengthtbl");
  for (c = 0; c < res->songs; c++)
  {
    unsigned char t[5];
    char tb[16];
    formatsongtime(songlengths[c], tb);
    for (int i = 0; i < 5; i++) t[i] = petscii_screencode(tb[i]);
    insertbytes(t, 5);
  }
  insertlabel("screendata");
  insertbytes(packed, packedlen);
  insertlabel("frontendend");
  (void)line;

  if (assemble(&src, &dest))
  {
    seterror("ASSEMBLY OF THE PLAYER FRONTEND FAILED (INTERNAL ERROR)");
    return 0;
  }
  fesize = membuf_memlen(&dest);
  if (0x0801 + fesize > es->base)
  {
    sprintf(errortext, "THE PLAYER SCREEN NEEDS $0801-$%04X, PLEASE SET THE PLAYER ADDRESS TO $%04X OR HIGHER", 0x0801 + fesize - 1, (0x0801 + fesize + 0xff) & 0xff00);
    membuf_free(&src);
    membuf_free(&dest);
    return 0;
  }
  if (es->base + res->size > 0xd000)
  {
    sprintf(errortext, "A RUNNABLE PRG MUST END BELOW $D000 (MUSIC ENDS AT $%04X)", es->base + res->size - 1);
    membuf_free(&src);
    membuf_free(&dest);
    return 0;
  }
  total = es->base + res->size - 0x0801;
  buf = (unsigned char *)calloc(total + 2, 1);
  if (!buf) return 0;
  buf[0] = 0x01;
  buf[1] = 0x08;
  memcpy(buf + 2, membuf_get(&dest), fesize);
  memcpy(buf + 2 + (es->base - 0x0801), res->data, res->size);
  membuf_free(&src);
  membuf_free(&dest);
  *out = buf;
  *outsize = total + 2;
  return 1;
}

// ---------------------------------------------------------------------------
// Export dialog
// ---------------------------------------------------------------------------

static const char *formatnames[4] = {
  "SID file (for SID players & emulators)",
  "PRG - runnable, with GrooveGoat player screen",
  "PRG - music data + playroutine only",
  "BIN - raw binary (no load address)"};

static void zpdescription(char *buf, int zp)
{
  if (playerversion & PLAYER_ZPGHOSTREGS)
    sprintf(buf, "$%02X-$%02X (ghostregs start at %02X)", zp, zp + 26, zp);
  else if (zp < 0x90)
    sprintf(buf, "$%02X-$%02X (used by BASIC interpreter)", zp, zp + 1);
  else if (zp < 0xfb)
    sprintf(buf, "$%02X-$%02X (used by KERNAL routines)", zp, zp + 1);
  else if (zp < 0xfe)
    sprintf(buf, "$%02X-$%02X (unused)", zp, zp + 1);
  else
    sprintf(buf, "$%02X-$%02X ($FF used by BASIC interpreter)", zp, zp + 1);
}

// Find the PSID v5 layout matching the song's SID addresses (or -1)
static int findv5config(const unsigned *addr, int n, int *start)
{
  int cfg;
  unsigned tmp[16];
  if (addr[0] != 0xd400) return -1;
  for (cfg = 0; cfg < 7; cfg++)
  {
    if (n > v5max[cfg]) continue;
    if (v5addresses(cfg, (int)addr[1] == v5stdstart[cfg] ? 0 : (int)addr[1], n, tmp))
    {
      int k, ok = 1;
      for (k = 0; k < n; k++)
        if (tmp[k] != addr[k]) ok = 0;
      if (ok)
      {
        *start = (int)addr[1] == v5stdstart[cfg] ? 0 : (int)addr[1];
        return cfg;
      }
    }
  }
  return -1;
}

// Returns 1 = export, 0 = cancel
static int exportdialog(EXPORTSETTINGS *es)
{
  int sel = 0;
  int w = 96;
  int h = 26;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - 26) / 2;
  int c;
  int rows[32];
  int numrows;
  int v5cfg = 0;
  int v5start = 0;
  int first = 1;

  // SID files: SID 1 is always at $D400
  {
    int st = 0;
    int cfg = findv5config(sidaddr, numsids, &st);
    if (cfg >= 0)
    {
      v5cfg = cfg;
      v5start = st;
    }
  }

  key = 0;
  rawkey = 0;
  for (;;)
  {
    char buf[200];
    int y;
    int hitrow = -1;
    int hitbtn = 0;
    unsigned addrs[MAX_SIDS];
    int addrok = 1;

    // Rows: 0 format, 1 player address, 2 zeropage, 3 SID layout / addresses, 10+ options, 30 export, 31 cancel
    numrows = 0;
    rows[numrows++] = 0;
    rows[numrows++] = 1;
    rows[numrows++] = 2;
    if (numsids == 1)
    {
      if (es->format != EXP_SID) rows[numrows++] = 3;
    }
    else if (es->format == EXP_SID)
    {
      if (numsids == 2) rows[numrows++] = 4;
      else
      {
        rows[numrows++] = 5;
        rows[numrows++] = 6;
      }
    }
    else
    {
      for (c = 0; c < numsids; c++) rows[numrows++] = 20 + c;
    }
    for (c = 0; c < MAX_OPTIONS; c++)
    {
      if ((numsids > 1) && (c == 4)) continue; // zeropage ghostregs
      rows[numrows++] = 10 + c;
    }
    rows[numrows++] = 30;
    rows[numrows++] = 31;
    if (first)
    {
      sel = numrows - 2;
      first = 0;
    }
    if (sel >= numrows) sel = numrows - 1;
    if (sel < 0) sel = 0;

    // Compute the addresses that will be used
    if (es->format == EXP_SID)
    {
      if (numsids == 1)
      {
        addrs[0] = 0xd400;
      }
      else if (numsids == 2)
      {
        addrs[0] = 0xd400;
        addrs[1] = validsidaddr(sidaddr[1]) ? sidaddr[1] : 0xd420;
      }
      else
      {
        addrok = v5addresses(v5cfg, v5start, numsids, addrs);
      }
    }
    else
    {
      for (c = 0; c < numsids; c++) addrs[c] = numsids == 1 ? sidaddress : sidaddr[c];
    }

    printstatus();
    dlg_drawframe(x0, y0, w, h, "Export music");
    y = y0 + 2;
    for (c = 0; c < numrows; c++)
    {
      int r = rows[c];
      int isel = (c == sel);
      int bg = isel ? C_SELECT : C_PANEL;
      int ry;
      if (r < 10) ry = y0 + 2 + (r < 4 ? r : 3);
      else if (r < 20) ry = y0 + 14 + (r - 10);
      else if (r < 30) ry = y0 + 5 + (r - 20) / 4 * 0;
      else ry = y0 + h - 2;
      buf[0] = 0;
      switch (r)
      {
        case 0:
        sprintf(buf, "Format            " GS_PREV " %s " GS_NEXT, formatnames[es->format]);
        break;
        case 1:
        sprintf(buf, "Player address    $%04X", es->base);
        break;
        case 2:
        {
          char zb[80];
          zpdescription(zb, es->zp);
          sprintf(buf, "Zeropage          %s", zb);
        }
        break;
        case 3:
        sprintf(buf, "SID address       $%04X", addrs[0]);
        break;
        case 4:
        sprintf(buf, "SID 2 address     $%04X   (SID 1 is at $D400 in SID files)", addrs[1]);
        break;
        case 5:
        sprintf(buf, "SID layout        " GS_PREV " %s " GS_NEXT, v5names[v5cfg]);
        ry = y0 + 5;
        break;
        case 6:
        sprintf(buf, "SID 2 starts at   $%04X%s", v5start ? v5start : v5stdstart[v5cfg], v5start ? "" : " (standard)");
        ry = y0 + 6;
        break;
      }
      if ((r >= 20) && (r < 30))
      {
        int s = r - 20;
        ry = y0 + 5 + s / 2;
        sprintf(buf, "SID %d address     $%04X", s + 1, addrs[s]);
        printtext(x0 + 3 + (s & 1) * 32, ry, MKCOL(isel ? 15 : 8, bg), buf);
        if ((mousey == ry) && (mousex >= x0 + 3 + (s & 1) * 32) && (mousex < x0 + 3 + (s & 1) * 32 + 24)) hitrow = c;
        continue;
      }
      if ((r >= 10) && (r < 20))
      {
        int o = r - 10;
        sprintf(buf, "[%c] %s", (playerversion & (PLAYER_BUFFERED << o)) ? 'x' : ' ', playeroptname[o]);
        ry = y0 + 14 + o;
        if ((numsids > 1) && (o > 4)) ry--;
      }
      if (r == 30)
      {
        printtext(x0 + w - 24, ry, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), "  Export  ");
        if ((mousey == ry) && (mousex >= x0 + w - 24) && (mousex < x0 + w - 14)) hitbtn = 1;
        continue;
      }
      if (r == 31)
      {
        printtext(x0 + w - 12, ry, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), "  Cancel  ");
        if ((mousey == ry) && (mousex >= x0 + w - 12) && (mousex < x0 + w - 2)) hitbtn = 2;
        continue;
      }
      if ((r == 5) || (r == 6)) { }
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hitrow = c;
    }

    // Information
    printtext(x0 + 3, y0 + 9, MKCOL(14, C_PANEL), "SIDs:");
    sprintf(buf, "%d x %s, %s, %s", numsids, editorInfo.sidmodel ? "8580" : "6581", editorInfo.ntsc ? "NTSC" : "PAL",
      editorInfo.multiplier ? (editorInfo.multiplier == 1 ? "1x speed" : "multispeed") : "25Hz");
    printtext(x0 + 21, y0 + 9, MKCOL(8, C_PANEL), buf);
    if (numsids > 1)
    {
      int ghost = !directlayout(addrs, numsids);
      sprintf(buf, "%s", ghost ? "Register buffer (any address layout, +1 frame latency)" : "Direct register writes (SIDs are $20 apart)");
      printtext(x0 + 3, y0 + 10, MKCOL(14, C_PANEL), "Playroutine:");
      printtext(x0 + 21, y0 + 10, MKCOL(8, C_PANEL), buf);
    }
    if (es->format == EXP_SID)
    {
      sprintf(buf, "PSID v%d%s", numsids == 1 ? 2 : (numsids == 2 ? 3 : 5),
        numsids > 2 ? "  (multi-SID format of the HVSC SID file specification)" : "");
      printtext(x0 + 3, y0 + 11, MKCOL(14, C_PANEL), "File format:");
      printtext(x0 + 21, y0 + 11, MKCOL(8, C_PANEL), buf);
      if (!addrok)
        printtext(x0 + 3, y0 + 12, MKCOL(12, C_PANEL), "This layout cannot hold this many SIDs - choose another one.");
    }
    if (es->format == EXP_PRGPLAYER)
    {
      printtext(x0 + 3, y0 + 11, MKCOL(14, C_PANEL), "Start on a C64:");
      printtext(x0 + 21, y0 + 11, MKCOL(8, C_PANEL), "LOAD\"NAME\",8,1 then RUN - shows song info & play time");
    }
    printtext(x0 + 3, y0 + 13, MKCOL(14, C_PANEL), "Playroutine options:");
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL), "UP/DOWN select, LEFT/RIGHT/SPACE change, ENTER edit/export, ESC cancel");

    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      win_quitted = 0;
      return 0;
    }
    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn == 1) rawkey = KEY_ENTER, sel = numrows - 2;
      else if (hitbtn == 2) return 0;
      else if (hitrow >= 0)
      {
        sel = hitrow;
        if (rows[sel] == 0) rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        else if (rows[sel] == 5) rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        else rawkey = KEY_ENTER;
        // the < and > arrows always step back / forward
        if (ui_arrowat(mousex, mousey) < 0) rawkey = KEY_LEFT;
        if (ui_arrowat(mousex, mousey) > 0) rawkey = KEY_RIGHT;
      }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h)) return 0;
    }
    switch (rawkey)
    {
      case KEY_ESC:
      return 0;

      case KEY_UP:
      if (sel > 0) sel--;
      break;

      case KEY_DOWN:
      case KEY_TAB:
      if (sel < numrows - 1) sel++;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      case KEY_SPACE:
      case KEY_ENTER:
      {
        int r = rows[sel];
        int dir = (rawkey == KEY_LEFT) ? -1 : 1;
        if (r == 0)
        {
          es->format += dir;
          if (es->format < 0) es->format = 3;
          if (es->format > 3) es->format = 0;
        }
        else if (r == 1)
        {
          if (rawkey == KEY_LEFT) es->base -= 0x100;
          else if (rawkey == KEY_RIGHT) es->base += 0x100;
          else
          {
            unsigned v = es->base;
            if (dlg_edithex("Player address", "Address (page aligned):", &v, 4)) es->base = v;
          }
          es->base &= 0xff00;
          if (es->base < 0x0200) es->base = 0x0200;
        }
        else if (r == 2)
        {
          if (rawkey == KEY_LEFT) es->zp--;
          else if (rawkey == KEY_RIGHT) es->zp++;
          else
          {
            unsigned v = es->zp;
            if (dlg_edithex("Zeropage address", "Address:", &v, 2)) es->zp = v;
          }
          if (playerversion & PLAYER_ZPGHOSTREGS)
          {
            if (es->zp < 0x02) es->zp = 0xe5;
            if (es->zp > 0xe5) es->zp = 0x02;
          }
          else
          {
            if (es->zp < 0x02) es->zp = 0xfe;
            if (es->zp > 0xfe) es->zp = 0x02;
          }
        }
        else if (r == 3)
        {
          unsigned v = sidaddress;
          if (dlg_edithex("SID address", "Address:", &v, 4)) sidaddress = v;
        }
        else if (r == 4)
        {
          unsigned v = addrs[1];
          if (dlg_edithex("SID 2 address", "Address:", &v, 4))
          {
            if (validsidaddr(v)) sidaddr[1] = v;
            else
            {
              const char *l[1] = {"Valid: $D420-$D7E0 or $DE00-$DFE0 in steps of $20."};
              dlg_message("SID 2 address", l, 1);
            }
          }
        }
        else if (r == 5)
        {
          int tries = 7;
          do
          {
            v5cfg += (rawkey == KEY_LEFT) ? -1 : 1;
            if (v5cfg < 0) v5cfg = 6;
            if (v5cfg > 6) v5cfg = 0;
          } while ((v5max[v5cfg] < numsids) && (--tries));
          v5start = 0;
        }
        else if (r == 6)
        {
          unsigned v = v5start ? v5start : v5stdstart[v5cfg];
          if (dlg_edithex("SID 2 start", "Address:", &v, 4))
          {
            if (validsidaddr(v)) v5start = (v == (unsigned)v5stdstart[v5cfg]) ? 0 : v;
          }
        }
        else if ((r >= 20) && (r < 30))
        {
          int s = r - 20;
          unsigned v = sidaddr[s];
          char title[40];
          sprintf(title, "SID %d address", s + 1);
          if (dlg_edithex(title, "Address:", &v, 4))
          {
            if ((v & 0x1f) || (v < 0xd400) || ((v > 0xd7e0) && (v < 0xde00)) || (v > 0xdfe0))
            {
              const char *l[1] = {"Valid: $D400-$D7E0 or $DE00-$DFE0 in steps of $20."};
              dlg_message("SID address", l, 1);
            }
            else
            {
              sidaddr[s] = v;
              songmodified = 1;
            }
          }
        }
        else if ((r >= 10) && (r < 20))
        {
          int opt = r - 10;
          playerversion ^= (PLAYER_BUFFERED << opt);
          if (opt)
          {
            if ((playerversion & PLAYER_SOUNDEFFECTS) || (playerversion & PLAYER_ZPGHOSTREGS) || (playerversion & PLAYER_FULLBUFFERED))
              playerversion |= PLAYER_BUFFERED;
          }
          else
          {
            if (!(playerversion & PLAYER_BUFFERED))
            {
              playerversion &= ~PLAYER_SOUNDEFFECTS;
              playerversion &= ~PLAYER_ZPGHOSTREGS;
              playerversion &= ~PLAYER_FULLBUFFERED;
            }
          }
        }
        else if (r == 30)
        {
          if ((rawkey == KEY_ENTER) || (rawkey == KEY_SPACE))
          {
            if (!addrok) break;
            for (c = 0; c < numsids; c++) es->addr[c] = addrs[c];
            es->ghost = (numsids > 1) && (!directlayout(addrs, numsids));
            es->v5config = v5cfg;
            es->v5start = v5start;
            return 1;
          }
        }
        else if (r == 31)
        {
          if ((rawkey == KEY_ENTER) || (rawkey == KEY_SPACE)) return 0;
        }
      }
      break;
    }
  }
}

static int doexport(int format)
{
  EXPORTSETTINGS es;
  PACKRESULT res;
  char packedsongname[MAX_FILENAME];
  char packedfilter[MAX_FILENAME];
  FILE *songhandle = NULL;
  unsigned char *prgdata = NULL;
  int prgsize = 0;
  int c;
  const char *ext;

  stopsong();
  memset(&es, 0, sizeof es);
  es.format = format;
  es.base = playeradr;
  es.zp = zeropageadr;
  if ((format == EXP_PRGPLAYER) && (es.base < 0x1000)) es.base = 0x1000;

  // Check song first (fast feedback)
  if (!analysesong())
  {
    const char *l[1];
    l[0] = errortext;
    dlg_message("Export", l, 1);
    return 0;
  }

  if (!exportdialog(&es)) return 0;
  playeradr = es.base;
  zeropageadr = es.zp;
  for (c = 0; c < numsids; c++) exportaddr[c] = es.addr[c];

  setstatusmessage("Packing and relocating...");
  printstatus();
  fliptoscreen();

  if (!packmusic(&es, &res))
  {
    const char *l[1];
    l[0] = errortext;
    dlg_message("Export failed", l, 1);
    return 0;
  }

  if (es.format == EXP_PRGPLAYER)
  {
    for (c = 0; c < res.songs; c++) songlengths[c] = calcsonglength(c);
    if (!buildprgfrontend(&es, &res, &prgdata, &prgsize))
    {
      const char *l[1];
      l[0] = errortext;
      dlg_message("Export failed", l, 1);
      free(res.data);
      return 0;
    }
  }

  // Results
  {
    char lines[12][100];
    const char *lp[12];
    const char *opts[2] = {"Save", "Cancel"};
    int n = 0;
    sprintf(lines[n++], "Playroutine:      %5d bytes   (%d SID%s, %d voices)", res.playersize, numsids, numsids > 1 ? "s" : "", numchannels);
    sprintf(lines[n++], "Songtable:        %5d bytes   (%d subtune%s)", res.songtblsize, res.songs, res.songs > 1 ? "s" : "");
    sprintf(lines[n++], "Song-orderlists:  %5d bytes", res.songdatasize);
    sprintf(lines[n++], "Patterntable:     %5d bytes", res.patttblsize);
    sprintf(lines[n++], "Patterns:         %5d bytes", res.pattdatasize);
    sprintf(lines[n++], "Instruments:      %5d bytes", res.instrsize);
    sprintf(lines[n++], "Tables:           %5d bytes", res.tblsize);
    sprintf(lines[n++], "Total size:       %5d bytes   ($%04X-$%04X)", res.size, es.base, es.base + res.size - 1);
    if (es.format == EXP_PRGPLAYER)
      sprintf(lines[n++], "Runnable PRG:     %5d bytes   ($0801-$%04X, start with RUN)", prgsize - 2, 0x0801 + prgsize - 3);
    if (es.ghost)
      sprintf(lines[n++], "SID register buffer at $%04X", res.ghostaddr);
    for (c = 0; c < n; c++) lp[c] = lines[c];
    if (dlg_choose("Packing results", lp, n, opts, 2, 0) != 0)
    {
      free(res.data);
      free(prgdata);
      return 0;
    }
  }

  // By default, copy loaded song name up to the extension
  memset(packedsongname, 0, sizeof packedsongname);
  for (c = 0; c < (int)strlen(loadedsongfilename); c++)
  {
    if (loadedsongfilename[c] == '.') break;
    packedsongname[c] = loadedsongfilename[c];
  }
  if (!packedsongname[0]) strcpy(packedsongname, "music");
  switch (es.format)
  {
    case EXP_SID: ext = ".sid"; break;
    case EXP_BIN: ext = ".bin"; break;
    default: ext = ".prg"; break;
  }
  strcat(packedsongname, ext);
  sprintf(packedfilter, "*%s", ext);

  // Now ask for filename, retry if unsuccessful
  while (!songhandle)
  {
    if (!fileselector(packedsongname, packedpath, packedfilter, "SAVE MUSIC + PLAYROUTINE", 3))
    {
      free(res.data);
      free(prgdata);
      return 0;
    }

    if (strlen(packedsongname) < MAX_FILENAME-4)
    {
      int extfound = 0;
      for (c = strlen(packedsongname)-1; c >= 0; c--)
      {
        if (packedsongname[c] == '.') extfound = 1;
      }
      if (!extfound) strcat(packedsongname, ext);
    }
    songhandle = fopen(packedsongname, "wb");
  }

  switch (es.format)
  {
    case EXP_SID:
    writesidfile(songhandle, &es, &res);
    break;

    case EXP_PRGPLAYER:
    fwrite(prgdata, prgsize, 1, songhandle);
    break;

    case EXP_PRG:
    fwritele16(songhandle, es.base);
    fwrite(res.data, res.size, 1, songhandle);
    break;

    case EXP_BIN:
    fwrite(res.data, res.size, 1, songhandle);
    break;
  }
  fclose(songhandle);
  snprintf(statusmessage, sizeof statusmessage, "Exported %s (%d bytes)", packedsongname, es.format == EXP_PRGPLAYER ? prgsize : res.size);
  fileformat = es.format == EXP_SID ? FORMAT_SID : (es.format == EXP_BIN ? FORMAT_BIN : FORMAT_PRG);
  if (es.format == EXP_PRGPLAYER) prgfrontend = 1;
  if (es.format == EXP_PRG) prgfrontend = 0;
  free(res.data);
  free(prgdata);
  return 1;
}

void relocator(void)
{
  int format = EXP_SID;
  if (fileformat == FORMAT_PRG) format = prgfrontend ? EXP_PRGPLAYER : EXP_PRG;
  if (fileformat == FORMAT_BIN) format = EXP_BIN;
  doexport(format);
  printmainscreen();
  key = 0;
  rawkey = 0;
}

void exportsid(void)
{
  doexport(EXP_SID);
  printmainscreen();
  key = 0;
  rawkey = 0;
}

void exportprg(void)
{
  doexport(EXP_PRGPLAYER);
  printmainscreen();
  key = 0;
  rawkey = 0;
}

// Command line export (no window): groovegoat -export song.sng out.sid
int commandlineexport(const char *outname, int format, int base)
{
  EXPORTSETTINGS es;
  PACKRESULT res;
  FILE *h;
  int c;
  unsigned char *prgdata = NULL;
  int prgsize = 0;

  memset(&es, 0, sizeof es);
  es.format = format;
  es.base = base;
  es.zp = zeropageadr;
  if (numsids == 1)
    es.addr[0] = (format == EXP_SID) ? 0xd400 : sidaddress;
  else if (format == EXP_SID)
  {
    int st = 0;
    int cfg = findv5config(sidaddr, numsids, &st);
    if (numsids == 2)
    {
      es.addr[0] = 0xd400;
      es.addr[1] = validsidaddr(sidaddr[1]) ? sidaddr[1] : 0xd420;
    }
    else
    {
      if (cfg < 0)
      {
        cfg = 0;
        st = 0;
      }
      es.v5config = cfg;
      es.v5start = st;
      v5addresses(cfg, st, numsids, es.addr);
    }
  }
  else
  {
    for (c = 0; c < numsids; c++) es.addr[c] = sidaddr[c];
  }
  es.ghost = (numsids > 1) && (!directlayout(es.addr, numsids));
  for (c = 0; c < numsids; c++) exportaddr[c] = es.addr[c];

  if (!packmusic(&es, &res))
  {
    fprintf(stderr, "error: %s\n", errortext);
    return 0;
  }
  if (format == EXP_PRGPLAYER)
  {
    for (c = 0; c < res.songs; c++) songlengths[c] = calcsonglength(c);
    if (!buildprgfrontend(&es, &res, &prgdata, &prgsize))
    {
      fprintf(stderr, "error: %s\n", errortext);
      return 0;
    }
  }
  h = fopen(outname, "wb");
  if (!h)
  {
    fprintf(stderr, "error: could not write %s\n", outname);
    return 0;
  }
  switch (format)
  {
    case EXP_SID: writesidfile(h, &es, &res); break;
    case EXP_PRGPLAYER: fwrite(prgdata, prgsize, 1, h); break;
    case EXP_PRG: fwritele16(h, es.base); fwrite(res.data, res.size, 1, h); break;
    case EXP_BIN: fwrite(res.data, res.size, 1, h); break;
  }
  fclose(h);
  printf("Exported %s: %d SID(s), %d subtune(s), music $%04X-$%04X%s\n", outname, numsids, res.songs, es.base, es.base + res.size - 1,
    es.ghost ? " (register buffer)" : "");
  for (c = 0; c < numsids; c++) printf("  SID %d at $%04X\n", c + 1, es.addr[c]);
  free(res.data);
  free(prgdata);
  return 1;
}

// ---------------------------------------------------------------------------
// Pattern packing & helpers (unchanged from GoatTracker)
// ---------------------------------------------------------------------------

int packpattern(unsigned char *dest, unsigned char *src, int rows)
{
  unsigned char temp1[MAX_PATTROWS*4];
  unsigned char temp2[512];
  unsigned char instr = 0;
  int command = -1;
  int databyte = -1;
  int destsizeim = 0;
  int destsize = 0;
  int c, d;

  // First optimize instrument changes
  for (c = 0; c < rows; c++)
  {
    if ((c) && (src[c*4+1]) && (src[c*4+1] == instr))
    {
      temp1[c*4] = src[c*4];
      temp1[c*4+1] = 0;
      temp1[c*4+2] = src[c*4+2];
      temp1[c*4+3] = src[c*4+3];
    }
    else
    {
      temp1[c*4] = src[c*4];
      temp1[c*4+1] = src[c*4+1];
      temp1[c*4+2] = src[c*4+2];
      temp1[c*4+3] = src[c*4+3];
      if (src[c*4+1])
        instr = src[c*4+1];
    }

    switch(temp1[c*4+2])
    {
      // Remap speedtable commands
      case CMD_PORTAUP:
      case CMD_PORTADOWN:
      noportamento = 0;
      temp1[c*4+3] = tablemap[STBL][temp1[c*4+3]];
      break;

      case CMD_TONEPORTA:
      notoneporta = 0;
      temp1[c*4+3] = tablemap[STBL][temp1[c*4+3]];
      break;

      case CMD_VIBRATO:
      novib = 0;
      temp1[c*4+3] = tablemap[STBL][temp1[c*4+3]];
      break;

      case CMD_SETAD:
      nosetad = 0;
      break;

      case CMD_SETSR:
      nosetsr = 0;
      break;

      case CMD_SETWAVE:
      nosetwave = 0;
      break;

      // Remap table commands
      case CMD_SETWAVEPTR:
      nosetwaveptr = 0;
      temp1[c*4+3] = tablemap[WTBL][temp1[c*4+3]];
      break;

      case CMD_SETPULSEPTR:
      nosetpulseptr = 0;
      nopulse = 0;
      temp1[c*4+3] = tablemap[PTBL][temp1[c*4+3]];
      break;

      case CMD_SETFILTERPTR:
      nosetfiltptr = 0;
      nofilter = 0;
      temp1[c*4+3] = tablemap[FTBL][temp1[c*4+3]];
      break;

      case CMD_SETFILTERCTRL:
      nosetfiltctrl = 0;
      nofilter = 0;
      break;

      case CMD_SETFILTERCUTOFF:
      nosetfiltcutoff = 0;
      nofilter = 0;
      break;

      case CMD_SETMASTERVOL:
      nosetmastervol = 0;
      // If no authorinfo being saved, erase timingmarks (not supported)
      if (!(playerversion & PLAYER_AUTHORINFO))
      {
        if (temp1[c*4+3] > 0x0f)
        {
          temp1[c*4+2] = 0;
          temp1[c*4+3] = 0;
        }
      }
      break;

      case CMD_FUNKTEMPO:
      nofunktempo = 0;
      temp1[c*4+3] = tablemap[STBL][temp1[c*4+3]];
      break;

      case CMD_SETTEMPO:
      if (temp1[c*4+3] >= 0x80) nochanneltempo = 0;
      else noglobaltempo = 0;
      // Decrease databyte of all tempo commands for the playroutine
      // Do not touch funktempo
      if ((temp1[c*4+3] & 0x7f) >= 3)
        temp1[c*4+3]--;
      break;
    }
  }

  if (noeffects)
  {
    command = 0;
    databyte = 0;
  }

  // Write in playroutine format
  for (c = 0; c < rows; c++)
  {
    // Instrument change with mapping
    if (temp1[c*4+1])
    {
      temp2[destsizeim++] = instrmap[INSTRCHG+temp1[c*4+1]];
    }
    // Rest+FX
    if (temp1[c*4] == REST)
    {
      if ((temp1[c*4+2] != command) || (temp1[c*4+3] != databyte))
      {
        command = temp1[c*4+2];
        databyte = temp1[c*4+3];
        temp2[destsizeim++] = FXONLY+command;
        if (command)
          temp2[destsizeim++] = databyte;
      }
      else
        temp2[destsizeim++] = REST;
    }
    else
    {
      // Normal note
      if ((temp1[c*4+2] != command) || (temp1[c*4+3] != databyte))
      {
        command = temp1[c*4+2];
        databyte = temp1[c*4+3];
        temp2[destsizeim++] = FX+command;
        if (command)
          temp2[destsizeim++] = databyte;
      }
      temp2[destsizeim++] = temp1[c*4];
    }
  }

  // Final step: optimize long singlebyte rests with "packed rest"
  for (c = 0; c < destsizeim;)
  {
    int packok = 1;

    // Never pack first row or sequencer goes crazy
    if (!c) packok = 0;

    // There must be no instrument or command changes on the row to be packed
    if (temp2[c] < FX)
    {
      dest[destsize++] = temp2[c++];
      packok = 0;
    }
    if ((temp2[c] >= FXONLY) && (temp2[c] < FIRSTNOTE))
    {
      int fxnum = temp2[c] - FXONLY;
      dest[destsize++] = temp2[c++];
      if (fxnum) dest[destsize++] = temp2[c++];
      packok = 0;
      goto NEXTROW;
    }
    if (temp2[c] < FXONLY)
     {
      int fxnum = temp2[c] - FX;
      dest[destsize++] = temp2[c++];
      if (fxnum) dest[destsize++] = temp2[c++];
      packok = 0;
    }

    if (temp2[c] != REST) packok = 0;

    if (!packok)
      dest[destsize++] = temp2[c++];
    else
    {
      for (d = c; d < destsizeim; )
      {
        if (temp2[d] == REST)
        {
          d++;
          if (d-c == 64) break;
        }
        else break;
      }
      d -= c;
      if (d > 1)
      {
        dest[destsize++] = -d;
        c += d;
      }
      else
        dest[destsize++] = temp2[c++];
    }
    NEXTROW: {}
  }
  // See if pattern too big
  if (destsize > 256) return -1;

  // If less than 256 bytes, insert endmark
  if (destsize < 256)
  dest[destsize++] = 0x00;

  return destsize;
}

int testoverlap(int area1start, int area1size, int area2start, int area2size)
{
  int area1last = area1start+area1size-1;
  int area2last = area2start+area2size-1;

  if (area1start == area2start) return 1;

  if (area1start < area2start)
  {
    if (area1last < area2start) return 0;
    else return 1;
  }
  else
  {
    if (area2last < area1start) return 0;
    else return 1;
  }
}

unsigned char swapnybbles(unsigned char n)
{
  unsigned char highnybble = n >> 4;
  unsigned char lownybble = n & 0xf;

  return (lownybble << 4) | highnybble;
}

int insertfile(char *name)
{
  return insertresource(name);
}

void inserttext(const char *text)
{
  membuf_append(&src, text, strlen(text));
}

void insertdefine(const char *name, int value)
{
  char insertbuffer[1100];

  sprintf(insertbuffer, "%-16s = %d\n", name, value);
  inserttext(insertbuffer);
}

void insertlabel(const char *name)
{
  char insertbuffer[1100];

  sprintf(insertbuffer, "%s:\n", name);
  inserttext(insertbuffer);
}

void insertbytes(const unsigned char *bytes, int size)
{
  char insertbuffer[1100];
  int row = 0;

  while (size--)
  {
    if (!row)
    {
      inserttext("                .BYTE (");
      sprintf(insertbuffer, "$%02x", *bytes);
      inserttext(insertbuffer);
      bytes++;
      row++;
    }
    else
    {
      sprintf(insertbuffer, ",$%02x", *bytes);
      inserttext(insertbuffer);
      bytes++;
      row++;
      if (row == MAX_BYTES_PER_ROW)
      {
        inserttext(")\n");
        row = 0;
      }
    }
  }
  if (row) inserttext(")\n");
}

void insertbyte(unsigned char byte)
{
  char insertbuffer[1100];

  sprintf(insertbuffer, "                .BYTE ($%02x)\n", byte);
  inserttext(insertbuffer);
}

void insertaddrlo(const char *name)
{
  char insertbuffer[1100];

  sprintf(insertbuffer, "                .BYTE (%s %% 256)\n", name);
  inserttext(insertbuffer);
}

void insertaddrhi(const char *name)
{
  char insertbuffer[1100];

  sprintf(insertbuffer, "                .BYTE (%s / 256)\n", name);
  inserttext(insertbuffer);
}

void findtableduplicates(int num)
{
  int c,d,e;

  if (num == STBL)
  {
    for (c = 1; c <= MAX_TABLELEN; c++)
    {
      if (tableused[num][c])
      {
        for (d = c+1; d <= MAX_TABLELEN; d++)
        {
          if (tableused[num][d])
          {
            if ((ltable[num][d-1] == ltable[num][c-1]) && (rtable[num][d-1] == rtable[num][c-1]))
            {
              // Duplicate found, remove and map to the original
              tableused[num][d] = 0;
              for (e = d; e <= MAX_TABLELEN; e++)
                if (tableused[num][e]) tablemap[num][e]--;
              tablemap[num][d] = tablemap[num][c];
            }
          }
        }
      }
    }
  }
  else
  {
    for (c = 1; c <= MAX_TABLELEN; c++)
    {
      if (isusedandselfcontained(num, c))
      {
        for (d = c + gettablepartlen(num, c - 1); d <= MAX_TABLELEN; )
        {
          int len = gettablepartlen(num, d - 1);

          if (isusedandselfcontained(num, d))
          {
            for (e = 0; e < len; e++)
            {
              if (e < len-1)
              {
                // Is table data the same?
                if ((ltable[num][d+e-1] != ltable[num][c+e-1]) || (rtable[num][d+e-1] != rtable[num][c+e-1]))
                  break;
              }
              else
              {
                // Do both parts have a jump in the end?
                if (ltable[num][d+e-1] != ltable[num][c+e-1])
                  break;
                // Do both parts end?
                if (rtable[num][d+e-1] == 0)
                {
                  if (rtable[num][c+e-1] != 0)
                    break;
                }
                else
                {
                  // Do both parts loop in the same way?
                  if ((rtable[num][d+e-1] - d) != (rtable[num][c+e-1] - c))
                    break;
                }
              }
            }
            if (e == len)
            {
              // Duplicate found, remove and map to the original
              for (e = 0; e < len; e++)
                tableused[num][d+e] = 0;
              for (e = d; e < MAX_TABLELEN; e++)
                if (tableused[num][e]) tablemap[num][e] -= len;
              for (e = 0; e < len; e++)
                tablemap[num][d+e] = tablemap[num][c+e];
            }
          }
          d += len;
        }
      }
    }
  }
}

int isusedandselfcontained(int num, int start)
{
  int len = gettablepartlen(num, start - 1);
  int end = start + len - 1;
  int c;

  // Don't use jumps only
  if (len == 1) return 0;

  // Check that whole table is used
  for (c = start; c <= end; c++)
  {
    if (tableused[num][c] == 0) return 0;
  }
  // Check for jump to outside
  if (rtable[num][end-1] != 0)
  {
    if ((rtable[num][end-1] < start) || (rtable[num][end-1] > end)) return 0;
  }
  // Check for jump from outside
  for (c = 1; c < start; c++)
    if ((tableused[num][c]) && (ltable[num][c-1] == 0xff) && (rtable[num][c-1] >= start) && (rtable[num][c-1] <= end)) return 0;
  for (c = end+1; c <= MAX_TABLELEN; c++)
    if ((tableused[num][c]) && (ltable[num][c-1] == 0xff) && (rtable[num][c-1] >= start) && (rtable[num][c-1] <= end)) return 0;

  // OK!
  return 1;
}

void calcspeedtest(unsigned char pos)
{
  if (!pos)
  {
    nozerospeed = 0;
    return;
  }

  if (ltable[STBL][pos-1] >= 0x80) nocalculatedspeed = 0;
  else nonormalspeed = 0;
}

// C64 GrooveGoat - Join Songs
//
// Loads other songs (.sng 1/2 SIDs, .ggt 4/8 SIDs) into the current song:
//   - as new subtunes (every subtune of every file), or
//   - appended to the end of the current subtune (medley, first subtune of
//     every file)
// Instruments with exactly the same sound (ADSR, gate timer, 1st wave,
// vibrato and the contents of the wave / pulse / filter / speed programs) are
// shared, as are identical table programs and patterns. Pattern commands and
// wavetable commands that point into the tables are remapped.
//
// A source file is read with the normal song loader (all formats and their
// conversions) while the current song is kept in a snapshot.

#include <time.h>
#include <stdarg.h>
#include "goattrk2.h"
#include "gres.h"
#include "gjoin.h"

extern char loadedsongfilename[MAX_FILENAME];
extern char songfilename[MAX_FILENAME];
extern char songpath[MAX_PATHNAME];
extern char songfilter[MAX_FILENAME];

// ---------------------------------------------------------------------------
// Snapshot of the whole song (and the editor state the loader changes)
// ---------------------------------------------------------------------------

typedef struct
{
  INSTR instr[MAX_INSTR];
  unsigned char l[MAX_TABLES][MAX_TABLELEN], r[MAX_TABLES][MAX_TABLELEN];
  unsigned char order[MAX_SONGS][MAX_CHN][MAX_SONGLEN + 2];
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int pattlen[MAX_PATT];
  int songlen[MAX_SONGS][MAX_CHN];
  char names[3][MAX_STR];
  int numsids, numchannels;
  unsigned sidaddr[MAX_SIDS];
  EDITOR_INFO ei;
  EDITOR_UNDO_INFO eui;
  char songfilename[MAX_FILENAME], loadedsongfilename[MAX_FILENAME];
  int songmodified;
} SNAPSHOT;

static void snap_save(SNAPSHOT *s)
{
  memcpy(s->instr, instr, sizeof instr);
  memcpy(s->l, ltable, sizeof ltable);
  memcpy(s->r, rtable, sizeof rtable);
  memcpy(s->order, songorder, sizeof songorder);
  memcpy(s->patt, pattern, sizeof pattern);
  memcpy(s->pattlen, pattlen, sizeof pattlen);
  memcpy(s->songlen, songlen, sizeof songlen);
  memcpy(s->names[0], songname, MAX_STR);
  memcpy(s->names[1], authorname, MAX_STR);
  memcpy(s->names[2], copyrightname, MAX_STR);
  s->numsids = numsids;
  s->numchannels = numchannels;
  memcpy(s->sidaddr, sidaddr, sizeof sidaddr);
  s->ei = editorInfo;
  s->eui = editorUndoInfo;
  memcpy(s->songfilename, songfilename, MAX_FILENAME);
  memcpy(s->loadedsongfilename, loadedsongfilename, MAX_FILENAME);
  s->songmodified = songmodified;
}

static void snap_restore(const SNAPSHOT *s)
{
  memcpy(instr, s->instr, sizeof instr);
  memcpy(ltable, s->l, sizeof ltable);
  memcpy(rtable, s->r, sizeof rtable);
  memcpy(songorder, s->order, sizeof songorder);
  memcpy(pattern, s->patt, sizeof pattern);
  memcpy(pattlen, s->pattlen, sizeof pattlen);
  memcpy(songlen, s->songlen, sizeof songlen);
  memcpy(songname, s->names[0], MAX_STR);
  memcpy(authorname, s->names[1], MAX_STR);
  memcpy(copyrightname, s->names[2], MAX_STR);
  numsids = s->numsids;
  numchannels = s->numchannels;
  memcpy(sidaddr, s->sidaddr, sizeof sidaddr);
  editorInfo = s->ei;
  editorUndoInfo = s->eui;
  memcpy(songfilename, s->songfilename, MAX_FILENAME);
  memcpy(loadedsongfilename, s->loadedsongfilename, MAX_FILENAME);
  songmodified = s->songmodified;
  countpatternlengths();
}

// ---------------------------------------------------------------------------
// Source songs
// ---------------------------------------------------------------------------

typedef struct
{
  int nch, numsids, multiplier, adparam, ntsc, deftempo;
  INSTR instr[MAX_INSTR];
  unsigned char l[MAX_TABLES][MAX_TABLELEN], r[MAX_TABLES][MAX_TABLELEN];
  int tlen[MAX_TABLES];
  unsigned char order[MAX_SONGS][MAX_CHN][MAX_SONGLEN + 2];
  int songlen[MAX_SONGS][MAX_CHN];
  int numsongs;
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int pattlen[MAX_PATT];
  char name[MAX_STR];
} SRCSONG;

static int deftempo(const INSTR *ins63)
{
  // as the playroutine: instrument $3F without wavetable = default tempo
  if ((ins63->ad >= 2) && (!ins63->ptr[WTBL])) return ins63->ad;
  return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

// Read a song file with the normal loader; the current song is kept.
static int loadsource(const char *path, SRCSONG *src, char *err, int errsize)
{
  static SNAPSHOT keep;
  int ok, c, s;
  void *bstate = backup_keepstate();
  snap_save(&keep);
  snprintf(songfilename, MAX_FILENAME, "%s", path);
  loadsong();
  ok = strncmp(statusmessage, "Could not", 9) != 0;
  if (ok)
  {
    memset(src, 0, sizeof *src);
    src->nch = numchannels;
    src->numsids = numsids;
    src->multiplier = editorInfo.multiplier;
    src->adparam = editorInfo.adparam;
    src->ntsc = editorInfo.ntsc;
    memcpy(src->instr, instr, sizeof instr);
    memcpy(src->l, ltable, sizeof ltable);
    memcpy(src->r, rtable, sizeof rtable);
    for (c = 0; c < MAX_TABLES; c++) src->tlen[c] = gettablelen(c);
    memcpy(src->order, songorder, sizeof songorder);
    countpatternlengths();
    memcpy(src->songlen, songlen, sizeof songlen);
    memcpy(src->patt, pattern, sizeof pattern);
    memcpy(src->pattlen, pattlen, sizeof pattlen);
    memcpy(src->name, songname, MAX_STR);
    src->numsongs = 0;
    for (s = 0; s < MAX_SONGS; s++)
      if (songvalid(s)) src->numsongs = s + 1;
    src->deftempo = deftempo(&instr[MAX_INSTR - 1]);
  }
  else snprintf(err, errsize, "%s", statusmessage);
  snap_restore(&keep);
  backup_restorestate(bstate);
  statusmessage[0] = 0;
  return ok && src->numsongs > 0;
}

// ---------------------------------------------------------------------------
// Table programs
// ---------------------------------------------------------------------------

// Canonical form of the program at row "start" (0-based): the rows in the
// order they run, jumps as indexes into that list. rows[] gets the table rows.
// Returns the number of entries.
typedef struct
{
  int l, r;
} CENT;

static int canon(const unsigned char *l, const unsigned char *r, int len, int start, CENT *out, int *rows, int max)
{
  int idx[MAX_TABLELEN];
  int n = 0, cur = start, c;
  for (c = 0; c < MAX_TABLELEN; c++) idx[c] = -1;
  while (n < max)
  {
    if ((cur < 0) || (cur >= len))
    {
      out[n].l = 0x1000;     // runs off the end of the table
      out[n].r = 0;
      if (rows) rows[n] = -1;
      n++;
      break;
    }
    if (idx[cur] >= 0) break;
    idx[cur] = n;
    if (rows) rows[n] = cur;
    out[n].l = l[cur];
    out[n].r = r[cur];
    if (l[cur] == 0xff)
    {
      int t = r[cur] - 1;
      if (r[cur] == 0)
      {
        n++;
        break;
      }
      if ((t >= 0) && (t < len) && (idx[t] >= 0))
      {
        out[n].r = 0x100 + idx[t];
        n++;
        break;
      }
      out[n].r = 0x100 + n + 1;
      n++;
      cur = t;
      continue;
    }
    n++;
    cur++;
  }
  return n;
}

static int sameprog(const CENT *a, int na, const CENT *b, int nb)
{
  int c;
  if (na != nb) return 0;
  for (c = 0; c < na; c++)
    if ((a[c].l != b[c].l) || (a[c].r != b[c].r)) return 0;
  return 1;
}

// wavetable command with a table / speed pointer in the right column
static int wavecmdtable(int l)
{
  int cmd;
  if ((l < WAVECMD) || (l > WAVELASTCMD)) return -1;
  cmd = l & 0x0f;
  if (cmd == CMD_SETWAVEPTR) return WTBL;
  if (cmd == CMD_SETPULSEPTR) return PTBL;
  if (cmd == CMD_SETFILTERPTR) return FTBL;
  if (((cmd >= CMD_PORTAUP) && (cmd <= CMD_VIBRATO)) || (cmd == CMD_FUNKTEMPO)) return STBL;
  return -1;
}

// pattern command with a table / speed pointer in the data byte
static int pattcmdtable(int cmd)
{
  if (cmd == CMD_SETWAVEPTR) return WTBL;
  if (cmd == CMD_SETPULSEPTR) return PTBL;
  if (cmd == CMD_SETFILTERPTR) return FTBL;
  if (((cmd >= CMD_PORTAUP) && (cmd <= CMD_VIBRATO)) || (cmd == CMD_FUNKTEMPO)) return STBL;
  return -1;
}

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------

#define MAXC 256

static int sameinstr(const INSTR *a, const unsigned char (*al)[MAX_TABLELEN], const unsigned char (*ar)[MAX_TABLELEN], const int *alen,
                     const INSTR *b, const unsigned char (*bl)[MAX_TABLELEN], const unsigned char (*br)[MAX_TABLELEN], const int *blen)
{
  static CENT ca[MAXC], cb[MAXC];
  int t;
  if ((a->ad != b->ad) || (a->sr != b->sr) || (a->gatetimer != b->gatetimer) || (a->firstwave != b->firstwave) ||
      (a->vibdelay != b->vibdelay))
    return 0;
  for (t = 0; t < MAX_TABLES; t++)
  {
    if ((!a->ptr[t]) != (!b->ptr[t])) return 0;
    if (!a->ptr[t]) continue;
    if (t == STBL)
    {
      int pa = a->ptr[t] - 1, pb = b->ptr[t] - 1;
      if ((pa >= alen[t]) || (pb >= blen[t])) return 0;
      if ((al[t][pa] != bl[t][pb]) || (ar[t][pa] != br[t][pb])) return 0;
      continue;
    }
    {
      int na = canon(al[t], ar[t], alen[t], a->ptr[t] - 1, ca, NULL, MAXC);
      int nb = canon(bl[t], br[t], blen[t], b->ptr[t] - 1, cb, NULL, MAXC);
      int c;
      if (!sameprog(ca, na, cb, nb)) return 0;
      // pointers of wavetable commands differ between songs: no sharing
      if (t == WTBL)
        for (c = 0; c < na; c++)
          if ((wavecmdtable(ca[c].l) >= 0) && (ca[c].r)) return 0;
    }
  }
  return 1;
}

static int instrempty(int i)
{
  const INSTR *in = &instr[i];
  return (!in->ad) && (!in->sr) && (!in->ptr[0]) && (!in->ptr[1]) && (!in->ptr[2]) && (!in->ptr[3]) &&
         (!in->vibdelay) && (!in->name[0]);
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

static unsigned char reserved[MAX_PATT];

static int patternreferenced(int p)
{
  int s, c, d;
  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < MAX_SONGLEN; d++)
      {
        int v = songorder[s][c][d];
        if (v == LOOPSONG) break;
        if (v == p) return 1;
      }
  return 0;
}

static int patternempty(int p)
{
  int r;
  for (r = 0; r < pattlen[p]; r++)
    if ((pattern[p][r * 4] != REST) || (pattern[p][r * 4 + 1]) || (pattern[p][r * 4 + 2]) || (pattern[p][r * 4 + 3])) return 0;
  return 1;
}

static int freepattern(void)
{
  int p;
  for (p = 0; p < MAX_PATT; p++)
    if ((!reserved[p]) && (patternempty(p)) && (!patternreferenced(p))) return p;
  return -1;
}

static void setpattern(int p, const unsigned char *data, int len)
{
  memset(pattern[p], 0, sizeof pattern[p]);
  memcpy(pattern[p], data, len * 4);
  pattern[p][len * 4] = ENDPATT;
  pattlen[p] = len;
  reserved[p] = 1;
}

// identical pattern already in the song, else a free one with the data
static int putpattern(const unsigned char *data, int len, int *reused)
{
  int p;
  for (p = 0; p < MAX_PATT; p++)
    if ((pattlen[p] == len) && (!memcmp(pattern[p], data, len * 4)) && ((reserved[p]) || (patternreferenced(p))))
    {
      *reused = 1;
      return p;
    }
  *reused = 0;
  p = freepattern();
  if (p >= 0) setpattern(p, data, len);
  return p;
}

static int emptypattern(int len)
{
  static unsigned char rows[MAX_PATTROWS * 4];
  int r, dummy;
  for (r = 0; r < len; r++)
  {
    rows[r * 4] = REST;
    rows[r * 4 + 1] = rows[r * 4 + 2] = rows[r * 4 + 3] = 0;
  }
  return putpattern(rows, len, &dummy);
}

// rows of an orderlist (until the loop mark), -1 = unknown pattern
static int orderrows(const unsigned char *ord, const int *plen)
{
  int d, rows = 0, rep = 0;
  for (d = 0; d < MAX_SONGLEN; d++)
  {
    int v = ord[d];
    if (v == LOOPSONG) break;
    if ((v >= REPEAT) && (v < TRANSDOWN)) { rep = v - REPEAT; continue; }
    if (v >= TRANSDOWN) continue;
    rows += plen[v] * (rep + 1);
    rep = 0;
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Join one source into the current song
// ---------------------------------------------------------------------------

typedef struct
{
  int subfirst, sublast;          // new subtunes (JOIN_SUBTUNES)
  int insnew, insreused;
  int tabnew[MAX_TABLES], tabreused[MAX_TABLES];
  int pattnew, pattreused;
  int tempofix;
  int raised;
} JOINSTATS;

static int joinone(const SRCSONG *src, int mode, int raise, JOINSTATS *st, char *err, int errsize)
{
  static unsigned char usedpatt[MAX_PATT], usedins[MAX_INSTR];
  static unsigned char entry[MAX_TABLES][MAX_TABLELEN];      // needed entry points (source rows)
  static int entrymap[MAX_TABLES][MAX_TABLELEN];               // source row -> destination row
  static int rowmap[MAX_TABLES][MAX_TABLELEN];
  static unsigned char copyrow[MAX_TABLES][MAX_TABLELEN];
  static int insmap[MAX_INSTR];
  static int pattmap[MAX_PATT];
  static CENT cs[MAXC], cd[MAXC];
  static int crow[MAXC];
  static unsigned char buf[MAX_PATTROWS * 4];
  int dlen[MAX_TABLES];
  int s, c, d, t, i, firstsong, lastsong;

  memset(st, 0, sizeof *st);
  memset(usedpatt, 0, sizeof usedpatt);
  memset(usedins, 0, sizeof usedins);
  memset(entry, 0, sizeof entry);
  memset(copyrow, 0, sizeof copyrow);

  // channels
  if (src->nch > numchannels)
  {
    if (!raise)
    {
      snprintf(err, errsize, "The song has %d SIDs, the current song %d (allow raising the SID count)", src->numsids, numsids);
      return 0;
    }
    setnumsids(src->numsids);
    st->raised = numsids;
  }

  firstsong = 0;
  lastsong = mode == JOIN_MEDLEY ? 0 : src->numsongs - 1;

  // used patterns and instruments
  for (s = firstsong; s <= lastsong; s++)
    for (c = 0; c < src->nch; c++)
      for (d = 0; d < src->songlen[s][c]; d++)
        if (src->order[s][c][d] < REPEAT) usedpatt[src->order[s][c][d]] = 1;
  for (i = 0; i < MAX_PATT; i++)
  {
    if (!usedpatt[i]) continue;
    for (d = 0; d < src->pattlen[i]; d++)
    {
      int ins = src->patt[i][d * 4 + 1], cmd = src->patt[i][d * 4 + 2], data = src->patt[i][d * 4 + 3];
      if ((ins > 0) && (ins < MAX_INSTR)) usedins[ins] = 1;
      t = pattcmdtable(cmd);
      if ((t >= 0) && (data > 0) && (data <= src->tlen[t])) entry[t][data - 1] = 1;
    }
  }

  // instruments: shared when the sound is identical
  for (t = 0; t < MAX_TABLES; t++) dlen[t] = gettablelen(t);
  for (i = 1; i < MAX_INSTR - 1; i++)
  {
    insmap[i] = 0;
    if (!usedins[i]) continue;
    if ((!src->instr[i].ad) && (!src->instr[i].sr) && (!src->instr[i].ptr[WTBL]) && (!src->instr[i].name[0])) continue;
    for (d = 1; d < MAX_INSTR - 1; d++)
    {
      if (instrempty(d)) continue;
      if (sameinstr(&src->instr[i], src->l, src->r, src->tlen, &instr[d], (const unsigned char (*)[MAX_TABLELEN])ltable,
                    (const unsigned char (*)[MAX_TABLELEN])rtable, dlen))
      {
        insmap[i] = d;
        st->insreused++;
        break;
      }
    }
    if (!insmap[i])
    {
      insmap[i] = -1;      // copy
      for (t = 0; t < MAX_TABLES; t++)
        if ((src->instr[i].ptr[t]) && (src->instr[i].ptr[t] <= src->tlen[t])) entry[t][src->instr[i].ptr[t] - 1] = 1;
    }
  }
  insmap[MAX_INSTR - 1] = MAX_INSTR - 1;
  insmap[0] = 0;

  // tables: wave first (its commands add entries to all tables)
  for (t = 0; t < MAX_TABLES; t++)
  {
    int changed = 1;
    for (i = 0; i < MAX_TABLELEN; i++) entrymap[t][i] = -1;
    if (t == STBL)
    {
      // speed table: single rows, shared when equal
      for (i = 0; i < src->tlen[t]; i++)
      {
        if (!entry[t][i]) continue;
        for (d = 0; d < dlen[t]; d++)
          if ((ltable[t][d] == src->l[t][i]) && (rtable[t][d] == src->r[t][i])) break;
        if (d < dlen[t]) { entrymap[t][i] = d; st->tabreused[t]++; }
        else copyrow[t][i] = 1;
      }
    }
    else
    {
      static unsigned char dedup[MAX_TABLELEN];
      static int cand[MAX_TABLELEN];
      int again = 1;
      while (again)
      {
        int stable = 0;
        again = 0;
        // 1. an identical program in the song? (not for programs with
        //    wavetable commands: their pointers are remapped)
        for (i = 0; i < src->tlen[t]; i++)
        {
          int n, k, hascmd = 0;
          dedup[i] = 0;
          cand[i] = -1;
          if (!entry[t][i]) continue;
          n = canon(src->l[t], src->r[t], src->tlen[t], i, cs, crow, MAXC);
          if (t == WTBL)
            for (k = 0; k < n; k++)
              if ((wavecmdtable(cs[k].l) >= 0) && (cs[k].r)) hascmd = 1;
          if (hascmd) continue;
          for (d = 0; d < dlen[t]; d++)
          {
            int m = canon(ltable[t], rtable[t], dlen[t], d, cd, NULL, MAXC);
            if (sameprog(cs, n, cd, m)) break;
          }
          if (d < dlen[t]) { cand[i] = d; dedup[i] = 1; }
        }
        // 2. rows to copy; a shared program must not overlap them
        while (!stable)
        {
          stable = 1;
          memset(copyrow[t], 0, sizeof copyrow[t]);
          for (i = 0; i < src->tlen[t]; i++)
          {
            int n, k;
            if ((!entry[t][i]) || (dedup[i])) continue;
            n = canon(src->l[t], src->r[t], src->tlen[t], i, cs, crow, MAXC);
            for (k = 0; k < n; k++)
              if (crow[k] >= 0) copyrow[t][crow[k]] = 1;
          }
          for (i = 0; i < src->tlen[t]; i++)
          {
            int n, k;
            if (!dedup[i]) continue;
            n = canon(src->l[t], src->r[t], src->tlen[t], i, cs, crow, MAXC);
            for (k = 0; k < n; k++)
              if ((crow[k] >= 0) && (copyrow[t][crow[k]])) break;
            if (k < n) { dedup[i] = 0; stable = 0; }
          }
        }
        // 3. wavetable commands in copied rows point to more programs
        if (t == WTBL)
        {
          for (i = 0; i < src->tlen[t]; i++)
          {
            int tt, data;
            if (!copyrow[t][i]) continue;
            tt = wavecmdtable(src->l[t][i]);
            data = src->r[t][i];
            if ((tt >= 0) && (data > 0) && (data <= src->tlen[tt]) && (!entry[tt][data - 1]))
            {
              entry[tt][data - 1] = 1;
              if (tt == WTBL) again = 1;
            }
          }
        }
      }
      for (i = 0; i < src->tlen[t]; i++)
        if (dedup[i])
        {
          entrymap[t][i] = cand[i];
          st->tabreused[t]++;
        }
    }
    // copy the rows (in their order)
    {
      int n = 0;
      for (i = 0; i < src->tlen[t]; i++)
        if (copyrow[t][i]) n++;
      if (dlen[t] + n > MAX_TABLELEN)
      {
        static const char *tn[MAX_TABLES] = {"wave", "pulse", "filter", "speed"};
        snprintf(err, errsize, "The %stable is full (%d rows needed, %d free)", tn[t], n, MAX_TABLELEN - dlen[t]);
        return 0;
      }
      n = dlen[t];
      for (i = 0; i < src->tlen[t]; i++)
      {
        rowmap[t][i] = -1;
        if (!copyrow[t][i]) continue;
        rowmap[t][i] = n;
        ltable[t][n] = src->l[t][i];
        rtable[t][n] = src->r[t][i];
        n++;
      }
      st->tabnew[t] = n - dlen[t];
      for (i = 0; i < src->tlen[t]; i++)
        if ((entry[t][i]) && (entrymap[t][i] < 0)) entrymap[t][i] = rowmap[t][i];
      // jumps
      for (i = 0; i < src->tlen[t]; i++)
      {
        int dst = rowmap[t][i];
        if ((dst < 0) || (t == STBL)) continue;
        if ((ltable[t][dst] == 0xff) && (rtable[t][dst]))
        {
          int target = rtable[t][dst] - 1;
          rtable[t][dst] = (target < src->tlen[t]) && (rowmap[t][target] >= 0) ? rowmap[t][target] + 1 : 0;
        }
      }
    }
  }
  // wavetable commands: pointers into the other tables
  for (i = 0; i < src->tlen[WTBL]; i++)
  {
    int dst = rowmap[WTBL][i], tt, data;
    if (dst < 0) continue;
    tt = wavecmdtable(ltable[WTBL][dst]);
    data = rtable[WTBL][dst];
    if ((tt >= 0) && (data > 0))
      rtable[WTBL][dst] = (data <= src->tlen[tt]) && (entrymap[tt][data - 1] >= 0) ? entrymap[tt][data - 1] + 1 : 0;
  }

  // copy the instruments that are not shared
  for (i = 1; i < MAX_INSTR - 1; i++)
  {
    INSTR *in;
    if (insmap[i] != -1) continue;
    for (d = 1; d < MAX_INSTR - 1; d++)
      if (instrempty(d)) break;
    if (d >= MAX_INSTR - 1)
    {
      snprintf(err, errsize, "No free instrument slot (63 instruments are the limit)");
      return 0;
    }
    in = &instr[d];
    *in = src->instr[i];
    if (!in->name[0]) snprintf(in->name, MAX_INSTRNAMELEN, "Instr %02X", i);
    for (t = 0; t < MAX_TABLES; t++)
      if (in->ptr[t])
        in->ptr[t] = (in->ptr[t] <= src->tlen[t]) && (entrymap[t][in->ptr[t] - 1] >= 0) ? entrymap[t][in->ptr[t] - 1] + 1 : 0;
    insmap[i] = d;
    st->insnew++;
  }

  // patterns
  for (i = 0; i < MAX_PATT; i++)
  {
    int reused, len;
    pattmap[i] = -1;
    if (!usedpatt[i]) continue;
    len = src->pattlen[i];
    for (d = 0; d < len; d++)
    {
      int ins = src->patt[i][d * 4 + 1], cmd = src->patt[i][d * 4 + 2], data = src->patt[i][d * 4 + 3];
      buf[d * 4] = src->patt[i][d * 4];
      buf[d * 4 + 1] = (ins > 0) && (ins < MAX_INSTR) ? (insmap[ins] > 0 ? insmap[ins] : 0) : ins;
      buf[d * 4 + 2] = cmd;
      t = pattcmdtable(cmd);
      if ((t >= 0) && (data > 0))
        data = (data <= src->tlen[t]) && (entrymap[t][data - 1] >= 0) ? entrymap[t][data - 1] + 1 : 0;
      buf[d * 4 + 3] = data;
    }
    pattmap[i] = putpattern(buf, len, &reused);
    if (pattmap[i] < 0)
    {
      snprintf(err, errsize, "No free pattern (%d patterns are the limit)", MAX_PATT);
      return 0;
    }
    if (reused) st->pattreused++;
    else st->pattnew++;
  }

  // orderlists
  if (mode == JOIN_SUBTUNES)
  {
    int base = countsongs();
    if (!songvalid(base - 1)) base--;
    if (base + src->numsongs > MAX_SONGS)
    {
      snprintf(err, errsize, "Too many subtunes (%d is the limit)", MAX_SONGS);
      return 0;
    }
    st->subfirst = base;
    st->sublast = base + src->numsongs - 1;
    for (s = 0; s < src->numsongs; s++)
    {
      int ds = base + s;
      for (c = 0; c < numchannels; c++)
      {
        memset(songorder[ds][c], 0, MAX_SONGLEN + 2);
        if (c < src->nch)
        {
          int n = src->songlen[s][c];
          for (d = 0; d < n; d++)
          {
            int v = src->order[s][c][d];
            songorder[ds][c][d] = v < REPEAT ? pattmap[v] : v;
          }
          songorder[ds][c][n] = LOOPSONG;
          songorder[ds][c][n + 1] = src->order[s][c][n + 1];
        }
        else
        {
          int p = emptypattern(32);
          if (p < 0)
          {
            snprintf(err, errsize, "No free pattern (%d patterns are the limit)", MAX_PATT);
            return 0;
          }
          songorder[ds][c][0] = p;
          songorder[ds][c][1] = LOOPSONG;
          songorder[ds][c][2] = 0;
        }
      }
    }
  }
  else
  {
    // medley: append the first subtune of the file to the current subtune;
    // all channels of both parts must have the same length in rows
    int ds = editorInfo.esnum, drows = -1, srows = -1;
    countpatternlengths();
    for (c = 0; c < numchannels; c++)
    {
      int r = orderrows(songorder[ds][c], pattlen);
      if (drows < 0) drows = r;
      if (r != drows)
      {
        snprintf(err, errsize, "Medley: the channels of subtune %02X have different lengths (%d / %d rows)", ds, drows, r);
        return 0;
      }
    }
    for (c = 0; c < src->nch; c++)
    {
      int r = orderrows(src->order[0][c], src->pattlen);
      if (srows < 0) srows = r;
      if (r != srows)
      {
        snprintf(err, errsize, "Medley: the channels of the song have different lengths (%d / %d rows)", srows, r);
        return 0;
      }
    }
    for (c = 0; c < numchannels; c++)
    {
      int n = songlen[ds][c], restart = songorder[ds][c][n + 1];
      int add = 0;
      unsigned char tmp[MAX_SONGLEN + 2];
      if (c < src->nch)
      {
        for (d = 0; d < src->songlen[0][c]; d++)
        {
          int v = src->order[0][c][d];
          tmp[add++] = v < REPEAT ? pattmap[v] : v;
        }
      }
      else
      {
        // silent channel: empty patterns as long as the patterns of voice 1
        int rep = 0;
        for (d = 0; d < src->songlen[0][0]; d++)
        {
          int v = src->order[0][0][d], p;
          if ((v >= REPEAT) && (v < TRANSDOWN)) { rep = v - REPEAT; tmp[add++] = v; continue; }
          if (v >= TRANSDOWN) continue;
          p = emptypattern(src->pattlen[v]);
          if (p < 0)
          {
            snprintf(err, errsize, "No free pattern (%d patterns are the limit)", MAX_PATT);
            return 0;
          }
          tmp[add++] = p;
          rep = 0;
        }
        (void)rep;
      }
      if (n + add > MAX_SONGLEN - 1)
      {
        snprintf(err, errsize, "Medley: the orderlist of channel %d.%d gets too long (%d entries)", c / 3 + 1, c % 3 + 1, n + add);
        return 0;
      }
      memcpy(&songorder[ds][c][n], tmp, add);
      songorder[ds][c][n + add] = LOOPSONG;
      songorder[ds][c][n + add + 1] = restart;
    }
    st->subfirst = st->sublast = ds;
  }

  // tempo: the song's default tempo goes on its first row when it differs
  // (new subtune) or always (medley: the tempo before it is unknown), unless
  // the song starts with a tempo command anyway
  {
    int mytempo = deftempo(&instr[MAX_INSTR - 1]);
    int nsongs = mode == JOIN_MEDLEY ? 1 : src->numsongs;
    countpatternlengths();
    for (s = 0; s < nsongs; s++)
    {
      int ds = st->subfirst + (mode == JOIN_MEDLEY ? 0 : s), hastempo = 0, best = -1, bestk = 0;
      if ((mode == JOIN_SUBTUNES) && (src->deftempo == mytempo)) continue;
      for (c = 0; c < src->nch; c++)
      {
        int k = 0, first;
        while ((k < src->songlen[s][c]) && (src->order[s][c][k] >= REPEAT)) k++;
        if (k >= src->songlen[s][c]) continue;
        first = src->order[s][c][k];
        if ((src->patt[first][2] == CMD_SETTEMPO) || (src->patt[first][2] == CMD_FUNKTEMPO)) hastempo = 1;
        if ((best < 0) && (src->patt[first][2] == 0) && (src->patt[first][3] == 0)) { best = c; bestk = k; }
      }
      if ((hastempo) || (best < 0)) continue;
      {
        unsigned char *ord = songorder[ds][best];
        int k = bestk, slot, p, reused;
        if (mode == JOIN_MEDLEY) k += songlen[ds][best] - src->songlen[0][best];
        slot = ord[k];
        memcpy(buf, pattern[slot], pattlen[slot] * 4);
        buf[2] = CMD_SETTEMPO;
        buf[3] = src->deftempo;
        p = putpattern(buf, pattlen[slot], &reused);
        if (p < 0) continue;
        if (!reused) st->pattnew++;
        ord[k] = p;
        st->tempofix++;
      }
    }
  }
  countpatternlengths();
  return 1;
}

// ---------------------------------------------------------------------------
// Join a list of files
// ---------------------------------------------------------------------------

typedef struct
{
  char path[MAX_PATHNAME];
  char shortname[64];
  int ok;
  int nch, numsongs, numins;
  char info[160];
} JOINFILE;

static JOINFILE jfiles[JOIN_MAXFILES];
static int numjfiles = 0;
static int joinmode = JOIN_SUBTUNES;
static int joinraise = 1;
static char report[24][160];
static int numreport = 0;
static int reporterror = 0;

static void rep(int err, const char *fmt, ...)
{
  va_list ap;
  if (numreport >= 24) return;
  va_start(ap, fmt);
  vsnprintf(report[numreport++], 160, fmt, ap);
  va_end(ap);
  if (err) reporterror = 1;
}

static const char *basename_of(const char *p)
{
  const char *b = p, *s;
  for (s = p; *s; s++)
    if ((*s == '/') || (*s == '\\')) b = s + 1;
  return b;
}

// Joins all files; dry = 1: only checks (the song is restored)
static int joinall(int dry)
{
  static SNAPSHOT keep;
  static SRCSONG src;
  int f, ok = 1, instrbefore = 0, i;
  char err[200];

  numreport = 0;
  reporterror = 0;
  if (!numjfiles)
  {
    rep(1, "Add one or more songs (.sng / .ggt).");
    return 0;
  }
  snap_save(&keep);
  memset(reserved, 0, sizeof reserved);
  for (i = 1; i < MAX_INSTR - 1; i++)
    if (!instrempty(i)) instrbefore++;
  for (f = 0; (f < numjfiles) && ok; f++)
  {
    JOINSTATS st;
    err[0] = 0;
    if (!loadsource(jfiles[f].path, &src, err, sizeof err))
    {
      rep(1, "%s: %s", jfiles[f].shortname, err[0] ? err : "no subtunes");
      ok = 0;
      break;
    }
    if (!joinone(&src, joinmode, joinraise, &st, err, sizeof err))
    {
      rep(1, "%s: %s", jfiles[f].shortname, err);
      ok = 0;
      break;
    }
    if (joinmode == JOIN_SUBTUNES)
      rep(0, "%s -> subtune%s %02X%s%s", jfiles[f].shortname, st.sublast > st.subfirst ? "s" : "", st.subfirst,
          st.sublast > st.subfirst ? "-" : "", "");
    else
      rep(0, "%s -> appended to subtune %02X", jfiles[f].shortname, st.subfirst);
    if ((joinmode == JOIN_SUBTUNES) && (st.sublast > st.subfirst))
      snprintf(report[numreport - 1] + strlen(report[numreport - 1]), 16, "%02X", st.sublast);
    rep(0, "   instruments %d shared, %d new; patterns %d shared, %d new; table rows W%d P%d F%d S%d new%s",
        st.insreused, st.insnew, st.pattreused, st.pattnew, st.tabnew[0], st.tabnew[1], st.tabnew[2], st.tabnew[3],
        st.tempofix ? "; tempo command added" : "");
    if (st.raised) rep(0, "   the song now has %d SIDs", st.raised);
    if (src.multiplier != editorInfo.multiplier) rep(0, "   note: speed multiplier %dx (this song %dx) - plays at this song's speed", src.multiplier, editorInfo.multiplier);
    if (src.adparam != editorInfo.adparam) rep(0, "   note: hard restart ADSR %04X (this song %04X)", src.adparam, editorInfo.adparam);
  }
  if (ok)
  {
    int insafter = 0;
    for (i = 1; i < MAX_INSTR - 1; i++)
      if (!instrempty(i)) insafter++;
    countpatternlengths();
    rep(0, "Result: %d subtunes, %d instruments (of 62), %d patterns, tables W%d P%d F%d S%d (of 255)",
        countsongs(), insafter, highestusedpattern + 1, gettablelen(0), gettablelen(1), gettablelen(2), gettablelen(3));
  }
  if ((dry) || (!ok)) snap_restore(&keep);
  (void)instrbefore;
  return ok;
}

static int addfile(const char *path)
{
  JOINFILE *j;
  if (numjfiles >= JOIN_MAXFILES) return 0;
  j = &jfiles[numjfiles++];
  memset(j, 0, sizeof *j);
  snprintf(j->path, MAX_PATHNAME, "%s", path);
  snprintf(j->shortname, sizeof j->shortname, "%s", basename_of(path));
  {
    static SRCSONG src;
    char err[200];
    if (loadsource(path, &src, err, sizeof err))
    {
      int i, n = 0;
      j->ok = 1;
      for (i = 1; i < MAX_INSTR - 1; i++)
        if ((src.instr[i].ad) || (src.instr[i].sr) || (src.instr[i].ptr[0]) || (src.instr[i].name[0])) n++;
      snprintf(j->info, sizeof j->info, "%d SID%s, %d subtune%s, %d instruments", src.numsids, src.numsids > 1 ? "s" : "",
               src.numsongs, src.numsongs > 1 ? "s" : "", n);
    }
    else snprintf(j->info, sizeof j->info, "cannot be read: %s", err);
  }
  return 1;
}

// backup next to the song: NAME_before-join_YYYY-MM-DD_HHMM.sng
static int writebackup(const char *dir, char *out, int outsize)
{
  char base[MAX_FILENAME], *dot;
  const char *ext;
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  int ggt = (numsids > 2) || ((loadedsongfilename[0]) && (strstr(loadedsongfilename, ".ggt") || strstr(loadedsongfilename, ".GGT")));
  snprintf(base, sizeof base, "%s", loadedsongfilename[0] ? basename_of(loadedsongfilename) : "untitled");
  dot = strrchr(base, '.');
  if (dot) *dot = 0;
  ext = ggt ? ".ggt" : ".sng";
  snprintf(out, outsize, "%s%s%s_before-join_%04d-%02d-%02d_%02d%02d%s", dir, dir[0] ? "/" : "", base,
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, ext);
  return writesongfile(out, ggt);
}

static void songdir(char *dir, int size)
{
  char cwd[MAX_PATHNAME];
  dir[0] = 0;
  if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
  if (loadedsongfilename[0])
  {
    const char *b = basename_of(loadedsongfilename);
    if ((loadedsongfilename[0] == '/') || (loadedsongfilename[0] == '\\') || (loadedsongfilename[1] == ':'))
      snprintf(dir, size, "%.*s", (int)(b - loadedsongfilename - 1 > 0 ? b - loadedsongfilename - 1 : 0), loadedsongfilename);
    else if (b != loadedsongfilename)
      snprintf(dir, size, "%s/%.*s", cwd, (int)(b - loadedsongfilename - 1), loadedsongfilename);
    else snprintf(dir, size, "%s", cwd);
  }
  else snprintf(dir, size, "%s", cwd);
}

// the final join: backup, join, editor state
static int dojoin(char *msg, int msgsize, int backup)
{
  char dir[MAX_PATHNAME], bak[MAX_PATHNAME + MAX_FILENAME + 64];
  bak[0] = 0;
  stopsong();
  if (backup)
  {
    songdir(dir, sizeof dir);
    if (!writebackup(dir, bak, sizeof bak))
    {
      snprintf(msg, msgsize, "Could not write the backup %s - nothing was changed", bak);
      return 0;
    }
  }
  if (!joinall(0))
  {
    snprintf(msg, msgsize, "%s", numreport ? report[numreport - 1] : "Join failed");
    return 0;
  }
  undoInitAllAreas();
  songmodified = 1;
  snprintf(msg, msgsize, "Joined %d song%s%s%s", numjfiles, numjfiles > 1 ? "s" : "", bak[0] ? ". Backup: " : "",
           bak[0] ? basename_of(bak) : "");
  return 1;
}

int joinsongs_cli(const char *files, int mode, int raise, int backup, char *msg, int msgsize)
{
  char tmp[4096], *p, *q;
  int ok, k;
  numjfiles = 0;
  joinmode = mode;
  joinraise = raise;
  snprintf(tmp, sizeof tmp, "%s", files);
  for (p = tmp; p && *p; p = q)
  {
    q = strchr(p, ',');
    if (q) *q++ = 0;
    if (*p) addfile(p);
  }
  ok = dojoin(msg, msgsize, backup);
  for (k = 0; k < numreport; k++) fprintf(stderr, "%s\n", report[k]);
  return ok;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

void joinsongsdialog(void)
{
  int w = 110, h = 40;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int sel = 0, cur = 0, dirty = 1, c;
  char cwd[MAX_PATHNAME], msg[200] = "";
  static char addpath[MAX_PATHNAME] = "";
  static char addname[MAX_FILENAME] = "";
  enum { B_ADD = 0, B_REMOVE, B_UP, B_DOWN, B_JOIN, B_CANCEL, B_NUM };
  static const char *btntext[B_NUM] = {" Add song... ", " Remove ", " Up ", " Down ", "  Join  ", " Cancel "};
  int btnx[B_NUM];

  if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
  if (!addpath[0]) snprintf(addpath, sizeof addpath, "%s", songpath);
  stopsong();
  numjfiles = 0;
  key = rawkey = 0;
  {
    int x = x0 + 3;
    for (c = 0; c < B_NUM; c++)
    {
      btnx[c] = x;
      x += strlen(btntext[c]) + 2;
    }
    btnx[B_JOIN] = x0 + w - 24;
    btnx[B_CANCEL] = x0 + w - 13;
  }

  for (;;)
  {
    char buf[200];
    int hitfile = -1, hitopt = -1, hitbtn = -1, nsel;
    int listy = y0 + 3, opty = y0 + 3 + JOIN_MAXFILES + 1, repy = opty + 3;

    if (dirty)
    {
      joinall(1);
      dirty = 0;
    }
    // selection: 0..numjfiles-1 files, then 2 options, then buttons
    nsel = numjfiles + 2 + B_NUM;
    if (sel >= nsel) sel = nsel - 1;

    printstatus();
    dlg_drawframe(x0, y0, w, h, "Join Songs");
    printtext(x0 + 3, y0 + 2, MKCOL(14, C_PANEL), "Songs to join (in this order):");
    for (c = 0; c < JOIN_MAXFILES; c++)
    {
      int ry = listy + c, isel = (sel == c) && (c < numjfiles);
      int bg = isel ? C_SELECT : (c == cur && c < numjfiles ? C_BEATROW : C_PANEL);
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      if (c < numjfiles)
      {
        sprintf(buf, "%2d. %-36.36s %s", c + 1, jfiles[c].shortname, jfiles[c].info);
        printtext(x0 + 3, ry, MKCOL(isel ? 15 : (jfiles[c].ok ? 8 : 10), bg), buf);
        if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hitfile = c;
      }
      else if (c == numjfiles)
        printtext(x0 + 3, ry, MKCOL(3, bg), "    (Add song... or INS: choose a .sng or .ggt file)");
    }
    for (c = 0; c < 2; c++)
    {
      int s = numjfiles + c, isel = (sel == s), bg = isel ? C_SELECT : C_PANEL, ry = opty + c;
      if (c == 0)
      {
        if (joinmode == JOIN_SUBTUNES) sprintf(buf, "Join as     " GS_PREV " New subtunes (every subtune of every song) " GS_NEXT);
        else sprintf(buf, "Join as     " GS_PREV " Medley: appended to the end of subtune %02X " GS_NEXT, editorInfo.esnum);
      }
      else sprintf(buf, "[%c] Raise the number of SIDs if a song needs more", joinraise ? 'x' : ' ');
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hitopt = c;
    }
    printtext(x0 + 3, repy, MKCOL(14, C_PANEL), reporterror ? "Check: the songs cannot be joined" : "Check:");
    for (c = 0; c < numreport && c < 12; c++)
      printtext(x0 + 5, repy + 1 + c, MKCOL((reporterror && c == numreport - 1) ? 10 : (!strncmp(report[c], "Result", 6) ? 13 : 8), C_PANEL), report[c]);
    printtext(x0 + 3, y0 + h - 5, MKCOL(3, C_PANEL),
              "Identical instruments, table programs and patterns are shared. A backup of the current song is saved first.");
    if (msg[0]) printtext(x0 + 3, y0 + h - 4, MKCOL(10, C_PANEL), msg);
    printtext(x0 + 3, y0 + h - 3, MKCOL(3, C_PANEL), "INS add, DEL remove, SHIFT+UP/DOWN move, LEFT/RIGHT options, ENTER join, ESC cancel");
    for (c = 0; c < B_NUM; c++)
    {
      int isel = (sel == numjfiles + 2 + c);
      int dis = ((c == B_JOIN) && (reporterror || !numjfiles));
      printtext(btnx[c], y0 + h - 2, MKCOL(dis ? 11 : C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), btntext[c]);
      if ((mousey == y0 + h - 2) && (mousex >= btnx[c]) && (mousex < btnx[c] + (int)strlen(btntext[c]))) hitbtn = c;
    }

    fliptoscreen();
    getkey();
    if (win_quitted) break;

    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn >= 0) { sel = numjfiles + 2 + hitbtn; rawkey = KEY_ENTER; }
      else if (hitfile >= 0) { sel = cur = hitfile; rawkey = 0; }
      else if (hitopt >= 0) { sel = numjfiles + hitopt; rawkey = KEY_RIGHT; }
      else if ((mousex < x0) || (mousex >= x0 + w) || (mousey < y0) || (mousey >= y0 + h)) break;
    }

    {
      int action = -1;
      if (rawkey == KEY_INS) action = B_ADD;
      if ((rawkey == KEY_DEL) && (sel < numjfiles)) action = B_REMOVE;
      if ((shiftpressed) && (rawkey == KEY_UP) && (cur < numjfiles)) action = B_UP;
      if ((shiftpressed) && (rawkey == KEY_DOWN) && (cur < numjfiles)) action = B_DOWN;
      if (action < 0)
      {
        switch (rawkey)
        {
          case KEY_ESC:
          goto done;
          case KEY_UP:
          if (sel > 0) sel--;
          if (sel < numjfiles) cur = sel;
          break;
          case KEY_DOWN:
          case KEY_TAB:
          if (sel < nsel - 1) sel++;
          if (sel < numjfiles) cur = sel;
          break;
          case KEY_LEFT:
          case KEY_RIGHT:
          case KEY_SPACE:
          if (sel == numjfiles) { joinmode ^= 1; dirty = 1; }
          else if (sel == numjfiles + 1) { joinraise ^= 1; dirty = 1; }
          else if (sel >= numjfiles + 2)
          {
            sel += rawkey == KEY_LEFT ? -1 : 1;
            if (sel < numjfiles + 2) sel = numjfiles + 2;
            if (sel >= nsel) sel = nsel - 1;
          }
          break;
          case KEY_ENTER:
          if (sel >= numjfiles + 2) action = sel - numjfiles - 2;
          else action = B_JOIN;
          break;
        }
      }
      msg[0] = 0;
      switch (action)
      {
        case B_ADD:
        if (numjfiles >= JOIN_MAXFILES)
        {
          snprintf(msg, sizeof msg, "At most %d songs at a time", JOIN_MAXFILES);
          break;
        }
        if (fileselector(addname, addpath, songfilter, "JOIN SONGS: ADD A SONG (.sng / .ggt)", 0))
        {
          char dir[MAX_PATHNAME], full[MAX_PATHNAME + MAX_FILENAME + 2];
          if (!getcwd(dir, sizeof dir)) dir[0] = 0;
          snprintf(full, sizeof full, "%s/%s", dir, addname);
          addfile(full);
          sel = cur = numjfiles - 1;
          dirty = 1;
        }
        if (cwd[0]) chdir(cwd);     // the current song keeps its folder
        key = rawkey = 0;
        break;

        case B_REMOVE:
        if (cur < numjfiles)
        {
          memmove(&jfiles[cur], &jfiles[cur + 1], (numjfiles - cur - 1) * sizeof(JOINFILE));
          numjfiles--;
          if (cur >= numjfiles) cur = numjfiles ? numjfiles - 1 : 0;
          if (sel >= numjfiles && sel < numjfiles + 1) sel = cur;
          dirty = 1;
        }
        break;

        case B_UP:
        case B_DOWN:
        {
          int o = cur + (action == B_UP ? -1 : 1);
          if ((cur < numjfiles) && (o >= 0) && (o < numjfiles))
          {
            JOINFILE tmp = jfiles[cur];
            jfiles[cur] = jfiles[o];
            jfiles[o] = tmp;
            cur = o;
            if (sel < numjfiles) sel = o;
            dirty = 1;
          }
        }
        break;

        case B_JOIN:
        if ((!numjfiles) || (reporterror)) break;
        {
          char m[300];
          if (dojoin(m, sizeof m, 1))
          {
            const char *lines[26];
            int k;
            setstatusmessage(m);
            for (k = 0; k < numreport && k < 24; k++) lines[k] = report[k];
            lines[k++] = "";
            lines[k++] = m;
            dlg_message("Join Songs", lines, k);
            goto done;
          }
          snprintf(msg, sizeof msg, "%s", m);
        }
        break;

        case B_CANCEL:
        goto done;
      }
    }
  }
done:
  key = rawkey = 0;
  printmainscreen();
}

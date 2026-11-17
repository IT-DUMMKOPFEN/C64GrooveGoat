//
// C64 GrooveGoat - the orderlist editor (CTRL+O)
//
// All voices of all SIDs side by side, the orderlist positions downwards.
// Every voice keeps its own length and its own loop position, as in
// GoatTracker. A cell is one pattern of the voice with its transpose (the one
// in effect, bright where it is set) and its repeat count. A rectangle of
// cells can be copied, cut, pasted, duplicated, inserted, deleted, moved,
// transposed and repeated; shared patterns can be made unique.
//
// The orderlists are read into steps (pattern, transpose, repeat) and written
// back (only the voices that changed), so the editor never has to deal with
// the transpose and repeat commands themselves.
//

#include "goattrk2.h"
#include "gdialog.h"
#include "gres.h"

typedef struct
{
  unsigned char p;          // pattern
  signed char tr;           // transpose in effect (-16 ... +14)
  unsigned char rep;        // played 1 ... 16 times
  unsigned char trset;      // a transpose command stands before it (display)
} STEP;

static STEP vs[MAX_CHN][MAX_SONGLEN];
static int vn[MAX_CHN], vloop[MAX_CHN], vtrans[MAX_CHN];
static int sub;

// the clipboard: a rectangle of steps (the columns can be of different height)
static STEP clip[MAX_CHN][MAX_SONGLEN];
static int clipn[MAX_CHN], clipw = 0;

// the editor's own undo: whole states of the subtune's orderlists and the patterns
typedef struct
{
  unsigned char order[MAX_CHN][MAX_SONGLEN + 2];
  int len[MAX_CHN];
  unsigned char patt[MAX_PATT][MAX_PATTROWS * 4 + 4];
  int plen[MAX_PATT];
} OESTATE;
#define OE_UNDO 40
static OESTATE *undo[OE_UNDO];
static int nundo = 0, nredo = 0, undopos = 0;

static int entryframes[MAX_CHN][MAX_SONGLEN + 2];
static int timesdirty = 1, totalframes = 0;

static char msg[160];
static int msgcol = 8;

// ---------------------------------------------------------------------------
// Orderlist <-> steps
// ---------------------------------------------------------------------------

static void decode(int c)
{
  int d, tr = 0, rep = 0, n = 0, trset = 0, loopentry = songorder[sub][c][songlen[sub][c] + 1];
  vloop[c] = 0;
  vtrans[c] = 0;
  for (d = 0; (d < songlen[sub][c]) && (n < MAX_SONGLEN); d++)
  {
    int v = songorder[sub][c][d];
    if ((v >= TRANSDOWN) && (v < LOOPSONG))
    {
      tr = v - TRANSUP;
      trset = 1;
      vtrans[c] = 1;
      continue;
    }
    if ((v >= REPEAT) && (v < TRANSDOWN))
    {
      rep = v - REPEAT;
      continue;
    }
    if (v >= MAX_PATT) continue;
    vs[c][n].p = v;
    vs[c][n].tr = tr;
    vs[c][n].rep = rep + 1;
    vs[c][n].trset = trset;
    n++;
    rep = 0;
    trset = 0;
  }
  vn[c] = n;
  // the loop: the step whose commands start at the loop entry
  {
    int k = 0;
    vloop[c] = 0;
    for (d = 0; d < songlen[sub][c]; d++)
    {
      if (d == loopentry) { vloop[c] = k < n ? k : 0; break; }
      if (songorder[sub][c][d] < MAX_PATT) k++;
    }
  }
  if (!n)
  {
    vs[c][0].p = 0;
    vs[c][0].tr = 0;
    vs[c][0].rep = 1;
    vs[c][0].trset = 0;
    vn[c] = 1;
  }
}

static int encode(int c, int write)
{
  unsigned char o[MAX_SONGLEN + 8];
  int k, n = 0, cur = 0, loopentry = 0, lasttr = vn[c] ? vs[c][vn[c] - 1].tr : 0;
  for (k = 0; k < vn[c]; k++)
  {
    const STEP *s = &vs[c][k];
    if (n > MAX_SONGLEN) return 0;
    if (k == vloop[c]) loopentry = n;
    // a transpose command where it changes, and at the loop position (the
    // end of the voice may leave another transpose in effect)
    if ((s->tr != cur) || ((k == vloop[c]) && (lasttr != s->tr)))
    {
      o[n++] = TRANSUP + s->tr;
      cur = s->tr;
    }
    if (s->rep > 1) o[n++] = REPEAT + s->rep - 1;
    o[n++] = s->p;
  }
  if (n > MAX_SONGLEN) return 0;
  if (write)
  {
    memcpy(songorder[sub][c], o, n);
    songorder[sub][c][n] = LOOPSONG;
    songorder[sub][c][n + 1] = loopentry;
    songlen[sub][c] = n;
  }
  return n;
}

static void decodeall(void)
{
  int c;
  for (c = 0; c < numchannels; c++) decode(c);
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

static void takestate(OESTATE *s)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
  {
    memcpy(s->order[c], songorder[sub][c], MAX_SONGLEN + 2);
    s->len[c] = songlen[sub][c];
  }
  memcpy(s->patt, pattern, sizeof s->patt);
  memcpy(s->plen, pattlen, sizeof s->plen);
}

static void givestate(const OESTATE *s)
{
  int c;
  snd_lock();
  for (c = 0; c < MAX_CHN; c++)
  {
    memcpy(songorder[sub][c], s->order[c], MAX_SONGLEN + 2);
    songlen[sub][c] = s->len[c];
  }
  memcpy(pattern, s->patt, sizeof s->patt);
  memcpy(pattlen, s->plen, sizeof s->plen);
  snd_unlock();
  decodeall();
  timesdirty = 1;
}

// before a change: the state now goes on the undo stack
static void pushundo(void)
{
  int k;
  if (nundo == OE_UNDO)
  {
    OESTATE *t = undo[0];
    for (k = 1; k < OE_UNDO; k++) undo[k - 1] = undo[k];
    undo[OE_UNDO - 1] = t;
    nundo--;
  }
  if (!undo[nundo]) undo[nundo] = (OESTATE *)malloc(sizeof(OESTATE));
  if (undo[nundo]) takestate(undo[nundo]);
  nundo++;
  nredo = 0;
  undopos = nundo;
}

static OESTATE *redostate = NULL;

static void doundo(void)
{
  if (!nundo) { snprintf(msg, sizeof msg, "Nothing to undo"); return; }
  if (!redostate) redostate = (OESTATE *)malloc(sizeof(OESTATE));
  // keep the current state for redo (one step)
  if (redostate) takestate(redostate);
  nundo--;
  givestate(undo[nundo]);
  nredo = redostate ? 1 : 0;
  snprintf(msg, sizeof msg, "Undone (%d more)", nundo);
}

static void doredo(void)
{
  if (!nredo) { snprintf(msg, sizeof msg, "Nothing to redo"); return; }
  pushundo();
  givestate(redostate);
  nredo = 0;
  snprintf(msg, sizeof msg, "Redone");
}

// after a change: write the voices back
static int commit(const int *dirty)
{
  int c;
  for (c = 0; c < numchannels; c++)
    if ((dirty[c]) && (!encode(c, 0)))
    {
      snprintf(msg, sizeof msg, "Voice %d.%d: the orderlist would be too long (max. %d entries) - nothing changed", c / 3 + 1, c % 3 + 1,
               MAX_SONGLEN);
      msgcol = 10;
      givestate(undo[--nundo]);
      return 0;
    }
  snd_lock();
  for (c = 0; c < numchannels; c++)
    if (dirty[c]) encode(c, 1);
  countpatternlengths();
  snd_unlock();
  decodeall();
  songmodified = 1;
  timesdirty = 1;
  return 1;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

static int pattisempty(int p)
{
  int r;
  for (r = 0; r < pattlen[p]; r++)
    if ((pattern[p][r * 4] != REST) || (pattern[p][r * 4 + 1]) || (pattern[p][r * 4 + 2]) || (pattern[p][r * 4 + 3])) return 0;
  return 1;
}

// how often the pattern stands in all orderlists of all subtunes
static int pattuses(int p)
{
  int s2, c, d, n = 0;
  for (s2 = 0; s2 < MAX_SONGS; s2++)
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < songlen[s2][c]; d++)
        if (songorder[s2][c][d] == p) n++;
  return n;
}

// a free pattern (empty, unused), with the pattern length of the song
static int freepattern(const unsigned char *taken)
{
  int p;
  for (p = 0; p < MAX_PATT; p++)
  {
    if ((taken) && (taken[p])) continue;
    if ((pattisempty(p)) && (!pattuses(p)))
    {
      int c, onscreen = 0;
      for (c = 0; c < MAX_CHN; c++)
        if (editorUndoInfo.editorInfo[c].epnum == p) onscreen = 1;
      if ((onscreen) && (p == 0)) continue;
      clearpattern(p);
      pattlen[p] = defaultpatternlength;
      return p;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

static const int pattcols[10] = {2, 5, 7, 10, 13, 14, 3, 4, 8, 12};

#define OE_TOP 5
#define OE_ROWS 41

static int curv = 0, curr = 0, ancv = 0, ancr = 0, hassel = 0, viewr = 0, viewsid = -1;
static int hexdigit = -1;

static void selrect(int *v0, int *v1, int *r0, int *r1)
{
  if (hassel)
  {
    *v0 = ancv < curv ? ancv : curv;
    *v1 = ancv < curv ? curv : ancv;
    *r0 = ancr < curr ? ancr : curr;
    *r1 = ancr < curr ? curr : ancr;
  }
  else
  {
    *v0 = *v1 = curv;
    *r0 = *r1 = curr;
  }
}

static int maxrows(void)
{
  int c, m = 0;
  for (c = 0; c < numchannels; c++)
    if (vn[c] > m) m = vn[c];
  return m;
}

static void copysel(int cut)
{
  int v0, v1, r0, r1, c, r, dirty[MAX_CHN];
  selrect(&v0, &v1, &r0, &r1);
  clipw = v1 - v0 + 1;
  for (c = v0; c <= v1; c++)
  {
    clipn[c - v0] = 0;
    for (r = r0; (r <= r1) && (r < vn[c]); r++) clip[c - v0][clipn[c - v0]++] = vs[c][r];
  }
  if (!cut)
  {
    snprintf(msg, sizeof msg, "Copied %d positions of %d voices", r1 - r0 + 1, clipw);
    return;
  }
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = v0; c <= v1; c++)
  {
    int a = r0, b = r1 < vn[c] - 1 ? r1 : vn[c] - 1, n = b - a + 1;
    if (n <= 0) continue;
    if (n >= vn[c]) n = vn[c] - 1;     // a voice keeps at least one pattern
    if (n <= 0) continue;
    memmove(&vs[c][a], &vs[c][a + n], sizeof(STEP) * (vn[c] - a - n));
    vn[c] -= n;
    if (vloop[c] >= vn[c]) vloop[c] = 0;
    else if (vloop[c] > a) vloop[c] = vloop[c] - n > a ? vloop[c] - n : a;
    dirty[c] = 1;
  }
  if (commit(dirty)) snprintf(msg, sizeof msg, "Cut %d positions of %d voices", r1 - r0 + 1, clipw);
  hassel = 0;
}

// insert the clipboard (or overwrite with it) at row r of the voices from v
static void pasteat(int v, int r, int overwrite, const char *what)
{
  int c, k, dirty[MAX_CHN];
  if (!clipw) { snprintf(msg, sizeof msg, "The clipboard is empty - select and copy first (CTRL+C)"); return; }
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = 0; (c < clipw) && (v + c < numchannels); c++)
  {
    int vc = v + c, n = clipn[c], at = r > vn[vc] ? vn[vc] : r;
    if (!n) continue;
    if (overwrite)
    {
      if (at + n > MAX_SONGLEN) n = MAX_SONGLEN - at;
      for (k = 0; k < n; k++) vs[vc][at + k] = clip[c][k];
      if (at + n > vn[vc]) vn[vc] = at + n;
    }
    else
    {
      if (vn[vc] + n > MAX_SONGLEN) n = MAX_SONGLEN - vn[vc];
      if (n <= 0) continue;
      memmove(&vs[vc][at + n], &vs[vc][at], sizeof(STEP) * (vn[vc] - at));
      for (k = 0; k < n; k++) vs[vc][at + k] = clip[c][k];
      vn[vc] += n;
      if (vloop[vc] > at) vloop[vc] += n;
    }
    dirty[vc] = 1;
  }
  if (commit(dirty)) snprintf(msg, sizeof msg, "%s %d voices", what, clipw);
}

static void duplicate(void)
{
  int v0, v1, r0, r1;
  selrect(&v0, &v1, &r0, &r1);
  copysel(0);
  pasteat(v0, r1 + 1, 0, "Duplicated: the selection again after itself -");
  hassel = 1;
  ancv = v0;
  curv = v1;
  ancr = r1 + 1;
  curr = r1 + 1 + (r1 - r0);
}

// INS: a new empty pattern in each selected voice (DEL: delete the positions)
static void insertrows(void)
{
  int v0, v1, r0, r1, c, k, dirty[MAX_CHN];
  unsigned char taken[MAX_PATT];
  selrect(&v0, &v1, &r0, &r1);
  pushundo();
  memset(dirty, 0, sizeof dirty);
  memset(taken, 0, sizeof taken);
  for (c = v0; c <= v1; c++)
  {
    int at = r0 > vn[c] ? vn[c] : r0;
    for (k = 0; k <= r1 - r0; k++)
    {
      int p = freepattern(taken);
      if ((p < 0) || (vn[c] >= MAX_SONGLEN)) break;
      taken[p] = 1;
      memmove(&vs[c][at + 1], &vs[c][at], sizeof(STEP) * (vn[c] - at));
      vs[c][at].p = p;
      vs[c][at].tr = at < vn[c] ? vs[c][at + 1].tr : (at ? vs[c][at - 1].tr : 0);
      vs[c][at].rep = 1;
      vs[c][at].trset = 0;
      vn[c]++;
      if (vloop[c] > at) vloop[c]++;
      at++;
      dirty[c] = 1;
    }
  }
  if (commit(dirty)) snprintf(msg, sizeof msg, "Inserted new empty patterns (%d rows each)", defaultpatternlength);
}

static void deleterows(void)
{
  hassel = hassel ? 1 : 0;
  copysel(1);
  snprintf(msg, sizeof msg, "Deleted (also in the clipboard: CTRL+V brings it back)");
}

static void moverows(int d)
{
  int v0, v1, r0, r1, c, dirty[MAX_CHN];
  selrect(&v0, &v1, &r0, &r1);
  for (c = v0; c <= v1; c++)
    if ((r0 + d < 0) || (r1 + d >= vn[c])) { snprintf(msg, sizeof msg, "Cannot move further"); return; }
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = v0; c <= v1; c++)
  {
    STEP t;
    if (d < 0)
    {
      t = vs[c][r0 - 1];
      memmove(&vs[c][r0 - 1], &vs[c][r0], sizeof(STEP) * (r1 - r0 + 1));
      vs[c][r1] = t;
    }
    else
    {
      t = vs[c][r1 + 1];
      memmove(&vs[c][r0 + 1], &vs[c][r0], sizeof(STEP) * (r1 - r0 + 1));
      vs[c][r0] = t;
    }
    dirty[c] = 1;
  }
  if (commit(dirty))
  {
    ancr += d;
    curr += d;
    snprintf(msg, sizeof msg, "Moved %s", d < 0 ? "up" : "down");
  }
}

static void transpose(int d)
{
  int v0, v1, r0, r1, c, r, dirty[MAX_CHN], clamped = 0;
  selrect(&v0, &v1, &r0, &r1);
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = v0; c <= v1; c++)
    for (r = r0; (r <= r1) && (r < vn[c]); r++)
    {
      int t = vs[c][r].tr + d;
      if (t < -16) { t = -16; clamped = 1; }
      if (t > 14) { t = 14; clamped = 1; }
      vs[c][r].tr = t;
      vs[c][r].trset = 1;
      dirty[c] = 1;
    }
  // the step after the selection keeps its own transpose
  for (c = v0; c <= v1; c++)
    if (r1 + 1 < vn[c]) vs[c][r1 + 1].trset = 1;
  if (commit(dirty))
    snprintf(msg, sizeof msg, "Transposed %+d%s", d, clamped ? " (limited to -16 ... +14 half steps)" : "");
}

static void repeat(int d)
{
  int v0, v1, r0, r1, c, r, dirty[MAX_CHN];
  selrect(&v0, &v1, &r0, &r1);
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = v0; c <= v1; c++)
    for (r = r0; (r <= r1) && (r < vn[c]); r++)
    {
      int t = vs[c][r].rep + d;
      if (t < 1) t = 1;
      if (t > 16) t = 16;
      vs[c][r].rep = t;
      dirty[c] = 1;
    }
  if (commit(dirty)) snprintf(msg, sizeof msg, "Repeat %s", d > 0 ? "more often" : "less often");
}

// U: the patterns of the selection that are also used elsewhere get copies
static void makeunique(void)
{
  int v0, v1, r0, r1, c, r, dirty[MAX_CHN], map[MAX_PATT], made = 0;
  int insel[MAX_PATT];
  unsigned char taken[MAX_PATT];
  selrect(&v0, &v1, &r0, &r1);
  memset(insel, 0, sizeof insel);
  for (c = v0; c <= v1; c++)
    for (r = r0; (r <= r1) && (r < vn[c]); r++) insel[vs[c][r].p] += vs[c][r].rep ? 1 : 1;
  pushundo();
  memset(dirty, 0, sizeof dirty);
  memset(taken, 0, sizeof taken);
  for (r = 0; r < MAX_PATT; r++) map[r] = -1;
  for (c = v0; c <= v1; c++)
    for (r = r0; (r <= r1) && (r < vn[c]); r++)
    {
      int p = vs[c][r].p;
      if (pattuses(p) <= insel[p]) continue;           // only used here
      if (map[p] < 0)
      {
        int q = freepattern(taken);
        if (q < 0) { snprintf(msg, sizeof msg, "No free pattern left"); msgcol = 10; break; }
        taken[q] = 1;
        memcpy(pattern[q], pattern[p], MAX_PATTROWS * 4 + 4);
        pattlen[q] = pattlen[p];
        map[p] = q;
        made++;
      }
      vs[c][r].p = map[p];
      dirty[c] = 1;
    }
  if (!made) { nundo--; snprintf(msg, sizeof msg, "The patterns of the selection are used only here - nothing to do"); return; }
  if (commit(dirty)) snprintf(msg, sizeof msg, "%d shared patterns copied - the selection now has its own", made);
}

// N: a new empty pattern in the cell
static void newpattern(void)
{
  int dirty[MAX_CHN], p;
  pushundo();
  p = freepattern(NULL);
  if (p < 0) { nundo--; snprintf(msg, sizeof msg, "No free pattern left"); msgcol = 10; return; }
  memset(dirty, 0, sizeof dirty);
  if (curr >= vn[curv])
  {
    curr = vn[curv];
    vs[curv][curr].tr = curr ? vs[curv][curr - 1].tr : 0;
    vs[curv][curr].rep = 1;
    vs[curv][curr].trset = 0;
    vn[curv]++;
  }
  vs[curv][curr].p = p;
  dirty[curv] = 1;
  if (commit(dirty)) snprintf(msg, sizeof msg, "New empty pattern %02X (%d rows)", p, pattlen[p]);
}

static void setcell(int p)
{
  int dirty[MAX_CHN];
  if (curr > vn[curv]) curr = vn[curv];
  if (curr >= MAX_SONGLEN) return;
  pushundo();
  memset(dirty, 0, sizeof dirty);
  if (curr == vn[curv])
  {
    vs[curv][curr].tr = curr ? vs[curv][curr - 1].tr : 0;
    vs[curv][curr].rep = 1;
    vs[curv][curr].trset = 0;
    vn[curv]++;
  }
  vs[curv][curr].p = p;
  dirty[curv] = 1;
  commit(dirty);
}

static void setloop(void)
{
  int v0, v1, r0, r1, c, dirty[MAX_CHN];
  selrect(&v0, &v1, &r0, &r1);
  pushundo();
  memset(dirty, 0, sizeof dirty);
  for (c = v0; c <= v1; c++)
  {
    vloop[c] = r0 < vn[c] ? r0 : vn[c] - 1;
    dirty[c] = 1;
  }
  if (commit(dirty)) snprintf(msg, sizeof msg, "Loop: the song continues at position %02X after the end", r0);
}

static void playfrom(int r)
{
  int c;
  for (c = 0; c < numchannels; c++)
  {
    int k, e = 0, steps = 0;
    // the orderlist entry of step r (its commands first)
    for (k = 0; k < songlen[sub][c]; k++)
    {
      if (steps == (r < vn[c] ? r : vn[c] - 1)) { e = k; break; }
      if (songorder[sub][c][k] < MAX_PATT) steps++;
    }
    editorUndoInfo.editorInfo[c].espos = e;
  }
  editorInfo.esnum = sub;
  followplay = 1;
  initsong(sub, PLAY_POS);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static int stepframe(int c, int r)
{
  int k, steps = 0;
  if ((r < 0) || (r >= vn[c])) return -1;
  for (k = 0; k < songlen[sub][c]; k++)
  {
    if (songorder[sub][c][k] < MAX_PATT)
    {
      if (steps == r) return entryframes[c][k];
      steps++;
    }
  }
  return -1;
}

static int playstep(int c)
{
  int k, steps = 0, sp;
  if ((!isplaying()) || (psnum != sub)) return -1;
  sp = chn[c].songptr - 1;
  for (k = 0; k < songlen[sub][c]; k++)
  {
    if (songorder[sub][c][k] < MAX_PATT)
    {
      if (k == sp) return steps;
      steps++;
    }
  }
  return -1;
}

static void fmttime(char *out, int frames)
{
  int fps = (editorInfo.ntsc ? 60 : 50) * (editorInfo.multiplier ? editorInfo.multiplier : 1);
  int s = frames / fps;
  if (frames < 0) sprintf(out, "  -  ");
  else sprintf(out, "%2d:%02d", s / 60, s % 60);
}

typedef struct
{
  int v0, v1, cw, x0, side;
} LAYOUT;

static void layout(LAYOUT *l)
{
  int nv, avail;
  if ((viewsid >= 0) && (viewsid * 3 >= numchannels)) viewsid = -1;
  l->v0 = viewsid < 0 ? 0 : viewsid * 3;
  l->v1 = viewsid < 0 ? numchannels - 1 : viewsid * 3 + 2;
  nv = l->v1 - l->v0 + 1;
  l->x0 = 12;
  l->side = nv <= 12;
  avail = MAX_COLUMNS - l->x0 - (l->side ? 32 : 1) - (nv / 3);
  l->cw = avail / nv;
  if (l->cw > 10) l->cw = 10;
  if (l->cw < 4) l->cw = 4;
}

static int colx(const LAYOUT *l, int v)
{
  return l->x0 + (v - l->v0) * l->cw + (v - l->v0) / 3;
}

enum { OB_COPY = 1, OB_CUT, OB_PASTE, OB_DUP, OB_INS, OB_DEL, OB_UP, OB_DOWN, OB_T12M, OB_T1M, OB_T1P, OB_T12P, OB_REP, OB_UNIQUE,
       OB_NEW, OB_LOOP, OB_PLAY, OB_STOP, OB_UNDO, OB_REDO, OB_CLOSE, OB_ROWSM, OB_ROWSP, OB_ROWS, OB_SUBM, OB_SUBP, OB_VIEW, OB_NUM };

typedef struct { int x, y, w, id; } HIT;
static HIT hits[64];
static int nhits;

static int button(int x, int y, const char *label, int id, int hi)
{
  int w = (int)strlen(label);
  printtext(x, y, MKCOL(C_BUTTONTEXT, hi ? C_BUTTONHI : C_BUTTON), label);
  if (nhits < 64) { hits[nhits].x = x; hits[nhits].y = y; hits[nhits].w = w; hits[nhits].id = id; nhits++; }
  return x + w + 1;
}

static void draw(const LAYOUT *l)
{
  int x, y, c, r, v0, v1, r0, r1;
  char buf[200];
  int hdr = MKCOL(C_HEADERTEXT, C_HEADER);
  nhits = 0;
  selrect(&v0, &v1, &r0, &r1);
  fillarea(0, 0, MAX_COLUMNS, MAX_ROWS, MKCOL(8, C_PANEL), ' ');
  snprintf(tip_context, sizeof tip_context, "Orderlist editor");
  tip_newframe();

  // title row
  printblankc(0, 0, hdr, MAX_COLUMNS);
  printtext(1, 0, MKCOL(7, C_HEADER), "ORDERLIST EDITOR");
  {
    int mr = maxrows(), fps = (editorInfo.ntsc ? 60 : 50) * (editorInfo.multiplier ? editorInfo.multiplier : 1);
    x = 19;
    printtext(x, 0, hdr, "Subtune");
    x = button(x + 8, 0, GS_PREV, OB_SUBM, 0);
    sprintf(buf, "%02X", sub);
    printtext(x - 1, 0, MKCOL(7, C_HEADER), buf);
    x = button(x + 1, 0, GS_NEXT, OB_SUBP, 0);
    snprintf(buf, sizeof buf, "%d SID%s, %d voices, up to %d positions, %d:%02d", numsids, numsids > 1 ? "s" : "", numchannels, mr,
             totalframes / fps / 60, totalframes / fps % 60);
    printtext(x + 1, 0, hdr, buf);
    x = 86;
    printtext(x, 0, hdr, "Rows of new patterns");
    x = button(x + 21, 0, GS_PREV, OB_ROWSM, 0);
    sprintf(buf, " %3d ", defaultpatternlength);
    x = button(x - 1, 0, buf, OB_ROWS, 0);
    x = button(x - 1, 0, GS_NEXT, OB_ROWSP, 0);
    button(x + 2, 0, viewsid < 0 ? " All SIDs " : " One SID ", OB_VIEW, 0);
  }

  // tool buttons
  printblankc(0, 1, MKCOL(8, C_PANEL), MAX_COLUMNS);
  x = 1;
  x = button(x, 1, " Copy ", OB_COPY, 0);
  x = button(x, 1, " Cut ", OB_CUT, 0);
  x = button(x, 1, " Paste ", OB_PASTE, 0);
  x = button(x, 1, " Duplicate ", OB_DUP, 1) + 1;
  x = button(x, 1, " Insert ", OB_INS, 0);
  x = button(x, 1, " Delete ", OB_DEL, 0);
  x = button(x, 1, " " GS_UP " ", OB_UP, 0);
  x = button(x, 1, " " GS_DOWN " ", OB_DOWN, 0) + 1;
  x = button(x, 1, " -12 ", OB_T12M, 0);
  x = button(x, 1, " -1 ", OB_T1M, 0);
  x = button(x, 1, " +1 ", OB_T1P, 0);
  x = button(x, 1, " +12 ", OB_T12P, 0);
  x = button(x, 1, " Repeat ", OB_REP, 0) + 1;
  x = button(x, 1, " Make unique ", OB_UNIQUE, 0);
  x = button(x, 1, " New pattern ", OB_NEW, 0);
  x = button(x, 1, " Loop here ", OB_LOOP, 0) + 1;
  printblankc(0, 2, MKCOL(8, C_PANEL), MAX_COLUMNS);
  x = 1;
  x = button(x, 2, isplaying() ? " " GS_STOP " Stop " : " " GS_PLAY " Play from here ", isplaying() ? OB_STOP : OB_PLAY, 0) + 1;
  x = button(x, 2, " Undo ", OB_UNDO, 0);
  x = button(x, 2, " Redo ", OB_REDO, 0);
  button(MAX_COLUMNS - 9, 2, " Close ", OB_CLOSE, 0);

  // headers
  for (c = l->v0; c <= l->v1; c++)
  {
    x = colx(l, c);
    if ((c % 3 == 0))
    {
      snprintf(buf, sizeof buf, "SID %d  $%04X", c / 3 + 1, sidaddr[c / 3]);
      buf[l->cw * 3] = 0;
      printtext(x, 3, MKCOL(7, C_PANEL), buf);
    }
    sprintf(buf, "%d.%d", c / 3 + 1, c % 3 + 1);
    printblankc(x, 4, hdr, l->cw - 1);
    printtext(x + 1, 4, MKCOL(c == curv ? 7 : C_HEADERTEXT, C_HEADER), buf);
    if ((c % 3 == 0) && (c > l->v0)) printglyph(x - 1, 4, MKCOL(14, C_PANEL), GS_VLINE[0]);
  }
  printblankc(0, 4, hdr, l->x0 - 1);
  snprintf(buf, sizeof buf, "Pos Time%d.%d", curv / 3 + 1, curv % 3 + 1);
  printtext(0, 4, hdr, "Pos");
  printtext(4, 4, hdr, "Time");

  // rows
  {
    int mr = maxrows();
    int play[MAX_CHN];
    for (c = 0; c < numchannels; c++) play[c] = playstep(c);
    for (y = 0; y < OE_ROWS; y++)
    {
      int row = viewr + y, sy = OE_TOP + y, anyplay = 0;
      if (row > mr) break;
      for (c = l->v0; c <= l->v1; c++)
        if (play[c] == row) anyplay = 1;
      sprintf(buf, "%02X", row);
      printtext(0, sy, MKCOL(anyplay ? 13 : 3, C_PANEL), buf);
      fmttime(buf, stepframe(curv, row));
      printtext(4, sy, MKCOL(3, C_PANEL), buf);
      for (c = l->v0; c <= l->v1; c++)
      {
        int cx = colx(l, c), sel = (c >= v0) && (c <= v1) && (row >= r0) && (row <= r1), cur = (c == curv) && (row == curr);
        int bg = cur ? C_SELECT : (sel ? C_BUTTON : (play[c] == row ? C_PLAYROW : C_PANEL));
        if ((c % 3 == 0) && (c > l->v0)) printglyph(cx - 1, sy, MKCOL(14, C_PANEL), GS_VLINE[0]);
        printblankc(cx, sy, MKCOL(8, bg), l->cw - 1);
        if (row < vn[c])
        {
          const STEP *s = &vs[c][row];
          int col = pattisempty(s->p) ? 3 : 15;
          char t[16] = "";
          printglyph(cx, sy, MKCOL(pattcols[s->p % 10], bg), GS_LEFTH[0]);
          sprintf(buf, "%02X", s->p);
          if ((cur) && (hexdigit >= 0)) sprintf(buf, "%X_", hexdigit);
          printtext(cx + 1, sy, MKCOL(cur ? 15 : col, bg), buf);
          if (s->tr) sprintf(t, "%+d", s->tr);
          if ((t[0]) && (l->cw >= 6))
          {
            t[l->cw - 4] = 0;
            printtext(cx + 3, sy, MKCOL(((!row) || (vs[c][row - 1].tr != s->tr)) ? 14 : 6, bg), t);
          }
          if ((s->rep > 1) && (l->cw >= 6 + (int)strlen(t)))
          {
            sprintf(buf, "x%d", s->rep);
            printtext(cx + 3 + (int)strlen(t), sy, MKCOL(8, bg), buf);
          }
          if ((row == vloop[c]) && (l->cw >= 6)) printglyph(cx + l->cw - 2, sy, MKCOL(10, bg), '<');
          else if ((row == vloop[c]) && (row) && (!cur)) printtext(cx + 1, sy, MKCOL(10, bg), buf);
        }
        else if (row == vn[c])
        {
          snprintf(buf, sizeof buf, "RST%02X", vloop[c]);
          buf[l->cw - 1] = 0;
          printtext(cx, sy, MKCOL(10, bg), buf);
        }
        else if (cur) printtext(cx, sy, MKCOL(3, bg), "...");
      }
    }
  }

  // side panel
  if (l->side)
  {
    int sx = MAX_COLUMNS - 31, sy = 3, p;
    const STEP *s = curr < vn[curv] ? &vs[curv][curr] : NULL;
    fillarea(sx - 1, 3, 32, OE_ROWS + 2, MKCOL(8, C_HEADER), ' ');
    printtext(sx, sy++, MKCOL(7, C_HEADER), "Selection");
    snprintf(buf, sizeof buf, "Positions  %02X-%02X (%d)", r0, r1, r1 - r0 + 1);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    snprintf(buf, sizeof buf, "Voices     %d.%d-%d.%d (%d)", v0 / 3 + 1, v0 % 3 + 1, v1 / 3 + 1, v1 % 3 + 1, v1 - v0 + 1);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    snprintf(buf, sizeof buf, "Clipboard  %s", clipw ? "" : "empty");
    if (clipw) snprintf(buf, sizeof buf, "Clipboard  %d voices", clipw);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    sy++;
    snprintf(buf, sizeof buf, "Voice %d.%d", curv / 3 + 1, curv % 3 + 1);
    printtext(sx, sy++, MKCOL(7, C_HEADER), buf);
    snprintf(buf, sizeof buf, "Length     %d positions", vn[curv]);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    snprintf(buf, sizeof buf, "Loop to    %02X", vloop[curv]);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    snprintf(buf, sizeof buf, "Entries    %d of %d", songlen[sub][curv], MAX_SONGLEN);
    printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
    printtext(sx, sy++, MKCOL(3, C_HEADER), "The Time column: this voice");
    sy++;
    if (s)
    {
      int r, n = pattuses(s->p), shown = 0;
      snprintf(buf, sizeof buf, "Pattern %02X", s->p);
      printtext(sx, sy++, MKCOL(7, C_HEADER), buf);
      snprintf(buf, sizeof buf, "Rows       %d%s", pattlen[s->p], pattisempty(s->p) ? " (empty)" : "");
      printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
      snprintf(buf, sizeof buf, "Transpose  %+d   Played %dx", s->tr, s->rep);
      printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
      {
        int o = snprintf(buf, sizeof buf, "Used %dx:", n), c2, d;
        for (c2 = 0; (c2 < numchannels) && (shown < 4); c2++)
          for (d = 0; (d < vn[c2]) && (shown < 4); d++)
            if (vs[c2][d].p == s->p)
            {
              o += snprintf(buf + o, sizeof buf - o, " %d.%d@%02X", c2 / 3 + 1, c2 % 3 + 1, d);
              shown++;
            }
        if (n > shown) snprintf(buf + o, sizeof buf - o, " ...");
        buf[30] = 0;
        printtext(sx, sy++, MKCOL(C_HEADERTEXT, C_HEADER), buf);
      }
      for (r = 0; (r < pattlen[s->p]) && (r < 10); r++)
      {
        unsigned char *q = &pattern[s->p][r * 4];
        char nt[8];
        if ((q[0] >= FIRSTNOTE) && (q[0] <= LASTNOTE)) sprintf(nt, "%s", notename[q[0] - FIRSTNOTE]);
        else if (q[0] == KEYOFF) sprintf(nt, "---");
        else if (q[0] == KEYON) sprintf(nt, "+++");
        else sprintf(nt, "...");
        snprintf(buf, sizeof buf, "%02d %s %02X %X%02X", r, nt, q[1], q[2], q[3]);
        printtext(sx, sy++, MKCOL((q[0] <= LASTNOTE) || (q[1]) || (q[2]) ? 15 : 3, C_HEADER), buf);
      }
      if (pattlen[s->p] > 10) printtext(sx, sy++, MKCOL(3, C_HEADER), "   ...");
    }
    sy++;
    printtext(sx, sy++, MKCOL(7, C_HEADER), "Legend");
    printtext(sx, sy, MKCOL(14, C_HEADER), "+3");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "transpose set here");
    printtext(sx, sy, MKCOL(6, C_HEADER), "+3");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "transpose still in effect");
    printtext(sx, sy, MKCOL(8, C_HEADER), "x2");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "played twice");
    printtext(sx, sy, MKCOL(10, C_HEADER), "<");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "loop position, RST = end");
    printtext(sx, sy, MKCOL(3, C_HEADER), "05");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "empty pattern");
    printtext(sx + 3, sy++, MKCOL(C_HEADERTEXT, C_HEADER), "colour mark = pattern number");
  }

  // message and help
  printblankc(0, MAX_ROWS - 3, MKCOL(8, C_PANEL), MAX_COLUMNS);
  if (msg[0]) printtext(1, MAX_ROWS - 3, MKCOL(msgcol, C_PANEL), msg);
  printblankc(0, MAX_ROWS - 2, MKCOL(3, C_PANEL), MAX_COLUMNS);
  printtext(1, MAX_ROWS - 2, MKCOL(3, C_PANEL),
            "Cursor/mouse move  SHIFT+cursor/drag select  0-F pattern  CTRL+C/X/V copy/cut/paste (SHIFT+V over)  CTRL+D duplicate  "
            "INS/DEL");
  printblankc(0, MAX_ROWS - 1, hdr, MAX_COLUMNS);
  printtext(1, MAX_ROWS - 1, hdr,
            "+ - * / transpose  R/SHIFT+R repeat  CTRL+cursor move  U unique  N new  L loop  F1 play  ENTER pattern  TAB SIDs  "
            "CTRL+Z/Y  ESC");
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

static void keepvisible(void)
{
  int mr = maxrows();
  if (curr < 0) curr = 0;
  if (curr > mr) curr = mr;
  if (curr < viewr) viewr = curr;
  if (curr >= viewr + OE_ROWS) viewr = curr - OE_ROWS + 1;
  if (viewr < 0) viewr = 0;
}

static int changesince(const OESTATE *s)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
    if ((songlen[sub][c] != s->len[c]) || (memcmp(songorder[sub][c], s->order[c], MAX_SONGLEN + 2))) return 1;
  return (memcmp(s->patt, pattern, sizeof s->patt) != 0) || (memcmp(s->plen, pattlen, sizeof s->plen) != 0);
}

void orderlisteditor(void)
{
  static OESTATE *start = NULL, *now = NULL;
  LAYOUT l;
  int dragging = 0, lastclick = 0, lastcr = -1, lastcv = -1, done = 0, gotoeditor = 0;
  sub = editorInfo.esnum;
  if (!start) start = (OESTATE *)malloc(sizeof(OESTATE));
  if (!now) now = (OESTATE *)malloc(sizeof(OESTATE));
  if ((!start) || (!now)) return;
  takestate(start);
  nundo = nredo = 0;
  decodeall();
  curv = editorInfo.eschn < numchannels ? editorInfo.eschn : 0;
  curr = 0;
  {
    // the cursor on the step of the orderlist position of the main screen
    int k, steps = 0;
    for (k = 0; (k < editorInfo.eseditpos) && (k < songlen[sub][curv]); k++)
      if (songorder[sub][curv][k] < MAX_PATT) steps++;
    curr = steps;
  }
  hassel = 0;
  hexdigit = -1;
  timesdirty = 1;
  msg[0] = 0;
  viewr = curr > OE_ROWS / 2 ? curr - OE_ROWS / 2 : 0;
  key = rawkey = 0;

  while (!done)
  {
    int k, hit = -1, hc = -1, hr = -1, ctrl, shift;
    if (timesdirty)
    {
      totalframes = calcentryframes(sub, entryframes);
      timesdirty = 0;
    }
    layout(&l);
    keepvisible();
    if (curv < l.v0) curv = l.v0;
    if (curv > l.v1) curv = l.v1;
    draw(&l);
    fliptoscreen();
    getkey();
    if (win_quitted) break;
    ctrl = ctrlpressed || metapressed;
    shift = shiftpressed;

    // mouse: buttons, cells, headers, wheel
    for (k = 0; k < nhits; k++)
      if ((mousey == hits[k].y) && (mousex >= hits[k].x) && (mousex < hits[k].x + hits[k].w)) hit = hits[k].id;
    for (k = l.v0; k <= l.v1; k++)
      if ((mousex >= colx(&l, k)) && (mousex < colx(&l, k) + l.cw - 1)) hc = k;
    if ((mousey >= OE_TOP) && (mousey < OE_TOP + OE_ROWS)) hr = viewr + mousey - OE_TOP;
    if (mouseheld == 0) dragging = 0;
    if ((mouseb & MOUSEB_LEFT) && (!prevmouseb))
    {
      if (hit > 0)
      {
        switch (hit)
        {
          case OB_COPY: rawkey = KEY_C; ctrl = 1; break;
          case OB_CUT: rawkey = KEY_X; ctrl = 1; break;
          case OB_PASTE: rawkey = KEY_V; ctrl = 1; break;
          case OB_DUP: rawkey = KEY_D; ctrl = 1; break;
          case OB_INS: rawkey = KEY_INS; break;
          case OB_DEL: rawkey = KEY_DEL; break;
          case OB_UP: rawkey = KEY_UP; ctrl = 1; break;
          case OB_DOWN: rawkey = KEY_DOWN; ctrl = 1; break;
          case OB_T12M: transpose(-12); break;
          case OB_T1M: transpose(-1); break;
          case OB_T1P: transpose(1); break;
          case OB_T12P: transpose(12); break;
          case OB_REP: rawkey = KEY_R; break;
          case OB_UNIQUE: rawkey = KEY_U; break;
          case OB_NEW: rawkey = KEY_N; break;
          case OB_LOOP: rawkey = KEY_L; break;
          case OB_PLAY: rawkey = KEY_F1; break;
          case OB_STOP: rawkey = KEY_F4; break;
          case OB_UNDO: rawkey = KEY_Z; ctrl = 1; break;
          case OB_REDO: rawkey = KEY_Y; ctrl = 1; break;
          case OB_CLOSE: rawkey = KEY_ESC; hassel = 0; break;
          case OB_ROWSM: case OB_ROWSP:
          {
            int n = defaultpatternlength + (hit == OB_ROWSM ? -1 : 1) * (shift ? 8 : 1);
            if (n < 1) n = 1;
            if (n > MAX_PATTROWS) n = MAX_PATTROWS;
            pushundo();
            k = setsongpatternlength(n);
            songmodified = 1;
            snprintf(msg, sizeof msg, "New patterns of this song have %d rows (%d empty unused patterns changed)", n, k);
          }
          break;
          case OB_ROWS:
          {
            char lb[8];
            snprintf(lb, sizeof lb, "%d", defaultpatternlength);
            if (dlg_edittext("Pattern length", "Rows of new patterns in this song (1-128):", lb, 3, 1))
            {
              int n = atoi(lb);
              if (n < 1) n = 1;
              if (n > MAX_PATTROWS) n = MAX_PATTROWS;
              pushundo();
              k = setsongpatternlength(n);
              songmodified = 1;
              snprintf(msg, sizeof msg, "New patterns of this song have %d rows (%d empty unused patterns changed)", n, k);
            }
          }
          break;
          case OB_SUBM: rawkey = 0; key = '<'; break;
          case OB_SUBP: rawkey = 0; key = '>'; break;
          case OB_VIEW: rawkey = KEY_TAB; break;
        }
        if ((hit != OB_SUBM) && (hit != OB_SUBP)) key = 0;
      }
      else if ((mousey == 4) && (hc >= 0))
      {
        // a voice header: the whole voice
        hassel = 1;
        ancv = curv = hc;
        ancr = 0;
        curr = vn[hc] - 1;
        rawkey = key = 0;
      }
      else if ((hc >= 0) && (hr >= 0))
      {
        Uint32 t = SDL_GetTicks();
        if ((t - (Uint32)lastclick < 400) && (hc == lastcv) && (hr == lastcr))
        {
          rawkey = KEY_ENTER;
          key = 0;
        }
        else
        {
          if (shift) hassel = 1;
          else { ancv = hc; ancr = hr; hassel = 0; }
          curv = hc;
          curr = hr;
          dragging = 1;
          rawkey = key = 0;
        }
        lastclick = t;
        lastcv = hc;
        lastcr = hr;
        hexdigit = -1;
      }
    }
    else if ((mouseb & MOUSEB_LEFT) && (dragging) && (hc >= 0) && (hr >= 0) && ((hc != curv) || (hr != curr)))
    {
      curv = hc;
      curr = hr;
      hassel = 1;
    }
    if (mousewheel)
    {
      viewr -= mousewheel * 3;
      if (viewr < 0) viewr = 0;
      if (curr < viewr) curr = viewr;
      if (curr >= viewr + OE_ROWS) curr = viewr + OE_ROWS - 1;
    }
    if ((!rawkey) && (!key)) continue;
    msgcol = 8;

    // hex digits: a pattern number
    if ((!ctrl) && (hexnybble >= 0) && (rawkey != KEY_INS) && (rawkey != KEY_DEL))
    {
      if (hexdigit < 0) hexdigit = hexnybble;
      else
      {
        int p = hexdigit * 16 + hexnybble;
        hexdigit = -1;
        if (p >= MAX_PATT) snprintf(msg, sizeof msg, "Pattern %02X does not exist (00-%02X)", p, MAX_PATT - 1);
        else
        {
          setcell(p);
          curr++;
          hassel = 0;
        }
      }
      key = rawkey = 0;
      continue;
    }
    if (rawkey) hexdigit = -1;

    if (ctrl)
    {
      switch (rawkey)
      {
        case KEY_C: copysel(0); break;
        case KEY_X: copysel(1); break;
        case KEY_V: pasteat(curv, curr, shift, shift ? "Pasted over" : "Pasted into"); break;
        case KEY_D: duplicate(); break;
        case KEY_Z: doundo(); break;
        case KEY_Y: doredo(); break;
        case KEY_A: hassel = 1; ancv = l.v0; ancr = 0; curv = l.v1; curr = maxrows() - 1; break;
        case KEY_UP: moverows(-1); break;
        case KEY_DOWN: moverows(1); break;
      }
      key = rawkey = 0;
      continue;
    }
    if (!rawkey)
    {
      // letters that came as text only
      switch (key)
      {
        case 'r': rawkey = KEY_R; break;
        case 'R': rawkey = KEY_R; shift = 1; break;
        case 'u': case 'U': rawkey = KEY_U; break;
        case 'n': case 'N': rawkey = KEY_N; break;
        case 'l': case 'L': rawkey = KEY_L; break;
      }
    }
    if (key == '+') transpose(1);
    else if (key == '-') transpose(-1);
    else if (key == '*') transpose(12);
    else if (key == '/') transpose(-12);
    else if (key == '<') { if (sub > 0) { sub--; decodeall(); timesdirty = 1; nundo = nredo = 0; } }
    else if (key == '>') { if (sub < MAX_SONGS - 1) { sub++; decodeall(); timesdirty = 1; nundo = nredo = 0; } }
    else switch (rawkey)
    {
      case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT: case KEY_PGUP: case KEY_PGDN: case KEY_HOME: case KEY_END:
      if (shift) { if (!hassel) { ancv = curv; ancr = curr; } hassel = 1; }
      else hassel = 0;
      if (rawkey == KEY_UP) curr--;
      if (rawkey == KEY_DOWN) curr++;
      if (rawkey == KEY_PGUP) curr -= 16;
      if (rawkey == KEY_PGDN) curr += 16;
      if (rawkey == KEY_HOME) curr = 0;
      if (rawkey == KEY_END) curr = vn[curv];
      if ((rawkey == KEY_LEFT) && (curv > l.v0)) curv--;
      if ((rawkey == KEY_RIGHT) && (curv < l.v1)) curv++;
      break;
      case KEY_INS: insertrows(); break;
      case KEY_DEL: case KEY_BACKSPACE: deleterows(); break;
      case KEY_R: repeat(shift ? -1 : 1); break;
      case KEY_U: makeunique(); break;
      case KEY_N: newpattern(); break;
      case KEY_L: setloop(); break;
      case KEY_TAB:
      viewsid = viewsid < 0 ? curv / 3 : (viewsid + 1 < numsids ? viewsid + 1 : -1);
      if (numsids == 1) viewsid = -1;
      break;
      case KEY_F1: playfrom(curr); break;
      case KEY_F4: stopsong(); break;
      case KEY_ENTER:
      if (curr < vn[curv]) { gotoeditor = 1; done = 1; }
      break;
      case KEY_ESC:
      if (hassel) hassel = 0;
      else done = 1;
      break;
    }
    key = rawkey = 0;
  }

  // the whole session is one undo step of the main editor
  if (changesince(start))
  {
    void *ed;
    int c;
    takestate(now);
    givestate(start);
    ed = undoinstrbegin();
    for (c = 0; c < numchannels; c++)
      undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c + sub * MAX_CHN, UNDO_AREA_DIRTY_CHECK);
    undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
    undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
    givestate(now);
    undoinstrend(ed);
    countpatternlengths();
    songmodified = 1;
  }
  editorInfo.esnum = sub;
  if (gotoeditor)
  {
    // the pattern of the cell in the pattern editor, the orderlist on it
    int k, steps = 0;
    for (k = 0; k < songlen[sub][curv]; k++)
      if (songorder[sub][curv][k] < MAX_PATT)
      {
        if (steps == curr) break;
        steps++;
      }
    int c;
    editorInfo.eschn = curv;
    editorInfo.eseditpos = k;
    if (editorInfo.esview > k) editorInfo.esview = k;
    if (editorInfo.esview < k - 8) editorInfo.esview = k - 8;
    editorInfo.epchn = curv;
    // every voice shows its pattern at this position
    for (c = 0; c < numchannels; c++)
      editorUndoInfo.editorInfo[c].epnum = vs[c][curr < vn[c] ? curr : vn[c] - 1].p;
    editorInfo.eppos = 0;
    editorInfo.epview = -VISIBLEPATTROWS / 2;
    editorInfo.editmode = EDIT_PATTERN;
  }
  key = rawkey = 0;
  printmainscreen();
}

// tests (GG_OETEST): read and write every orderlist; 0 = all unchanged
int orderedit_selftest(void)
{
  int c, bad = 0;
  for (sub = 0; sub < MAX_SONGS; sub++)
    for (c = 0; c < numchannels; c++)
    {
      unsigned char o[MAX_SONGLEN + 2];
      int len = songlen[sub][c];
      if (!len) continue;
      memcpy(o, songorder[sub][c], MAX_SONGLEN + 2);
      decode(c);
      encode(c, 1);
      if ((songlen[sub][c] != len) || (memcmp(o, songorder[sub][c], len + 2)))
      {
        int k;
        bad++;
        fprintf(stderr, "subtune %d voice %d: ", sub, c);
        for (k = 0; k < len + 2; k++) fprintf(stderr, "%02X ", o[k]);
        fprintf(stderr, "\n            -> ");
        for (k = 0; k < songlen[sub][c] + 2; k++) fprintf(stderr, "%02X ", songorder[sub][c][k]);
        fprintf(stderr, "\n");
      }
    }
  return bad;
}

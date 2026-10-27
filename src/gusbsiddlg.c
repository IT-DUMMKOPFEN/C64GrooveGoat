//
// C64 GrooveGoat - USBSID-Pico dialog (Alt+U): output, board list and SID map
//
// The dialog edits local copies. Apply hands the result to sound_usbsidapply(),
// Cancel restores the sound with the unchanged globals. The dialog loop never
// calls usbsidpoll().
//

#define GUSBSIDDLG_C

#include "goattrk2.h"

#define DLG_W 80
#define DLG_MAXROWS 16
#define DLG_MAXINFO 32
#define DLG_VISIBLE 8
#define DLG_SONGROWS USBSID_MAXSIDS
#define DLG_TOKENSIZE 48

// Board row states
enum { ROW_OK, ROW_BUSY, ROW_MISSING };

// Map token modes
enum { TOK_AUTO, TOK_OFF, TOK_CHIP };

// Column offsets from the left frame edge, board rows
#define COL_NUM 4
#define COL_USE 8
#define COL_SERIAL 13
#define COL_SIDS 31
#define COL_CHIPS 37
#define COL_PCB 58
#define COL_STATE 64

// Column offsets, song SID rows
#define COL_SID 4
#define COL_ADDR 11
#define COL_CHOICE 18
#define COL_CHOICEW 22
#define COL_ARROW 41
#define COL_PREVIEW 44

#define COLOR_HEAD 14
#define COLOR_LABEL 7
#define COLOR_VALUE 15
#define COLOR_DIM 8
#define COLOR_OK 10
#define COLOR_WARN 12
#define COLOR_BUSY 6

typedef struct
{
  int mode;                       // TOK_*
  char serial[USBSID_SERIALSIZE]; // TOK_CHIP: board serial
  int slot;                       // TOK_CHIP: chip number on the board, 1 based
} MAPTOKEN;

typedef struct
{
  char serial[USBSID_SERIALSIZE]; // Empty for a board that reports no serial
  int state;                      // ROW_*
  int use;                        // Mark
  int named;                      // Listed in usbsidboards when the dialog opened
  int inuse;                      // Open in the engine when the dialog opened
  int busidx;                     // Position in USB bus and port order
} BOARDROW;

typedef struct
{
  int x, y, w, h;
  int nr;                         // Visible board rows
  int extra;                      // 1 when a "more" line follows the board rows
  int yout, yboard, ysonghdr, ysong, yhint, yhelp, ybtn;
} LAYOUT;

static BOARDROW rows[DLG_MAXROWS];
static int numrows;
static int boardtop;
static USBSID_SIDINFO info[DLG_MAXINFO];
static int numinfo;
static USBSID_SIDINFO cand[DLG_MAXINFO];
static int numcand;
static int rownum[DLG_MAXROWS];
static MAPTOKEN tokens[DLG_SONGROWS];
static int outon;
static int probed;
static int soundstopped;
static char statetext[64];
static int statewarn;

/**
 * @brief: Split a comma separated list, empty tokens are kept.
 * @param list: input, may be NULL
 * @param out: output, max tokens of width bytes each
 * @param width: bytes per token including the terminating zero
 * @param max: token capacity
 * @return: number of tokens
 */
static int splitlist(const char *list, char *out, int width, int max)
{
  int n = 0;
  const char *p = list;

  if ((!list) || (!list[0])) return 0;
  for (;;)
  {
    const char *e = strchr(p, ',');
    int len = e ? (int)(e - p) : (int)strlen(p);

    if (n >= max) break;
    if (len > width - 1) len = width - 1;
    memcpy(out + n * width, p, len);
    out[n * width + len] = 0;
    n++;
    if (!e) break;
    p = e + 1;
  }
  return n;
}

/**
 * @brief: Parse one SID map token with usbsid_parsetoken().
 * @param t: output token, auto when the text is malformed
 * @param s: "-", "0" or "SERIAL:N"
 */
static void parsetoken(MAPTOKEN *t, const char *s)
{
  memset(t, 0, sizeof *t);
  t->mode = TOK_AUTO;
  switch (usbsid_parsetoken(s, (int)strlen(s), t->serial, sizeof t->serial, &t->slot))
  {
    case USBSID_TOKEN_OFF:
      t->mode = TOK_OFF;
      break;
    case USBSID_TOKEN_CHIP:
      t->mode = TOK_CHIP;
      break;
    default:
      memset(t, 0, sizeof *t);
      t->mode = TOK_AUTO;
      break;
  }
}

/**
 * @brief: Parse the SID map string into the token table, missing tokens are auto.
 * @param map: comma separated tokens, may be empty
 */
static void parsemap(const char *map)
{
  char part[DLG_SONGROWS][DLG_TOKENSIZE];
  int n, c;

  n = splitlist(map, &part[0][0], DLG_TOKENSIZE, DLG_SONGROWS);
  for (c = 0; c < DLG_SONGROWS; c++)
  {
    if (c < n) parsetoken(&tokens[c], part[c]);
    else parsetoken(&tokens[c], "-");
  }
}

/**
 * @brief: Return the row of a board serial, -1 when absent.
 * @param serial: board serial, empty never matches
 */
static int findrow(const char *serial)
{
  int r;

  if (!serial[0]) return -1;
  for (r = 0; r < numrows; r++)
    if (!strcmp(rows[r].serial, serial)) return r;
  return -1;
}

/**
 * @brief: Return 1 when any playable board row carries the use mark.
 */
static int anyokuse(void)
{
  int r;

  for (r = 0; r < numrows; r++)
    if ((rows[r].state == ROW_OK) && (rows[r].use)) return 1;
  return 0;
}

/**
 * @brief: Return 1 when a board plays after Apply. With no mark on any playable board
 *         the engine opens every board.
 * @param r: board row
 */
static int rowactive(int r)
{
  return (rows[r].state == ROW_OK) && ((rows[r].use) || (!anyokuse()));
}

/**
 * @brief: Return 1 when the chip list of the output is known: output on and probed.
 */
static int chipsknown(void)
{
  return (outon) && (probed);
}

/**
 * @brief: Build the chip list of the boards that play, in list order.
 *         Board numbers follow the position among the playing boards.
 */
static void buildcand(void)
{
  int r, i, b = 0;

  numcand = 0;
  for (r = 0; r < numrows; r++)
  {
    rownum[r] = 0;
    if (!rowactive(r)) continue;
    b++;
    rownum[r] = b;
    if (!chipsknown()) continue;
    for (i = 0; i < numinfo; i++)
    {
      if ((numcand < DLG_MAXINFO) && (!strcmp(info[i].serial, rows[r].serial)))
      {
        cand[numcand] = info[i];
        cand[numcand].board = b;
        numcand++;
      }
    }
  }
}

/**
 * @brief: Return the display name of a SID type.
 * @param type: USBSID_TYPE_*
 */
static const char *sidtypename(int type)
{
  switch (type)
  {
    case USBSID_TYPE_6581: return "6581";
    case USBSID_TYPE_8580: return "8580";
    case USBSID_TYPE_NONE: return "none";
    case USBSID_TYPE_FMOPL: return "FM";
  }
  return "?";
}

/**
 * @brief: Shorten a long serial to 4 + "..." + 4 characters.
 * @param out: output
 * @param size: output size in bytes
 * @param serial: board serial
 */
static void shortserial(char *out, int size, const char *serial)
{
  int len = (int)strlen(serial);

  if (len > 12) snprintf(out, size, "%.4s...%s", serial, serial + len - 4);
  else snprintf(out, size, "%s", serial);
}

/**
 * @brief: Write the text of a map token for the SID map string.
 * @param t: token
 * @param out: output
 * @param size: output size in bytes
 */
static void tokentext(const MAPTOKEN *t, char *out, int size)
{
  if (t->mode == TOK_OFF) snprintf(out, size, "0");
  else if (t->mode == TOK_CHIP) snprintf(out, size, "%s:%d", t->serial, t->slot);
  else snprintf(out, size, "-");
}

/**
 * @brief: Return 1 when a board named by a token stays in use after Apply.
 * @param serial: board serial of the token
 */
static int boardkept(const char *serial)
{
  int r = findrow(serial);

  if (r < 0) return 0;
  if (rows[r].state == ROW_OK) return rowactive(r);
  return rows[r].use;
}

/**
 * @brief: Build the SID map string from the token table.
 * @param out: output
 * @param size: output size in bytes
 * @param forapply: 1 = tokens of boards that drop out turn auto and trailing auto tokens
 *                  are trimmed, 0 = all tokens, for resolving
 */
static void buildmap(char *out, int size, int forapply)
{
  int c, last = -1, len = 0;
  char text[DLG_TOKENSIZE];

  out[0] = 0;
  for (c = 0; c < DLG_SONGROWS; c++)
  {
    MAPTOKEN t = tokens[c];

    if ((forapply) && (t.mode == TOK_CHIP) && (!boardkept(t.serial))) t.mode = TOK_AUTO;
    if (t.mode != TOK_AUTO) last = c;
  }
  if ((forapply) && (last < 0)) return;
  for (c = 0; c < (forapply ? last + 1 : DLG_SONGROWS); c++)
  {
    MAPTOKEN t = tokens[c];

    if ((forapply) && (t.mode == TOK_CHIP) && (!boardkept(t.serial))) t.mode = TOK_AUTO;
    tokentext(&t, text, sizeof text);
    if (len < size - 1)
      len += snprintf(out + len, size - len, "%s%s", c ? "," : "", text);
  }
}

/**
 * @brief: Build the board list string for Apply. Empty = every board.
 * @param out: output
 * @param size: output size in bytes
 */
static void buildboards(char *out, int size)
{
  int r, okcount = 0, contrib = 0, plain = 1, last = -1, len = 0;

  out[0] = 0;
  for (r = 0; r < numrows; r++)
    if (rows[r].state == ROW_OK) okcount++;
  for (r = 0; r < numrows; r++)
  {
    if ((!rows[r].use) || ((rows[r].state != ROW_OK) && (!rows[r].named))) continue;
    // Only a playable board makes the engine open a named list
    if (rows[r].state == ROW_OK) contrib++;
    if ((rows[r].state != ROW_OK) || (rows[r].busidx < last)) plain = 0;
    last = rows[r].busidx;
  }
  if (!contrib)
  {
    // No mark: the engine opens every board. An explicit list is needed only when
    // the table order differs from the bus order (engine order = preview)
    int ordered = 1;

    last = -1;
    for (r = 0; r < numrows; r++)
    {
      if (rows[r].state != ROW_OK) continue;
      if (rows[r].busidx < last) ordered = 0;
      last = rows[r].busidx;
    }
    if (ordered) return;
    for (r = 0; r < numrows; r++)
    {
      if ((rows[r].state != ROW_OK) || (len >= size - 1)) continue;
      len += snprintf(out + len, size - len, "%s%s", len ? "," : "", rows[r].serial);
    }
    return;
  }
  if ((plain) && (contrib == okcount)) return;
  for (r = 0; r < numrows; r++)
  {
    if ((!rows[r].use) || ((rows[r].state != ROW_OK) && (!rows[r].named))) continue;
    if (len < size - 1)
      len += snprintf(out + len, size - len, "%s%s", len ? "," : "", rows[r].serial);
  }
}

/**
 * @brief: Add a board row.
 * @param serial: board serial, may be empty
 * @param state: ROW_*
 * @param use: initial mark
 * @param named: 1 when listed in usbsidboards
 * @param busidx: position in bus order
 */
static void addrow(const char *serial, int state, int use, int named, int busidx)
{
  BOARDROW *b;

  if (numrows >= DLG_MAXROWS) return;
  b = &rows[numrows++];
  memset(b, 0, sizeof *b);
  snprintf(b->serial, sizeof b->serial, "%s", serial);
  b->state = state;
  b->use = use;
  b->named = named;
  b->busidx = busidx;
}

/**
 * @brief: Return 1 when the probe opened a board, or when no probe ran.
 * @param serial: board serial
 */
static int boardopened(const char *serial)
{
  int i;

  if (!chipsknown()) return 1;
  for (i = 0; i < numinfo; i++)
    if (!strcmp(info[i].serial, serial)) return 1;
  return 0;
}

/**
 * @brief: Build the board rows from the attached list, usbsidboards and the SID map.
 * @param attached: comma separated serials in bus order, empty serial = board that is busy
 * @param count: number of attached boards
 * @param inuse: comma separated serials open in the engine
 */
static void buildrows(const char *attached, int count, const char *inuse)
{
  char att[DLG_MAXROWS][USBSID_SERIALSIZE];
  char named[DLG_MAXROWS][USBSID_SERIALSIZE];
  char open[DLG_MAXROWS][USBSID_SERIALSIZE];
  int natt, nnamed, nopen, a, n, c, r, i;

  natt = splitlist(attached, &att[0][0], USBSID_SERIALSIZE, DLG_MAXROWS);
  while ((natt < count) && (natt < DLG_MAXROWS)) att[natt++][0] = 0;
  nnamed = splitlist(usbsidboards, &named[0][0], USBSID_SERIALSIZE, DLG_MAXROWS);
  nopen = splitlist(inuse, &open[0][0], USBSID_SERIALSIZE, DLG_MAXROWS);

  numrows = 0;
  boardtop = 0;
  // Boards named in usbsidboards keep their place, attached or not
  for (n = 0; n < nnamed; n++)
  {
    int state;

    if ((!named[n][0]) || (findrow(named[n]) >= 0)) continue;
    a = -1;
    for (i = 0; i < natt; i++)
      if (!strcmp(att[i], named[n])) a = i;
    if (a < 0) state = ROW_MISSING;
    else state = boardopened(named[n]) ? ROW_OK : ROW_BUSY;
    addrow(named[n], state, 1, 1, a < 0 ? 999 : a);
  }
  // Remaining attached boards in bus order
  for (a = 0; a < natt; a++)
  {
    if ((!att[a][0]) || (findrow(att[a]) >= 0)) continue;
    if (boardopened(att[a])) addrow(att[a], ROW_OK, nnamed ? 0 : 1, 0, a);
    else addrow(att[a], ROW_BUSY, 0, 0, a);
  }
  for (a = 0; a < natt; a++)
    if (!att[a][0]) addrow("", ROW_BUSY, 0, 0, a);
  // Boards that only the SID map names
  for (c = 0; c < DLG_SONGROWS; c++)
    if ((tokens[c].mode == TOK_CHIP) && (findrow(tokens[c].serial) < 0))
      addrow(tokens[c].serial, ROW_MISSING, 1, 0, 999);
  for (r = 0; r < numrows; r++)
    for (n = 0; n < nopen; n++)
      if ((rows[r].serial[0]) && (!strcmp(rows[r].serial, open[n]))) rows[r].inuse = 1;
}

/**
 * @brief: Probe the boards with the local output flag and refresh the info list.
 *         The global usbsid is restored afterwards.
 */
static void probeboards(void)
{
  char attached[MAX_PATHNAME];
  unsigned saved = usbsid;

  usbsid = outon ? 1 : 0;
  if (outon) soundstopped = 1;
  numinfo = sound_usbsidprobe(info, DLG_MAXINFO, attached, sizeof attached);
  if (numinfo > DLG_MAXINFO) numinfo = DLG_MAXINFO;
  if (numinfo < 0) numinfo = 0;
  probed = outon ? 1 : 0;
  usbsid = saved;
}

/**
 * @brief: Toggle the use mark of a board row. A busy board can lose its mark but
 *         not gain one.
 * @param r: board row
 */
static void toggleuse(int r)
{
  if ((rows[r].state == ROW_BUSY) && (!rows[r].use)) return;
  rows[r].use ^= 1;
}

/**
 * @brief: Set the state line from the output flag and the last probe.
 */
static void refreshstate(void)
{
  int i, j, nboards = 0;

  statewarn = 0;
  if (!outon)
  {
    snprintf(statetext, sizeof statetext, "Output off, playing on reSID");
    return;
  }
  if ((!probed) || (numinfo <= 0))
  {
    snprintf(statetext, sizeof statetext, "Not connected, playing on reSID");
    return;
  }
  for (i = 0; i < numinfo; i++)
  {
    for (j = 0; j < i; j++)
      if (!strcmp(info[i].serial, info[j].serial)) break;
    if (j == i) nboards++;
  }
  snprintf(statetext, sizeof statetext, "Connected: %d board%s, %d SID%s", nboards, nboards == 1 ? "" : "s", numinfo, numinfo == 1 ? "" : "s");
}

/**
 * @brief: Toggle the output flag, a first switch on probes the boards and marks
 *         the boards that did not open as busy.
 */
static void toggleoutput(void)
{
  int r;

  outon ^= 1;
  if ((outon) && (!probed))
  {
    probeboards();
    for (r = 0; r < numrows; r++)
      if ((rows[r].state == ROW_OK) && (!boardopened(rows[r].serial)))
      {
        rows[r].state = ROW_BUSY;
        rows[r].use = rows[r].named ? rows[r].use : 0;
      }
  }
  refreshstate();
}

/**
 * @brief: Compute the board numbers, the candidate chips and the resolved targets.
 * @param tgtn: output, chip per song SID resolved with the song SID count
 * @param maskn: output, missing mask of tgtn
 * @param tgt8: output, chip per song SID resolved over all rows
 * @param mask8: output, missing mask of tgt8
 */
static void resolveall(int *tgtn, unsigned *maskn, int *tgt8, unsigned *mask8)
{
  char map[MAX_PATHNAME];
  int c, n = numsids;

  if (n < 1) n = 1;
  if (n > DLG_SONGROWS) n = DLG_SONGROWS;
  buildcand();
  for (c = 0; c < DLG_SONGROWS; c++) tgtn[c] = tgt8[c] = -1;
  *maskn = *mask8 = 0;
  if (!chipsknown()) return;
  buildmap(map, sizeof map, 0);
  *maskn = sound_usbsidresolve(map, cand, numcand, tgtn, n);
  *mask8 = sound_usbsidresolve(map, cand, numcand, tgt8, DLG_SONGROWS);
}

/**
 * @brief: Return the candidate index of a chip token, -1 when not listed.
 * @param t: chip token
 */
static int findcand(const MAPTOKEN *t)
{
  int i;

  for (i = 0; i < numcand; i++)
    if ((cand[i].slot == t->slot) && (!strcmp(cand[i].serial, t->serial))) return i;
  return -1;
}

/**
 * @brief: Step a song SID token through auto, off and the free chips.
 * @param s: song SID row
 * @param dir: +1 next, -1 previous
 */
static void cycletoken(int s, int dir)
{
  int opts[DLG_MAXINFO + 2];
  int numopts = 0, cur = -1, i, j, next;
  MAPTOKEN *t = &tokens[s];

  buildcand();
  opts[numopts++] = -2;
  opts[numopts++] = -1;
  for (i = 0; i < numcand; i++)
  {
    int taken = 0;

    if (cand[i].type == USBSID_TYPE_FMOPL) continue;
    for (j = 0; j < DLG_SONGROWS; j++)
    {
      if ((j != s) && (tokens[j].mode == TOK_CHIP) && (tokens[j].slot == cand[i].slot) &&
          (!strcmp(tokens[j].serial, cand[i].serial))) taken = 1;
    }
    if (!taken) opts[numopts++] = i;
  }
  if (t->mode == TOK_AUTO) cur = 0;
  else if (t->mode == TOK_OFF) cur = 1;
  else
  {
    int ci = findcand(t);

    for (i = 2; (ci >= 0) && (i < numopts); i++)
      if (opts[i] == ci) cur = i;
  }
  if (cur < 0) next = (dir > 0) ? 0 : numopts - 1;
  else next = (cur + dir + numopts) % numopts;

  if (opts[next] == -2) memset(t, 0, sizeof *t);
  else if (opts[next] == -1)
  {
    memset(t, 0, sizeof *t);
    t->mode = TOK_OFF;
  }
  else
  {
    memset(t, 0, sizeof *t);
    t->mode = TOK_CHIP;
    snprintf(t->serial, sizeof t->serial, "%s", cand[opts[next]].serial);
    t->slot = cand[opts[next]].slot;
  }
}

/**
 * @brief: Fill the layout record for the current number of board rows.
 * @param l: output
 */
static void calclayout(LAYOUT *l)
{
  l->w = DLG_W;
  l->nr = numrows < 1 ? 1 : (numrows > DLG_VISIBLE ? DLG_VISIBLE : numrows);
  l->extra = numrows > DLG_VISIBLE ? 1 : 0;
  l->yout = 2;
  l->yboard = 6;
  l->ysonghdr = 6 + l->nr + l->extra + 1;
  l->ysong = l->ysonghdr + 1;
  l->yhint = l->ysong + DLG_SONGROWS + 1;
  l->yhelp = l->yhint + 1;
  l->ybtn = l->yhelp + 2;
  l->h = l->ybtn + 2;
  l->x = (MAX_COLUMNS - l->w) / 2;
  l->y = (MAX_ROWS - l->h) / 2 - 2;
  if (l->y < 1) l->y = 1;
}

/**
 * @brief: Format the SID count, chip type list and PCB version of a board row.
 *         All three are "-" without probe data.
 * @param r: board row
 * @param sids: output, SID count text
 * @param ssize: sids size in bytes
 * @param chips: output, chip type list
 * @param csize: chips size in bytes
 * @param pcb: output, PCB version text
 * @param psize: pcb size in bytes
 */
static void rowchiptext(int r, char *sids, int ssize, char *chips, int csize, char *pcb, int psize)
{
  int i, n = 0, len = 0;

  snprintf(sids, ssize, "-");
  snprintf(chips, csize, "-");
  snprintf(pcb, psize, "-");
  if ((!chipsknown()) || (rows[r].state != ROW_OK)) return;
  chips[0] = 0;
  for (i = 0; i < numinfo; i++)
  {
    if (strcmp(info[i].serial, rows[r].serial)) continue;
    if (n == 0)
    {
      if (info[i].pcb > 0) snprintf(pcb, psize, "v%d.%d", info[i].pcb / 10, info[i].pcb % 10);
      else snprintf(pcb, psize, "?");
    }
    if (len < csize - 1)
      len += snprintf(chips + len, csize - len, "%s%s", n ? " " : "", sidtypename(info[i].type));
    n++;
  }
  snprintf(sids, ssize, "%d", n);
}

/**
 * @brief: Draw one board row.
 * @param l: layout
 * @param r: board row
 * @param ry: screen row
 * @param sel: 1 for the cursor row
 */
static void drawboard(const LAYOUT *l, int r, int ry, int sel)
{
  BOARDROW *b = &rows[r];
  int bg = sel ? C_SELECT : C_PANEL;
  int fg = (b->state == ROW_OK) ? (b->use ? COLOR_VALUE : COLOR_LABEL) : COLOR_DIM;
  char buf[80], sids[16], chips[48], pcb[16], shown[USBSID_SERIALSIZE + 16];
  const char *state = "";
  int statecolor = COLOR_OK;

  printblankc(l->x + 2, ry, MKCOL(COLOR_DIM, bg), l->w - 4);
  if (rownum[r]) snprintf(buf, sizeof buf, "%2d", rownum[r]);
  else snprintf(buf, sizeof buf, " -");
  printtext(l->x + COL_NUM, ry, MKCOL(fg, bg), buf);
  printtext(l->x + COL_USE, ry, MKCOL(fg, bg), b->use ? "[x]" : "[ ]");
  snprintf(shown, sizeof shown, "%.16s", b->serial[0] ? b->serial : "(no serial)");
  printtext(l->x + COL_SERIAL, ry, MKCOL(fg, bg), shown);
  rowchiptext(r, sids, sizeof sids, chips, sizeof chips, pcb, sizeof pcb);
  printtext(l->x + COL_SIDS, ry, MKCOL(fg, bg), sids);
  snprintf(shown, sizeof shown, "%.20s", chips);
  printtext(l->x + COL_CHIPS, ry, MKCOL(fg, bg), shown);
  printtext(l->x + COL_PCB, ry, MKCOL(fg, bg), pcb);
  if (b->state == ROW_BUSY)
  {
    state = "busy";
    statecolor = COLOR_BUSY;
  }
  else if (b->state == ROW_MISSING)
  {
    state = "missing";
    statecolor = COLOR_WARN;
  }
  else if (b->inuse) state = "in use";
  printtext(l->x + COL_STATE, ry, MKCOL(statecolor, bg), state);
}

/**
 * @brief: Draw one song SID row.
 * @param l: layout
 * @param s: song SID row
 * @param ry: screen row
 * @param sel: 1 for the cursor row
 * @param tgt: resolved chip for this row, -1 for none
 * @param missing: 1 when the explicit chip is not available
 */
static void drawsong(const LAYOUT *l, int s, int ry, int sel, int tgt, int missing)
{
  const MAPTOKEN *t = &tokens[s];
  int bg = sel ? C_SELECT : C_PANEL;
  int beyond = s >= numsids;
  int fg = beyond ? COLOR_DIM : COLOR_VALUE;
  int lab = beyond ? COLOR_DIM : COLOR_LABEL;
  int choicecolor = fg;
  char buf[80], ser[24], choice[48];

  printblankc(l->x + 2, ry, MKCOL(COLOR_DIM, bg), l->w - 4);
  snprintf(buf, sizeof buf, "SID %d", s + 1);
  printtext(l->x + COL_SID, ry, MKCOL(lab, bg), buf);
  snprintf(buf, sizeof buf, "$%04X", s < MAX_SIDS ? sidaddr[s] : (unsigned)(0xd400 + s * 0x20));
  printtext(l->x + COL_ADDR, ry, MKCOL(lab, bg), buf);

  if (t->mode == TOK_AUTO) snprintf(choice, sizeof choice, "auto");
  else if (t->mode == TOK_OFF) snprintf(choice, sizeof choice, "off");
  else
  {
    int ci = findcand(t);

    shortserial(ser, sizeof ser, t->serial);
    if ((ci >= 0) && (!missing)) snprintf(choice, sizeof choice, "Board %d SID %d", cand[ci].board, cand[ci].slot);
    else if (chipsknown())
    {
      snprintf(choice, sizeof choice, "%s:%d missing", ser, t->slot);
      if (!beyond) choicecolor = COLOR_WARN;
    }
    else snprintf(choice, sizeof choice, "%s:%d", ser, t->slot);
  }
  snprintf(buf, sizeof buf, "%.*s", COL_CHOICEW, choice);
  printtext(l->x + COL_CHOICE, ry, MKCOL(choicecolor, bg), buf);

  printtext(l->x + COL_ARROW, ry, MKCOL(lab, bg), "->");
  if (t->mode == TOK_OFF) snprintf(buf, sizeof buf, "silent");
  else if (!outon) snprintf(buf, sizeof buf, "reSID");
  else if (!chipsknown()) snprintf(buf, sizeof buf, "reSID");
  else if (tgt >= 0) snprintf(buf, sizeof buf, "Board %d SID %d  %s", cand[tgt].board, cand[tgt].slot, sidtypename(cand[tgt].type));
  else snprintf(buf, sizeof buf, "no chip available");
  printtext(l->x + COL_PREVIEW, ry, MKCOL((tgt < 0) && (t->mode != TOK_OFF) && (outon) && (!beyond) ? COLOR_WARN : fg, bg), buf);
}

/**
 * @brief: Return the hint text for the cursor row.
 * @param cur: cursor position
 * @param apply: position of the Apply button
 */
static const char *hinttext(int cur, int apply)
{
  if (cur == 0) return "SPACE switches the USBSID-Pico output on or off";
  if (cur <= numrows)
  {
    switch (rows[cur - 1].state)
    {
      case ROW_BUSY: return "Board is claimed by another program or did not open, SPACE clears its mark";
      case ROW_MISSING: return "Board is not attached, its settings are kept";
    }
    return "SPACE use this board, SHIFT+UP/DOWN sets the SID order";
  }
  if (cur < apply) return "LEFT/RIGHT pick the chip, DEL auto, 0 off";
  return "ENTER or click to confirm";
}

/**
 * @brief: Move the board scroll window to show a board row.
 * @param l: layout
 * @param r: board row
 */
static void showboardrow(const LAYOUT *l, int r)
{
  if (r < boardtop) boardtop = r;
  if (r >= boardtop + l->nr) boardtop = r - l->nr + 1;
  if (boardtop < 0) boardtop = 0;
}

/**
 * @brief: Run the dialog loop.
 * @return: 1 = Apply, 0 = Cancel
 */
static int rundialog(void)
{
  LAYOUT l;
  int cur = 0;
  int result = -2;
  int ncur;
  int applypos, cancelpos;

  calclayout(&l);
  applypos = 1 + numrows + DLG_SONGROWS;
  cancelpos = applypos + 1;
  ncur = cancelpos + 1;
  key = 0;
  rawkey = 0;

  while (result == -2)
  {
    int tgtn[DLG_SONGROWS], tgt8[DLG_SONGROWS];
    unsigned maskn, mask8;
    int c, r, s, bx, wheel;
    int click, rclick;
    char buf[100];
    int songfirst = 1 + numrows;
    int lastrow = songfirst + DLG_SONGROWS - 1;

    resolveall(tgtn, &maskn, tgt8, &mask8);
    printstatus();
    dlg_drawframe(l.x, l.y, l.w, l.h, "USBSID-Pico");

    // Output row
    c = (cur == 0) ? C_SELECT : C_PANEL;
    printblankc(l.x + 2, l.y + l.yout, MKCOL(COLOR_DIM, c), l.w - 4);
    printtext(l.x + COL_NUM, l.y + l.yout, MKCOL(COLOR_LABEL, c), "Output");
    printtext(l.x + 14, l.y + l.yout, MKCOL(COLOR_VALUE, c), outon ? "[x] USBSID-Pico on" : "[ ] USBSID-Pico off");
    printtext(l.x + 40, l.y + l.yout, MKCOL(statewarn ? COLOR_WARN : COLOR_LABEL, c), statetext);

    // Board section
    printtext(l.x + COL_NUM, l.y + l.yboard - 2, MKCOL(COLOR_HEAD, C_PANEL), "Boards");
    printtext(l.x + 12, l.y + l.yboard - 2, MKCOL(3, C_PANEL), "SPACE use, SHIFT+UP/DOWN order");
    printtext(l.x + COL_NUM, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), " #");
    printtext(l.x + COL_USE, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "Use");
    printtext(l.x + COL_SERIAL, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "Serial");
    printtext(l.x + COL_SIDS, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "SIDs");
    printtext(l.x + COL_CHIPS, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "Chips");
    printtext(l.x + COL_PCB, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "PCB");
    printtext(l.x + COL_STATE, l.y + l.yboard - 1, MKCOL(COLOR_LABEL, C_PANEL), "State");
    if (numrows == 0)
      printtext(l.x + COL_NUM, l.y + l.yboard, MKCOL(COLOR_DIM, C_PANEL), "No board attached");
    for (r = boardtop; (r < numrows) && (r < boardtop + l.nr); r++)
      drawboard(&l, r, l.y + l.yboard + r - boardtop, cur == 1 + r);
    if (l.extra)
    {
      snprintf(buf, sizeof buf, "and %d more", numrows - l.nr);
      printtext(l.x + COL_NUM, l.y + l.yboard + l.nr, MKCOL(COLOR_DIM, C_PANEL), buf);
    }

    // Song SID section
    printtext(l.x + COL_NUM, l.y + l.ysonghdr, MKCOL(COLOR_HEAD, C_PANEL), "Song SIDs");
    printtext(l.x + 14, l.y + l.ysonghdr, MKCOL(3, C_PANEL), "LEFT/RIGHT chip, DEL auto, 0 off");
    snprintf(buf, sizeof buf, "song uses %d SID%s", numsids, numsids == 1 ? "" : "s");
    printtext(l.x + l.w - 4 - (int)strlen(buf), l.y + l.ysonghdr, MKCOL(COLOR_LABEL, C_PANEL), buf);
    for (s = 0; s < DLG_SONGROWS; s++)
    {
      int beyond = s >= numsids;

      drawsong(&l, s, l.y + l.ysong + s, cur == songfirst + s, beyond ? tgt8[s] : tgtn[s],
        (((beyond ? mask8 : maskn) >> s) & 1) ? 1 : 0);
    }

    // Hint, help and buttons
    printtext(l.x + COL_NUM, l.y + l.yhint, MKCOL(3, C_PANEL), hinttext(cur, applypos));
    printtext(l.x + COL_NUM, l.y + l.yhelp, MKCOL(COLOR_LABEL, C_PANEL), "ENTER apply and reconnect, ESC cancel");
    bx = l.x + (l.w - 27) / 2;
    printtext(bx, l.y + l.ybtn, MKCOL(C_BUTTONTEXT, cur == applypos ? C_BUTTONHI : C_BUTTON), "   Apply   ");
    printtext(bx + 15, l.y + l.ybtn, MKCOL(C_BUTTONTEXT, cur == cancelpos ? C_BUTTONHI : C_BUTTON), "   Cancel   ");

    fliptoscreen();
    getkey();

    if (win_quitted)
    {
      result = 0;
      break;
    }

    // Mouse
    click = (mouseb & MOUSEB_LEFT) && (!(prevmouseb & MOUSEB_LEFT));
    rclick = (mouseb & MOUSEB_RIGHT) && (!(prevmouseb & MOUSEB_RIGHT));
    wheel = mousewheel;
    if (click)
    {
      if ((mousex < l.x) || (mousex >= l.x + l.w) || (mousey < l.y) || (mousey >= l.y + l.h)) result = 0;
      else if ((mousey == l.y + l.ybtn) && (mousex >= bx) && (mousex < bx + 11)) result = 1;
      else if ((mousey == l.y + l.ybtn) && (mousex >= bx + 15) && (mousex < bx + 27)) result = 0;
      else if ((mousey == l.y + l.yout) && (mousex >= l.x + 14) && (mousex < l.x + 33))
      {
        cur = 0;
        toggleoutput();
      }
      else if ((mousey >= l.y + l.yboard) && (mousey < l.y + l.yboard + l.nr) && (boardtop + mousey - l.y - l.yboard < numrows))
      {
        r = boardtop + mousey - l.y - l.yboard;
        cur = 1 + r;
        if ((mousex >= l.x + COL_USE) && (mousex < l.x + COL_USE + 3))
          toggleuse(r);
      }
      else if ((mousey >= l.y + l.ysong) && (mousey < l.y + l.ysong + DLG_SONGROWS))
      {
        s = mousey - l.y - l.ysong;
        cur = songfirst + s;
        if ((mousex >= l.x + COL_CHOICE) && (mousex < l.x + COL_CHOICE + COL_CHOICEW)) cycletoken(s, 1);
      }
    }
    else if (rclick)
    {
      if ((mousey >= l.y + l.ysong) && (mousey < l.y + l.ysong + DLG_SONGROWS) &&
          (mousex >= l.x + COL_CHOICE) && (mousex < l.x + COL_CHOICE + COL_CHOICEW))
      {
        s = mousey - l.y - l.ysong;
        cur = songfirst + s;
        cycletoken(s, -1);
      }
    }
    if ((wheel) && (mousey >= l.y + l.ysong) && (mousey < l.y + l.ysong + DLG_SONGROWS) &&
        (mousex >= l.x) && (mousex < l.x + l.w))
    {
      s = mousey - l.y - l.ysong;
      cur = songfirst + s;
      cycletoken(s, wheel > 0 ? -1 : 1);
    }
    if (result != -2) break;

    // Keyboard
    switch (rawkey)
    {
      case KEY_ESC:
      result = 0;
      break;

      case KEY_ENTER:
      result = (cur == cancelpos) ? 0 : 1;
      break;

      case KEY_UP:
      if ((shiftpressed) && (cur >= 1) && (cur <= numrows))
      {
        r = cur - 1;
        if (r > 0)
        {
          BOARDROW tmp = rows[r - 1];

          rows[r - 1] = rows[r];
          rows[r] = tmp;
          cur--;
        }
      }
      else if (cur > lastrow) cur = lastrow;
      else if (cur > 0) cur--;
      break;

      case KEY_DOWN:
      if ((shiftpressed) && (cur >= 1) && (cur <= numrows))
      {
        r = cur - 1;
        if (r < numrows - 1)
        {
          BOARDROW tmp = rows[r + 1];

          rows[r + 1] = rows[r];
          rows[r] = tmp;
          cur++;
        }
      }
      else if (cur < lastrow) cur++;
      break;

      case KEY_TAB:
      cur = (cur + (shiftpressed ? ncur - 1 : 1)) % ncur;
      break;

      case KEY_LEFT:
      case KEY_RIGHT:
      if (cur == 0) toggleoutput();
      else if ((cur >= songfirst) && (cur <= lastrow)) cycletoken(cur - songfirst, rawkey == KEY_RIGHT ? 1 : -1);
      else if (cur >= applypos) cur = (cur == applypos) ? cancelpos : applypos;
      break;

      case KEY_SPACE:
      if (cur == 0) toggleoutput();
      else if (cur <= numrows)
        toggleuse(cur - 1);
      else if (cur <= lastrow) cycletoken(cur - songfirst, 1);
      else result = (cur == cancelpos) ? 0 : 1;
      break;

      case KEY_DEL:
      case KEY_BACKSPACE:
      if ((cur >= songfirst) && (cur <= lastrow)) memset(&tokens[cur - songfirst], 0, sizeof tokens[0]);
      break;
    }
    if ((result == -2) && (key) && (!ctrlpressed) && (!altpressed) && (!metapressed))
    {
      if ((key == 'a') || (key == 'A') || (key == 'r') || (key == 'R')) result = 1;
      else if ((key == 'c') || (key == 'C')) result = 0;
      else if ((key == '0') && (cur >= songfirst) && (cur <= lastrow))
      {
        memset(&tokens[cur - songfirst], 0, sizeof tokens[0]);
        tokens[cur - songfirst].mode = TOK_OFF;
      }
    }
    if ((cur >= 1) && (cur <= numrows)) showboardrow(&l, cur - 1);
  }
  key = 0;
  rawkey = 0;
  return result == 1;
}

/**
 * @brief: Open the USBSID-Pico dialog: output switch, board list with use marks and
 *         order, and the chip choice for each song SID. Stops the song first.
 *         Apply stores the result through sound_usbsidapply() and reconnects, Cancel
 *         restores the sound with the unchanged settings.
 */
void usbsiddialog(void)
{
  char attached[MAX_PATHNAME];
  char inuse[MAX_PATHNAME];
  char boards[MAX_PATHNAME];
  char map[MAX_PATHNAME];
  int count, apply, nboards, nsids;

  if (!sound_usbsidbuilt())
  {
    setstatusmessage("USBSID-Pico support is not built into this version");
    return;
  }

  stopsong();
  nboards = sound_usbsidboardcount();
  nsids = sound_usbsidtotalsids();
  statewarn = 0;
  if (sound_usbsidlost())
  {
    snprintf(statetext, sizeof statetext, "Output: lost, playing on reSID");
    statewarn = 1;
  }
  else if (sound_usbsidactive())
    snprintf(statetext, sizeof statetext, "Connected: %d board%s, %d SID%s", nboards, nboards == 1 ? "" : "s", nsids, nsids == 1 ? "" : "s");
  else if (usbsid) snprintf(statetext, sizeof statetext, "Not connected, playing on reSID");
  else snprintf(statetext, sizeof statetext, "Output off, playing on reSID");

  count = sound_usbsidboards(attached, sizeof attached, inuse, sizeof inuse);
  outon = usbsid ? 1 : 0;
  probed = 0;
  soundstopped = 0;
  numinfo = 0;
  probeboards();
  parsemap(usbsidmap);
  buildrows(attached, count, inuse);

  apply = rundialog();
  if (apply)
  {
    buildboards(boards, sizeof boards);
    buildmap(map, sizeof map, 1);
    sound_usbsidapply(outon, boards, map);
  }
  else if (soundstopped) do_sound_init();
  printmainscreen();
  key = 0;
  rawkey = 0;
}

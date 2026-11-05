// C64 GrooveGoat - MIDI files: reader, the MIDI importer (roles,
// melody/bass lines, chords checked against everything that sounds, voices
// for 1-8 SIDs) and the SID loudness model

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "gsgcomp.h"
#include "gsgtimbre.h"
#include "gsgmidi.h"

static double pyround(double x)
{
  double f = floor(x), d = x - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return fmod(f, 2.0) == 0 ? f : f + 1;
}

// ---------------------------------------------------------------------------
// MIDI file
// ---------------------------------------------------------------------------

typedef struct { int start, end, pitch, vel, channel, track, program; } MNOTE_;
typedef struct { int tick, us; } MTEMPO;
typedef struct { int tick, num, den; } MSIG;
typedef struct { int tick, val; } MCCV;

typedef struct
{
  int ppq, format;
  MNOTE_ *notes;
  int nnotes, capnotes;
  MTEMPO *tempos;
  int ntempos, captempos;
  MSIG sigs[64];
  int nsigs;
  char trackname[256][64];
  int hasname[256];
  int prog[256][16], hasprog[256][16];
  MCCV *cc[16][2];          // controller 7 and 11 per channel
  int ncc[16][2], capcc[16][2];
  char title[64];
} MFILE;

static void mf_free(MFILE *m)
{
  int c, k;
  free(m->notes);
  free(m->tempos);
  for (c = 0; c < 16; c++)
    for (k = 0; k < 2; k++) free(m->cc[c][k]);
  free(m);
}

static int varlen(const unsigned char *d, int n, int *pos, int *ok)
{
  int v = 0, k;
  for (k = 0; k < 4; k++)
  {
    int b;
    if (*pos >= n) { *ok = 0; return 0; }
    b = d[(*pos)++];
    v = (v << 7) | (b & 0x7f);
    if (!(b & 0x80)) return v;
  }
  *ok = 0;
  return v;
}

typedef struct { int ch, pitch, tick, vel, prog, used; } ACTIVE;

static void addnote(MFILE *m, int start, int end, int pitch, int vel, int ch, int track, int prog)
{
  MNOTE_ *n;
  if (m->nnotes >= m->capnotes) m->notes = (MNOTE_ *)realloc(m->notes, sizeof(MNOTE_) * (m->capnotes = m->capnotes * 2 + 1024));
  n = &m->notes[m->nnotes++];
  n->start = start;
  n->end = end > start + 1 ? end : start + 1;
  n->pitch = pitch;
  n->vel = vel;
  n->channel = ch;
  n->track = track;
  n->program = prog;
}

static void read_track(MFILE *m, const unsigned char *d, int n, int track)
{
  int pos = 0, tick = 0, status = 0, program[16] = {0}, ok = 1, k;
  static ACTIVE act[4096];
  int nact = 0;
  while (pos < n)
  {
    int delta = varlen(d, n, &pos, &ok), b, kind, ch;
    if (!ok) break;
    tick += delta;
    if (pos >= n) break;
    b = d[pos];
    if (b & 0x80)
    {
      status = b;
      pos++;
    }
    else if (status == 0) break;
    if (status == 0xff)
    {
      int mtype, ln;
      if (pos >= n) break;
      mtype = d[pos++];
      ln = varlen(d, n, &pos, &ok);
      if ((!ok) || (pos + ln > n)) break;
      if ((mtype == 0x51) && (ln >= 3))
      {
        int us = (d[pos] << 16) | (d[pos + 1] << 8) | d[pos + 2];
        if (us > 0)
        {
          if (m->ntempos >= m->captempos) m->tempos = (MTEMPO *)realloc(m->tempos, sizeof(MTEMPO) * (m->captempos = m->captempos * 2 + 64));
          m->tempos[m->ntempos].tick = tick;
          m->tempos[m->ntempos].us = us;
          m->ntempos++;
        }
      }
      else if ((mtype == 0x58) && (ln >= 2) && (m->nsigs < 64))
      {
        m->sigs[m->nsigs].tick = tick;
        m->sigs[m->nsigs].num = d[pos];
        m->sigs[m->nsigs].den = 1 << d[pos + 1];
        m->nsigs++;
      }
      else if ((mtype == 0x03) && (track < 256))
      {
        char txt[64];
        int l = ln < 63 ? ln : 63, s = 0, e;
        memcpy(txt, d + pos, l);
        txt[l] = 0;
        for (k = 0; k < l; k++)
          if ((unsigned char)txt[k] > 127) txt[k] = '?';
        while ((txt[s] == ' ') || (txt[s] == '\t')) s++;
        e = strlen(txt);
        while ((e > s) && ((txt[e - 1] == ' ') || (txt[e - 1] == '\t') || (txt[e - 1] == '\r') || (txt[e - 1] == '\n'))) e--;
        txt[e] = 0;
        if ((txt[s]) && (!m->hasname[track]))
        {
          snprintf(m->trackname[track], 64, "%s", txt + s);
          m->hasname[track] = 1;
          if ((track == 0) && (!m->title[0])) snprintf(m->title, sizeof m->title, "%s", txt + s);
        }
      }
      else if (mtype == 0x2f)
      {
        pos += ln;
        break;
      }
      pos += ln;
      status = 0;
      continue;
    }
    if ((status == 0xf0) || (status == 0xf7))
    {
      int ln = varlen(d, n, &pos, &ok);
      if (!ok) break;
      pos += ln;
      status = 0;
      continue;
    }
    kind = status & 0xf0;
    ch = status & 0x0f;
    if ((kind == 0xc0) || (kind == 0xd0))
    {
      int a = pos < n ? d[pos] : 0;
      pos++;
      if (kind == 0xc0)
      {
        program[ch] = a & 0x7f;
        if ((track < 256) && (!m->hasprog[track][ch]))
        {
          m->hasprog[track][ch] = 1;
          m->prog[track][ch] = a & 0x7f;
        }
      }
      continue;
    }
    if (pos + 1 >= n + 1) break;
    {
      int a = d[pos], bb = pos + 1 < n ? d[pos + 1] : 0;
      pos += 2;
      if ((kind == 0xb0) && ((a == 7) || (a == 11)))
      {
        int w = a == 7 ? 0 : 1;
        if (m->ncc[ch][w] >= m->capcc[ch][w]) m->cc[ch][w] = (MCCV *)realloc(m->cc[ch][w], sizeof(MCCV) * (m->capcc[ch][w] = m->capcc[ch][w] * 2 + 64));
        m->cc[ch][w][m->ncc[ch][w]].tick = tick;
        m->cc[ch][w][m->ncc[ch][w]].val = bb;
        m->ncc[ch][w]++;
      }
      if ((kind == 0x90) && (bb > 0))
      {
        int f = -1;
        for (k = 0; k < nact; k++)
          if ((act[k].used) && (act[k].ch == ch) && (act[k].pitch == a)) { f = k; break; }
        if (f >= 0)
        {
          addnote(m, act[f].tick, tick, a, act[f].vel, ch, track, act[f].prog);
          act[f].used = 0;
        }
        // insertion order as Python's dict: append
        if (nact < 4096)
        {
          act[nact].ch = ch;
          act[nact].pitch = a;
          act[nact].tick = tick;
          act[nact].vel = bb;
          act[nact].prog = program[ch];
          act[nact].used = 1;
          nact++;
        }
      }
      else if ((kind == 0x80) || ((kind == 0x90) && (bb == 0)))
      {
        for (k = 0; k < nact; k++)
          if ((act[k].used) && (act[k].ch == ch) && (act[k].pitch == a))
          {
            addnote(m, act[k].tick, tick, a, act[k].vel, ch, track, act[k].prog);
            act[k].used = 0;
            break;
          }
      }
      // compact the list now and then
      if (nact > 3000)
      {
        int j = 0;
        for (k = 0; k < nact; k++)
          if (act[k].used) act[j++] = act[k];
        nact = j;
      }
    }
  }
  for (k = 0; k < nact; k++)
    if (act[k].used) addnote(m, act[k].tick, tick, act[k].pitch, act[k].vel, act[k].ch, track, act[k].prog);
}

static int cmpnote(const void *a, const void *b)
{
  const MNOTE_ *x = (const MNOTE_ *)a, *y = (const MNOTE_ *)b;
  if (x->start != y->start) return x->start < y->start ? -1 : 1;
  if (x->pitch != y->pitch) return x->pitch - y->pitch;
  return 0;
}

static unsigned rd32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static MFILE *read_midi(const unsigned char *data, int len, char *err, int errsize)
{
  MFILE *m;
  int i, hlen, ntrk, div, pos, track = 0;
  if ((len >= 12) && (!memcmp(data, "RIFF", 4)) && (!memcmp(data + 8, "RMID", 4)))
  {
    for (i = 0; i + 4 <= len; i++)
      if (!memcmp(data + i, "MThd", 4)) break;
    if (i + 4 > len)
    {
      snprintf(err, errsize, "RMID file without MIDI data");
      return NULL;
    }
    data += i;
    len -= i;
  }
  if ((len < 14) || (memcmp(data, "MThd", 4)))
  {
    for (i = 0; (i + 4 <= len) && (i < 4096); i++)
      if (!memcmp(data + i, "MThd", 4)) break;
    if ((i + 4 > len) || (i >= 4096))
    {
      snprintf(err, errsize, "Not a MIDI file (MThd header missing)");
      return NULL;
    }
    data += i;
    len -= i;
  }
  if (len < 14)
  {
    snprintf(err, errsize, "Not a MIDI file");
    return NULL;
  }
  m = (MFILE *)calloc(1, sizeof(MFILE));
  hlen = rd32(data + 4);
  m->format = (data[8] << 8) | data[9];
  ntrk = (data[10] << 8) | data[11];
  div = (data[12] << 8) | data[13];
  if (div & 0x8000)
  {
    int fps = 256 - (div >> 8), tpf = div & 0xff;
    m->ppq = fps * tpf / 2;
    if (m->ppq < 1) m->ppq = 1;
    m->tempos = (MTEMPO *)malloc(sizeof(MTEMPO) * 64);
    m->captempos = 64;
    m->tempos[0].tick = 0;
    m->tempos[0].us = 500000;
    m->ntempos = 1;
  }
  else m->ppq = div ? div : 480;
  pos = 8 + hlen;
  while ((pos + 8 <= len) && (track < (ntrk > 1 ? ntrk : 1) + 64))
  {
    int clen = (int)rd32(data + pos + 4);
    const unsigned char *chunk = data + pos + 8;
    int isTrk = !memcmp(data + pos, "MTrk", 4);
    pos += 8;
    if (clen < 0) break;
    if (pos + clen > len) clen = len - pos;
    pos += clen;
    if (!isTrk) continue;
    read_track(m, chunk, clen, track);
    track++;
  }
  // tempos: sorted, one per tick (the last one wins), first at tick 0
  {
    int a, b;
    if ((!m->ntempos) || (m->tempos[0].tick != 0))
    {
      m->tempos = (MTEMPO *)realloc(m->tempos, sizeof(MTEMPO) * (m->ntempos + 2));
      memmove(m->tempos + 1, m->tempos, sizeof(MTEMPO) * m->ntempos);
      m->tempos[0].tick = 0;
      m->tempos[0].us = 500000;
      m->ntempos++;
    }
    for (a = 1; a < m->ntempos; a++)
    {
      MTEMPO t = m->tempos[a];
      b = a - 1;
      while ((b >= 0) && ((m->tempos[b].tick > t.tick) || ((m->tempos[b].tick == t.tick) && (m->tempos[b].us > t.us)))) { m->tempos[b + 1] = m->tempos[b]; b--; }
      m->tempos[b + 1] = t;
    }
    // dedup: Python sorted the list (tick, us) and then kept the last per tick in a dict
    b = 0;
    for (a = 0; a < m->ntempos; a++)
    {
      if ((b) && (m->tempos[b - 1].tick == m->tempos[a].tick)) m->tempos[b - 1] = m->tempos[a];
      else m->tempos[b++] = m->tempos[a];
    }
    m->ntempos = b;
  }
  {
    int c, w, a, b;
    for (c = 0; c < 16; c++)
      for (w = 0; w < 2; w++)
        for (a = 1; a < m->ncc[c][w]; a++)
        {
          MCCV t = m->cc[c][w][a];
          b = a - 1;
          while ((b >= 0) && (m->cc[c][w][b].tick > t.tick)) { m->cc[c][w][b + 1] = m->cc[c][w][b]; b--; }
          m->cc[c][w][b + 1] = t;
        }
    for (a = 1; a < m->nsigs; a++)
    {
      MSIG t = m->sigs[a];
      b = a - 1;
      while ((b >= 0) && ((m->sigs[b].tick > t.tick) || ((m->sigs[b].tick == t.tick) &&
             ((m->sigs[b].num > t.num) || ((m->sigs[b].num == t.num) && (m->sigs[b].den > t.den)))))) { m->sigs[b + 1] = m->sigs[b]; b--; }
      m->sigs[b + 1] = t;
    }
  }
  if (!m->nsigs)
  {
    m->sigs[0].tick = 0;
    m->sigs[0].num = 4;
    m->sigs[0].den = 4;
    m->nsigs = 1;
  }
  // stable sort by (start, pitch)
  {
    MNOTE_ *tmp = (MNOTE_ *)malloc(sizeof(MNOTE_) * (m->nnotes + 1));
    int w, a, b, k;
    for (w = 1; w < m->nnotes; w *= 2)
    {
      for (a = 0; a < m->nnotes; a += 2 * w)
      {
        int mid = a + w < m->nnotes ? a + w : m->nnotes, e = a + 2 * w < m->nnotes ? a + 2 * w : m->nnotes;
        int i1 = a, i2 = mid;
        k = a;
        while ((i1 < mid) && (i2 < e))
        {
          if (cmpnote(&m->notes[i2], &m->notes[i1]) < 0) tmp[k++] = m->notes[i2++];
          else tmp[k++] = m->notes[i1++];
        }
        while (i1 < mid) tmp[k++] = m->notes[i1++];
        while (i2 < e) tmp[k++] = m->notes[i2++];
      }
      memcpy(m->notes, tmp, sizeof(MNOTE_) * m->nnotes);
    }
    free(tmp);
    (void)b;
  }
  if (!m->nnotes)
  {
    snprintf(err, errsize, "The MIDI file contains no notes");
    mf_free(m);
    return NULL;
  }
  return m;
}

static int mf_endtick(MFILE *m)
{
  int k, e = 0;
  for (k = 0; k < m->nnotes; k++)
    if (m->notes[k].end > e) e = m->notes[k].end;
  return e;
}

static int mf_tempo_at(MFILE *m, int tick)
{
  int k, t = 500000;
  for (k = 0; k < m->ntempos; k++)
  {
    if (m->tempos[k].tick <= tick) t = m->tempos[k].us;
    else break;
  }
  return t;
}

static int mf_cc_at(MFILE *m, int ch, int w, int tick, int def)
{
  int k, v = def;
  for (k = 0; k < m->ncc[ch][w]; k++)
  {
    if (m->cc[ch][w][k].tick <= tick) v = m->cc[ch][w][k].val;
    else break;
  }
  return v;
}

static double mf_seconds(MFILE *m, double tick)
{
  double sec = 0;
  int last = 0, us = 500000, k;
  for (k = 0; k < m->ntempos; k++)
  {
    if (m->tempos[k].tick >= tick) break;
    sec += (m->tempos[k].tick - last) * (double)us / 1e6 / m->ppq;
    last = m->tempos[k].tick;
    us = m->tempos[k].us;
  }
  return sec + (tick - last) * (double)us / 1e6 / m->ppq;
}

// ---------------------------------------------------------------------------
// Loudness on the SID
// ---------------------------------------------------------------------------

static const int RATE[16] = {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};
#define CYC_FRAME 19656

static int exp_mult(int level)
{
  if (level > 0x5d) return 1;
  if (level > 0x36) return 2;
  if (level > 0x1a) return 4;
  if (level > 0x0e) return 8;
  if (level > 0x06) return 16;
  if (level > 0x00) return 30;
  return 1;
}

typedef struct { int ad, sr, gate, total; double v; int used; } ENVC;
#define ENVCACHE 65536
static ENVC *envcache = NULL;
static int nenvcache = 0;

static double env_db(int ad, int sr, int gate_frames, int total_frames)
{
  unsigned h = (((unsigned)ad * 131 + sr) * 2654435761u ^ (unsigned)gate_frames * 40503u ^ (unsigned)total_frames * 9973u) & (ENVCACHE - 1);
  int a = ad >> 4, d = ad & 15, s = sr >> 4, r = sr & 15, sus = s * 17, sub = 4, nsteps, k, lvl = 0, phase = 0;
  double step_c = CYC_FRAME / (double)sub, gate, t = 0, pending = 0, *out;
  PYSUM sumv;
  int nvals = 0;
  if (!envcache) envcache = (ENVC *)calloc(ENVCACHE, sizeof(ENVC));
  if (nenvcache > ENVCACHE / 2)
  {
    memset(envcache, 0, sizeof(ENVC) * ENVCACHE);
    nenvcache = 0;
  }
  while ((envcache[h].used) && ((envcache[h].ad != ad) || (envcache[h].sr != sr) || (envcache[h].gate != gate_frames) || (envcache[h].total != total_frames)))
    h = (h + 1) & (ENVCACHE - 1);
  if (envcache[h].used) return envcache[h].v;
  ps_init(&sumv);
  nsteps = total_frames * sub;
  if (nsteps < 1) nsteps = 1;
  gate = (double)((gate_frames < total_frames ? gate_frames : total_frames) > 1 ? (gate_frames < total_frames ? gate_frames : total_frames) : 1) * CYC_FRAME;
  out = (double *)malloc(sizeof(double) * nsteps);
  for (k = 0; k < nsteps; k++)
  {
    double t_end = (k + 1) * step_c, acc = 0;
    while (t < t_end)
    {
      double lim, period, dt;
      if ((phase != 2) && (t >= gate)) { phase = 2; pending = 0; }
      lim = phase == 2 ? t_end : (t_end < gate ? t_end : gate);
      if (((phase == 1) && (lvl <= sus)) || ((phase == 2) && (lvl == 0)))
      {
        acc += (lvl / 255.0) * (lvl / 255.0) * (lim - t);
        t = lim;
        continue;
      }
      if (phase == 0) period = RATE[a];
      else if (phase == 1) period = RATE[d] * exp_mult(lvl);
      else period = RATE[r] * exp_mult(lvl);
      if (pending <= 0) pending = period;
      dt = pending < lim - t ? pending : lim - t;
      acc += (lvl / 255.0) * (lvl / 255.0) * dt;
      t += dt;
      pending -= dt;
      if (pending <= 1e-9)
      {
        if (phase == 0)
        {
          lvl++;
          if (lvl >= 255) { lvl = 255; phase = 1; }
        }
        else if (phase == 1) { if (lvl > sus) lvl--; }
        else if (lvl > 0) lvl--;
      }
    }
    out[k] = acc / step_c;
  }
  {
    int win = 5 * sub, i;
    for (i = 0; i < nsteps; i += win)
    {
      int j, e = i + win < nsteps ? i + win : nsteps;
      double p;
      PYSUM ps;
      ps_init(&ps);
      for (j = i; j < e; j++) ps_add(&ps, out[j]);
      p = ps_get(&ps) / (e - i);
      p = 10.0 * log10(p > 1e-9 ? p : 1e-9);
      ps_add(&sumv, p > -60.0 ? p : -60.0);
      nvals++;
    }
  }
  free(out);
  envcache[h].used = 1;
  nenvcache++;
  envcache[h].ad = ad;
  envcache[h].sr = sr;
  envcache[h].gate = gate_frames;
  envcache[h].total = total_frames;
  envcache[h].v = ps_get(&sumv) / nvals;
  return envcache[h].v;
}

typedef struct { char name[48]; double v; } TIMB;
static TIMB *timb = NULL;
static int ntimb = 0;
static char catdef[32][12];
static double catdefv[32];
static int ncatdef = 0;

static int cmptimb(const void *a, const void *b) { return strcmp(((const TIMB *)a)->name, ((const TIMB *)b)->name); }

static double timbre_db(const SGINS *i)
{
  const char *key = i->loudref[0] ? i->loudref : i->name;
  TIMB k, *f;
  int c;
  if (!timb)
  {
    int len = 0, cap = 0;
    char *t, *line, *next;
    for (c = 0; sg_timbretext[c]; c++) len += strlen(sg_timbretext[c]);
    t = (char *)malloc(len + 1);
    t[0] = 0;
    for (c = 0; sg_timbretext[c]; c++) strcat(t, sg_timbretext[c]);
    for (line = t; *line; line = next)
    {
      char *tab;
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      tab = strchr(line, '\t');
      if (!tab) continue;
      *tab = 0;
      if (ntimb >= cap) timb = (TIMB *)realloc(timb, sizeof(TIMB) * (cap = cap * 2 + 256));
      snprintf(timb[ntimb].name, 48, "%s", line);
      timb[ntimb].v = atof(tab + 1);
      ntimb++;
    }
    free(t);
    if (!timb) timb = (TIMB *)calloc(1, sizeof(TIMB));
    qsort(timb, ntimb, sizeof(TIMB), cmptimb);
    {
      const char *p = sg_catdefault;
      while ((*p) && (ncatdef < 32))
      {
        const char *eq = strchr(p, '='), *sp;
        if (!eq) break;
        snprintf(catdef[ncatdef], 12, "%.*s", (int)(eq - p), p);
        catdefv[ncatdef] = atof(eq + 1);
        ncatdef++;
        sp = strchr(eq, ' ');
        if (!sp) break;
        p = sp + 1;
      }
    }
  }
  snprintf(k.name, 48, "%s", key);
  f = (TIMB *)bsearch(&k, timb, ntimb, sizeof(TIMB), cmptimb);
  if (f) return f->v;
  for (c = 0; c < ncatdef; c++)
    if (!strcmp(catdef[c], i->cat)) return catdefv[c];
  return -20.0;
}

static double predict_db(const SGINS *i, int ad, int sr, int frames)
{
  int gt = i->gt & 0x3f;
  return timbre_db(i) + env_db(ad, sr, frames - gt > 1 ? frames - gt : 1, frames > 1 ? frames : 1);
}

static int choose_cmd(const SGINS *i, double target, int frames, double tolerance, int *cmd, int *data)
{
  int ad = i->ad, sr = i->sr, s0 = sr >> 4, d0 = ad & 15, have = 0, bc = 0, bd = 0, s;
  double base = predict_db(i, ad, sr, frames), best = 0;
  if (fabs(base - target) <= tolerance) return 0;
  if (s0 > 0)
  {
    for (s = 1; s < 16; s++)
    {
      int nsr = (s << 4) | (sr & 15);
      double err = fabs(predict_db(i, ad, nsr, frames) - target) + 0.05 * abs(s - s0);
      if ((!have) || (err < best)) { have = 1; best = err; bc = 6; bd = nsr; }
    }
  }
  else
  {
    for (s = 0; s < 16; s++)
    {
      int nad = (ad & 0xf0) | s;
      double err = fabs(predict_db(i, nad, sr, frames) - target) + 0.15 * abs(s - d0);
      if ((!have) || (err < best)) { have = 1; best = err; bc = 5; bd = nad; }
    }
  }
  if (!have) return 0;
  if (((bc == 6) && (bd == sr)) || ((bc == 5) && (bd == ad))) return 0;
  *cmd = bc;
  *data = bd;
  return 1;
}

static double midi_db(int vel, int cc7, int cc11)
{
#define F(x) (40.0 * log10(((x) > 1 ? (x) : 1) / 127.0))
  return F(vel) + F(cc7) + F(cc11);
#undef F
}

// ---------------------------------------------------------------------------
// General MIDI
// ---------------------------------------------------------------------------

static const char *GM_LEAD[128] = {
  "Fluegel", "Piano", "Synth Piano", "Honky Tonk", "Rhodes", "FM Piano", "Cembalo", "Clavinet",
  "Celesta", "Glockenspiel", "Spieluhr", "Vibraphon", "Marimba Holz", "Xylophon", "Roehrenglocken", "Hackbrett",
  "Orgel Drawbar", "Orgel Perkussiv", "Orgel Rock", "Orgel Kirche", "Harmonium", "Akkordeon", "Mundharmonika", "Akkordeon",
  "Gitarre Nylon", "Gitarre akust", "Gitarre Jazz", "Gitarre clean", "Gitarre Funk", "Gitarre crunch", "Gitarre verz", "Gitarre Flageo",
  "Bass Akustik", "E-Bass Finger", "E-Bass Pick", "Fretless", "Slap Bass", "Bass Funk", "Synth Bass 1", "Synth Bass 2",
  "Violine", "Viola", "Cello", "Kontrabass Bog", "Tremolo Str", "Pizzicato", "Harfe", "Pizzicato",
  "Streicher warm", "Streicher hell", "Synth Str", "PWM Flaeche", "Chor Aah", "Chor Ooh", "Stimme Synth", "Blech",
  "Trompete", "Posaune", "Tuba", "Trompete", "Horn", "Blechsatz", "Brass Synth", "Hoover Lead",
  "Saxophon", "Saxophon Alt", "Saxophon Tenor", "Saxophon", "Oboe", "Englischhorn", "Fagott", "Klarinette",
  "Piccolo", "Querfloete", "Blockfloete", "Panfloete", "Okarina", "Shakuhachi", "Pfeife", "Okarina",
  "Square 50", "Lead Saege", "Floete", "Pluck Lead", "Gitarre verz", "Stimme Synth", "Saege Breit", "Hubbard Lead",
  "Pad Glas", "Pad Warm PWM", "PWM Flaeche", "Chor", "Tagelharpa", "Glocke", "Pad Hell", "Sweep Flaeche",
  "Sanfter Lead", "Weiche Flaeche", "Glocke", "Sanfter Lead", "Saege Breit", "Weiche Flaeche", "Chor", "Sanfter Lead",
  "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Dudelsack", "Violine", "Oboe",
  "Spieluhr", "Marimba", "Steeldrum", "Marimba", "Pizzicato", "Pizzicato", "Pluck Lead", "Sanfter Lead",
  "Sanfter Lead", "Sanfter Lead", "Sanfter Lead", "Sanfter Lead", "Sanfter Lead", "Sanfter Lead", "Sanfter Lead", "Sanfter Lead"};

static const char *gm_lead(int p) { return GM_LEAD[p < 0 ? 0 : p > 127 ? 127 : p]; }

static const char *gm_bass(int p, int dance)
{
  switch (p)
  {
    case 32: return "Bass Akustik";
    case 33: return "E-Bass Finger";
    case 34: return "E-Bass Pick";
    case 35: return "Fretless";
    case 36: return "Slap Bass";
    case 37: return "Bass Funk";
    case 38: return dance ? "Bass Fett" : "Synth Bass 1";
    case 39: return !dance ? "Synth Bass 2" : "Tiefbass Puls";
    case 42: return "Cello";
    case 43: return "Kontrabass Bog";
    case 45: return "Bass Kontra Pz";
    case 58: return "Bass Tuba 2";
    case 70: return "Fagott";
  }
  if (p < 8) return !dance ? "Tiefes Piano" : "Tiefbass Puls";
  if ((p >= 16) && (p < 24)) return "Bass Orgel";
  if ((p >= 24) && (p < 32)) return "Tiefe Gitarre";
  if ((p >= 40) && (p < 56)) return "Tiefe Streicher";
  if ((p >= 56) && (p < 64)) return "Bass Tuba 2";
  if ((p >= 80) && (p < 104)) return "Bass Saege Fett";
  return dance ? "Tiefbass Puls" : "E-Bass Finger";
}

static const char *gm_arp(int p)
{
  int fam = p / 8;
  if ((p == 4) || (p == 5) || (p == 11)) return "Arp E-Piano";
  if (p == 19) return "Arp Kirche";
  if ((fam == 0) || (fam == 1)) return "Arp Puls";
  if (fam == 2) return "Arp Orgel";
  if (fam == 3) return ((p == 29) || (p == 30) || (p == 84)) ? "Arp Power" : "Arp Gitarre";
  if ((p == 50) || (p == 51) || (fam == 11)) return "Arp Flaeche";
  if ((fam == 5) || (fam == 6)) return "Arp Streicher";
  if (fam == 7) return "Arp Stab";
  if (fam == 10) return "Arp Saege";
  return "Arp Puls";
}

static const char *gm_drum_ins(int k)
{
  static const struct { int k; const char *n; } T[] = {
    {37, "Snare Rimklick"}, {41, "Tom Stand"}, {43, "Tom tief 2"}, {44, "Hihat Pedal"}, {45, "Tom tief"},
    {47, "Tom mittel 2"}, {48, "Tom mittel"}, {49, "Crash lang"}, {50, "Tom hoch 2"}, {51, "Ride"}, {52, "China"},
    {53, "Ride Glocke"}, {54, "Tamburin"}, {55, "Splash"}, {56, "Kuhglocke"}, {57, "Crash kurz"}, {58, "Guiro"},
    {59, "Ride"}, {60, "Bongo hoch"}, {61, "Bongo"}, {62, "Conga hoch"}, {63, "Conga hoch"}, {64, "Conga tief"},
    {65, "Timbale"}, {66, "Timbale"}, {67, "Agogo"}, {68, "Agogo"}, {69, "Cabasa"}, {70, "Shaker"}, {71, "Triangel"},
    {72, "Triangel"}, {73, "Guiro"}, {74, "Guiro"}, {75, "Claves"}, {76, "Holzblock"}, {77, "Holzblock"}, {78, "Bongo"},
    {79, "Bongo"}, {80, "Triangel"}, {81, "Triangel"}};
  int c;
  for (c = 0; c < (int)(sizeof T / sizeof T[0]); c++)
    if (T[c].k == k) return T[c].n;
  return NULL;
}

static int gm_drum(int k)
{
  if ((k == 35) || (k == 36)) return DL_K;
  if ((k == 38) || (k == 40) || (k == 37)) return DL_S;
  if (k == 39) return DL_C;
  if ((k == 42) || (k == 44)) return DL_H;
  if (k == 46) return DL_O;
  if ((k == 49) || (k == 52) || (k == 55) || (k == 57)) return DL_X;
  if ((k == 51) || (k == 53) || (k == 59)) return DL_R;
  if ((k == 41) || (k == 43) || (k == 45) || (k == 47) || (k == 48) || (k == 50)) return DL_T;
  return DL_P;
}

static const int DRUM_PRIO[9] = {DL_X, DL_S, DL_C, DL_K, DL_T, DL_O, DL_H, DL_R, DL_P};

static const char *ARPFAM(const char *t, int i)
{
  static const char *F[][5] = {
    {"Arp Puls", "Arp Retro 3", "Arp Chip flink", "Arp Kombi", "Arp Cembalo"},
    {"Arp E-Piano", "Arp Glas", "Arp Kombi", "Arp Pluck 2000", 0}, {"Arp Kirche", "Arp Orgel", "Arp Flaeche", 0, 0},
    {"Arp Orgel", "Arp Kirche", "Arp Akkordeon", "Arp Retro 3", 0}, {"Arp Gitarre", "Arp Harfe", "Arp Western", "Arp Clavinet", 0},
    {"Arp Power", "Arp Gitarre", "Arp Saege", 0, 0}, {"Arp Flaeche", "Arp Streicher", "Arp Langsam", "Arp String Machine", "Arp Glas"},
    {"Arp Streicher", "Arp Flaeche", "Arp String Machine", "Arp Langsam", 0}, {"Arp Stab", "Arp Brass", "Arp Rave", "Arp Profi", 0},
    {"Arp Saege", "Arp Trance", "Arp Profi Saege", "Arp Rave", 0}};
  int k;
  for (k = 0; k < (int)(sizeof F / sizeof F[0]); k++)
    if (!strcmp(F[k][0], t)) return (i + 1 < 5) ? F[k][i + 1] : NULL;
  return NULL;
}

typedef struct { const char *kick, *snare, *clap, *hat, *ohat, *crash, *ride, *perc; } KIT;
static const KIT DANCE_KITS[3] = {
  {"Kick Stampf", "Snare 909", "Clap Fett", "Hihat 909", "Hihat offen 909", "Crash", "Ride", "Bongo"},
  {"Kick Tief 909", "Snare Trocken", "Clap 808", "Hihat kurz", "Hihat halb", "Crash lang", "Ride", "Cabasa"},
  {"Kick Punch", "Snare Rausch", "Clap Doppel", "Profi Hihat", "Open Hat 808", "Crash kurz", "Ride", "Kuhglocke 808"}};
static const KIT BAND_KITS[3] = {
  {"Kick Punch", "Snare Akustik", "Clap", "Hihat geschl", "Hihat offen", "Crash lang", "Ride", "Kuhglocke"},
  {"Kick Akustik", "Snare Rock", "Clap", "Hihat Pedal", "Hihat halb", "Crash Hell", "Ride Jazz", "Tamburin"},
  {"Kick Trocken", "Snare Linn", "Clap 808", "Hihat geschl", "Hihat offen", "Crash Dunkel", "Ride", "Holzblock"}};

static const char *MELODY_WORDS[] = {"melod", "lead", "vocal", "voice", "solo", "sing", "gesang", "stimme", "theme",
  "thema", "tune", "flute", "violin", "trumpet", "sax", "right", "rechts", "mel", 0};
static const char *BASS_WORDS[] = {"bass", "left", "links", "cello", "tuba", "contra", 0};
static const char *DRUM_WORDS[] = {"drum", "perc", "schlag", "kit", 0};
static const int QUALS[9] = {Q_MAJ, Q_MIN, Q_DOM7, Q_MIN7, Q_MAJ7, Q_SUS4, Q_DIM, Q_AUG, Q_POW};
static const int QUALS_CAREFUL[6] = {Q_MAJ, Q_MIN, Q_MIN7, Q_DOM7, Q_SUS4, Q_POW};

static int haswords(const char *name, const char **w)
{
  char low[96];
  int k;
  for (k = 0; (name[k]) && (k < 95); k++) low[k] = tolower((unsigned char)name[k]);
  low[k] = 0;
  for (k = 0; w[k]; k++)
    if (strstr(low, w[k])) return 1;
  return 0;
}

// ---------------------------------------------------------------------------
// The importer
// ---------------------------------------------------------------------------

typedef struct { int r, d, p; double v; } PN;      // (start_row, dur, pitch, level dB)

typedef struct
{
  int track, channel, program;
  char name[64];
  PN *notes;
  int n, cap;
  int role;                 // 0 harm, 1 bass, 2 mel, 3 mel2
  double bass, mel, mean, poly;
  // lvmap: (row, pitch) -> max level
  int *lvr, *lvp, nlv, caplv;
  double *lvv;
} PART;

enum { R_HARM = 0, R_BASS, R_MEL, R_MEL2 };

typedef struct { int r, d, p, part; } LN;          // line note (part -1 = none)

typedef struct
{
  COMP *c;
  MFILE *mf;
  MIDIOPTS o;
  SGRNG var_rng;
  int hasvar;
  int voices, sids, model;
  int q, bar_rows, nbars, total_rows, beat_rows;
  double tpr, grid_err, shift;
  int main_us, timesig[2];
  PART *parts;
  int nparts;
  int *drums;               // letters per row
  int *drum_gm;             // per row: GM note per letter (9 letters)
  int drum_notes;
  int (*bar_tempo)[2];
  // lv_any: (row, pitch) -> level
  double *lvany;            // [row * 128 + pitch], NAN = none
  int single;
  LN *lead, *bass, *counter;
  int nlead, nbass, ncounter;
  int *mel_source;
  // inner voices: (r, d, p, v, part)
  struct { int r, d, p; double v; int part; } *inner;
  int ninner;
  CHORD *chords;
  int *haschord, nbeats;
  double (*snd)[12];
  unsigned char *harm_onset;
  PN *harm_notes;
  int nharm;
  int harm_prog;
  int dance;
  const KIT *kit;
  KIT kitv;
  char i_bass[48], kickbass[48], t_arp[48], t_pad[48];
  int lead_map_p[128];
  char lead_map[128][48];
  // names
  char counter_names[64][48];
  int ncounter_names;
  char inner_names[256][48];
  int ninner_names;
  char lead_names[64][48];
  int nlead_names;
  // poly instruments per part key
  char (*poly_ins)[32][48];
  int *npoly_ins;
  int extra_poly[SG_MAXTRACKS], nextra_poly;
  int harmony_beats, harmony_changed, harmony_simplified, harmony_rests;
  char err[200];
} IMP;

static int part_lv(PART *p, int r, int pitch, double *v)
{
  int k;
  for (k = 0; k < p->nlv; k++)
    if ((p->lvr[k] == r) && (p->lvp[k] == pitch)) { *v = p->lvv[k]; return 1; }
  return 0;
}

static void part_setlv(PART *p, int r, int pitch, double db)
{
  int k;
  for (k = p->nlv - 1; k >= 0 && k >= p->nlv - 64; k--)
    if ((p->lvr[k] == r) && (p->lvp[k] == pitch))
    {
      if (db > p->lvv[k]) p->lvv[k] = db;
      return;
    }
  if (p->nlv >= p->caplv)
  {
    p->caplv = p->caplv * 2 + 256;
    p->lvr = (int *)realloc(p->lvr, sizeof(int) * p->caplv);
    p->lvp = (int *)realloc(p->lvp, sizeof(int) * p->caplv);
    p->lvv = (double *)realloc(p->lvv, sizeof(double) * p->caplv);
  }
  p->lvr[p->nlv] = r;
  p->lvp[p->nlv] = pitch;
  p->lvv[p->nlv] = db;
  p->nlv++;
}

static int imp_use(IMP *x, const char *name) { return comp_use(x->c, name); }

static int imp_use_arp(IMP *x, const char *tpl, int q)
{
  int k = comp_use_arp(x->c, tpl, q);
  snprintf(x->c->ins[k].loudref, sizeof x->c->ins[k].loudref, "%s", tpl);
  return k;
}

static void choose_grid(IMP *x)
{
  MFILE *m = x->mf;
  int k, n = 0, endt = mf_endtick(m);
  static const int qs[5] = {4, 3, 6, 8, 2};
  static const double pens[5] = {0.0, 0.03, 0.07, 0.09, 0.05};
  double bpm0, best = 0;
  int bestq = 4, have = 0;
  // main tempo: the one that lasts longest (first one on a tie)
  {
    int us[1024], nu = 0, j;
    double dur[1024];
    for (k = 0; k < m->ntempos; k++)
    {
      int tk = m->tempos[k].tick, tk2 = k + 1 < m->ntempos ? m->tempos[k + 1].tick : endt, f = -1;
      for (j = 0; j < nu; j++)
        if (us[j] == m->tempos[k].us) f = j;
      if (f < 0)
      {
        if (nu >= 1024) continue;
        us[nu] = m->tempos[k].us;
        dur[nu] = 0;
        f = nu++;
      }
      dur[f] += tk2 - tk > 0 ? tk2 - tk : 0;
    }
    x->main_us = 500000;
    if (nu)
    {
      int b = 0;
      for (j = 1; j < nu; j++)
        if (dur[j] > dur[b]) b = j;
      x->main_us = us[b];
    }
  }
  bpm0 = 60e6 / x->main_us;
  n = m->nnotes < 6000 ? m->nnotes : 6000;
  for (k = 0; k < 5; k++)
  {
    double fpr = 3000.0 / (bpm0 * qs[k]), tpr, err = 0, score;
    int i;
    if ((fpr < 3) && (qs[k] != 2)) continue;
    if (fpr > 24) continue;
    tpr = m->ppq / (double)qs[k];
    for (i = 0; i < n; i++)
    {
      double t = m->notes[i].start / tpr;
      err += fabs(t - pyround(t));
    }
    err /= (n > 1 ? n : 1);
    score = err + pens[k];
    if ((!have) || (score < best))
    {
      have = 1;
      best = score;
      bestq = qs[k];
      x->grid_err = err;
    }
  }
  if (!have) { bestq = 4; x->grid_err = 0; }
  if (x->o.hint_q) { bestq = x->o.hint_q; x->grid_err = 0; }
  x->q = bestq;
  x->tpr = m->ppq / (double)x->q;
  x->timesig[0] = m->sigs[0].num;
  x->timesig[1] = m->sigs[0].den;
  x->bar_rows = (int)pyround(x->timesig[0] * x->q * 4.0 / x->timesig[1]);
  if (x->bar_rows < 1) x->bar_rows = 1;
}

static void gt_tempo(IMP *x, int us, int *a, int *b)
{
  double bpm = 60e6 / us, f = 3000.0 / (bpm * x->q), err;
  int n = (int)pyround(f);
  if (n < 3) n = 3;
  err = fabs(n - f) / f;
  if ((err > 0.05) && (f >= 8))
  {
    int lo = (int)f;
    if (lo < 3) lo = 3;
    *a = lo + 1;
    *b = lo;
    return;
  }
  *a = *b = n;
}

static void score_part(IMP *x, PART *p)
{
  int k, nrows = 0, cnt = 0, fam = p->program / 8, nbars = 0;
  double sum = 0, mean, poly;
  static int *seen = NULL, seencap = 0;
  static int *bseen = NULL, bcap = 0;
  if (seencap < x->total_rows + 1) { seencap = x->total_rows + 1; seen = (int *)realloc(seen, sizeof(int) * seencap); }
  if (bcap < x->nbars + 1) { bcap = x->nbars + 1; bseen = (int *)realloc(bseen, sizeof(int) * bcap); }
  memset(seen, 0, sizeof(int) * seencap);
  memset(bseen, 0, sizeof(int) * bcap);
  for (k = 0; k < p->n; k++)
  {
    sum += p->notes[k].p;
    if (!seen[p->notes[k].r]) nrows++;
    seen[p->notes[k].r]++;
    cnt++;
    if (!bseen[p->notes[k].r / x->bar_rows]) nbars++;
    bseen[p->notes[k].r / x->bar_rows] = 1;
  }
  mean = sum / (double)p->n;
  poly = cnt / (double)nrows;
  p->mean = mean;
  p->poly = poly;
  p->bass = 0;
  if (fam == 4) p->bass += 3;
  if (haswords(p->name, BASS_WORDS)) p->bass += 2;
  p->bass += mean < 50 ? 2 : (mean < 55 ? 1 : -2);
  p->bass += poly < 1.3 ? 1 : -1;
  p->mel = 0;
  if (haswords(p->name, MELODY_WORDS)) p->mel += 2;
  if (((fam >= 7) && (fam <= 10)) || (p->program == 52) || (p->program == 53) || (p->program == 54) || (p->program == 85)) p->mel += 1.5;
  p->mel += ((mean >= 60) && (mean <= 86)) ? 1 : -1;
  p->mel += poly < 1.3 ? 1 : (poly < 1.8 ? -0.5 : -1.5);
  {
    double v = p->n / (double)((nbars > 1 ? nbars : 1) * 4);
    p->mel += v < 1.0 ? v : 1.0;
  }
}

static void assign_roles(IMP *x)
{
  int k, *cand, nc = 0;
  for (k = 0; k < x->nparts; k++) x->parts[k].role = R_HARM;
  for (k = 0; k < x->nparts; k++)
    if (x->parts[k].bass >= 3) x->parts[k].role = R_BASS;
  cand = (int *)malloc(sizeof(int) * (x->nparts + 1));
  for (k = 0; k < x->nparts; k++)
    if (x->parts[k].role != R_BASS) cand[nc++] = k;
  // stable sort by -mel
  {
    int a, b;
    for (a = 1; a < nc; a++)
    {
      int t = cand[a];
      b = a - 1;
      while ((b >= 0) && (x->parts[cand[b]].mel < x->parts[t].mel)) { cand[b + 1] = cand[b]; b--; }
      cand[b + 1] = t;
    }
  }
  if ((nc) && (x->parts[cand[0]].mel >= 1.0))
  {
    x->parts[cand[0]].role = R_MEL;
    for (k = 1; k < nc; k++)
      if ((x->parts[cand[k]].mel >= 2.5) && (x->parts[cand[k]].poly < 1.5)) x->parts[cand[k]].role = R_MEL2;
  }
  free(cand);
  for (k = 0; k < x->nparts; k++)
  {
    PART *p = &x->parts[k];
    char key[32];
    int o;
    snprintf(key, sizeof key, "%d:%d", p->track, p->channel);
    if (!strcmp(x->o.bass, key))
    {
      for (o = 0; o < x->nparts; o++)
        if (x->parts[o].role == R_BASS) x->parts[o].role = R_HARM;
      p->role = R_BASS;
    }
    if (!strcmp(x->o.melody, key))
    {
      for (o = 0; o < x->nparts; o++)
        if (x->parts[o].role == R_MEL) x->parts[o].role = R_HARM;
      p->role = R_MEL;
    }
  }
  if (!strcmp(x->o.bass, "none"))
    for (k = 0; k < x->nparts; k++)
      if (x->parts[k].role == R_BASS) x->parts[k].role = R_HARM;
  // roles chosen by the enhanced calculation (track names)
  for (k = 0; k < x->nparts; k++)
  {
    int j;
    for (j = 0; j < x->o.nhint_roles; j++)
      if (!strcmp(x->parts[k].name, x->o.hint_role_name[j])) x->parts[k].role = x->o.hint_role[j];
  }
  {
    int nb = 0, nh = 0;
    for (k = 0; k < x->nparts; k++)
    {
      if (x->parts[k].role == R_BASS) nb++;
      else nh++;
    }
    x->single = (nh <= 1) && (!nb);
  }
}

static int analyze(IMP *x)
{
  MFILE *m = x->mf;
  int k, start_tick = m->notes[0].start, end_tick = mf_endtick(m), rows;
  double bar_ticks;
  choose_grid(x);
  for (k = 0; k < m->nnotes; k++)
    if (m->notes[k].start < start_tick) start_tick = m->notes[k].start;
  bar_ticks = x->bar_rows * x->tpr;
  x->shift = start_tick >= bar_ticks ? floor(start_tick / bar_ticks) * bar_ticks : 0;
  if (x->o.hint_q) x->shift = 0;
  if (x->o.max_seconds > 0)
  {
    int lo = 0, hi = end_tick;
    double s0 = mf_seconds(m, x->shift);
    while (hi - lo > 1)
    {
      int mid = (lo + hi) / 2;
      if (mf_seconds(m, mid) - s0 > x->o.max_seconds) hi = mid;
      else lo = mid;
    }
    if (hi < end_tick) end_tick = hi;
  }
  rows = (int)((end_tick - x->shift) / x->tpr + 0.999);
  x->total_rows = ((rows + x->bar_rows - 1) / x->bar_rows) * x->bar_rows;
  if (x->total_rows < x->bar_rows) x->total_rows = x->bar_rows;
  if (x->total_rows > 60000)
  {
    snprintf(x->err, sizeof x->err, "The MIDI file is too long");
    return 0;
  }
  x->nbars = x->total_rows / x->bar_rows;
  x->drums = (int *)calloc(x->total_rows + 1, sizeof(int));
  x->drum_gm = (int *)calloc((x->total_rows + 1) * 9, sizeof(int));
  x->lvany = (double *)malloc(sizeof(double) * (x->total_rows + 1) * 128);
  for (k = 0; k < (x->total_rows + 1) * 128; k++) x->lvany[k] = NAN;
  x->drum_notes = 0;
  x->parts = NULL;
  x->nparts = 0;
  {
    int capparts = 0;
    for (k = 0; k < m->nnotes; k++)
    {
      MNOTE_ *n = &m->notes[k];
      int r = (int)pyround((n->start - x->shift) / x->tpr), e, j;
      const char *name = n->track < 256 ? m->trackname[n->track] : "";
      PART *p = NULL;
      double db;
      if ((r < 0) || (r >= x->total_rows)) continue;
      if ((n->channel == 9) || (haswords(name, DRUM_WORDS) && (n->channel == 9)))
      {
        if ((n->vel >= 25) && (!x->o.drums_off))
        {
          int l = gm_drum(n->pitch), li;
          x->drums[r] |= l;
          for (li = 0; li < 9; li++)
            if ((1 << li) == l) x->drum_gm[r * 9 + li] = n->pitch;
          x->drum_notes++;
        }
        continue;
      }
      for (j = 0; j < x->nparts; j++)
        if ((x->parts[j].track == n->track) && (x->parts[j].channel == n->channel)) { p = &x->parts[j]; break; }
      if (!p)
      {
        if (x->nparts >= capparts) x->parts = (PART *)realloc(x->parts, sizeof(PART) * (capparts = capparts * 2 + 16));
        p = &x->parts[x->nparts++];
        memset(p, 0, sizeof *p);
        p->track = n->track;
        p->channel = n->channel;
        p->program = (n->track < 256) && (m->hasprog[n->track][n->channel]) ? m->prog[n->track][n->channel] : n->program;
        snprintf(p->name, sizeof p->name, "%s", name);
      }
      e = (int)pyround((n->end - x->shift) / x->tpr);
      db = midi_db(n->vel, mf_cc_at(m, n->channel, 0, n->start, 100), mf_cc_at(m, n->channel, 1, n->start, 127));
      if (p->n >= p->cap) p->notes = (PN *)realloc(p->notes, sizeof(PN) * (p->cap = p->cap * 2 + 256));
      p->notes[p->n].r = r;
      p->notes[p->n].d = (e < x->total_rows ? e : x->total_rows) - r;
      if (p->notes[p->n].d < 1) p->notes[p->n].d = 1;
      p->notes[p->n].p = n->pitch;
      p->notes[p->n].v = db;
      p->n++;
      part_setlv(p, r, n->pitch, db);
      {
        double *a = &x->lvany[r * 128 + n->pitch];
        if ((isnan(*a)) || (db > *a)) *a = db;
      }
    }
  }
  {
    int j = 0;
    for (k = 0; k < x->nparts; k++)
      if (x->parts[k].n) x->parts[j++] = x->parts[k];
    x->nparts = j;
  }
  if ((!x->nparts) && (!x->drum_notes))
  {
    snprintf(x->err, sizeof x->err, "No playable notes found");
    return 0;
  }
  for (k = 0; k < x->nparts; k++)
  {
    PART *p = &x->parts[k];
    // sort the notes (tuples: r, d, p, v)
    int a, b;
    for (a = 1; a < p->n; a++)
    {
      PN t = p->notes[a];
      b = a - 1;
      while ((b >= 0) && ((p->notes[b].r > t.r) || ((p->notes[b].r == t.r) && ((p->notes[b].d > t.d) ||
             ((p->notes[b].d == t.d) && ((p->notes[b].p > t.p) || ((p->notes[b].p == t.p) && (p->notes[b].v > t.v)))))))) { p->notes[b + 1] = p->notes[b]; b--; }
      p->notes[b + 1] = t;
    }
    score_part(x, p);
  }
  assign_roles(x);
  x->bar_tempo = (int (*)[2])malloc(sizeof(int) * 2 * (x->nbars + 1));
  for (k = 0; k < x->nbars; k++)
  {
    int tick = (int)(x->shift + k * bar_ticks);
    gt_tempo(x, mf_tempo_at(m, tick), &x->bar_tempo[k][0], &x->bar_tempo[k][1]);
    if (x->o.hint_tempo[0]) { x->bar_tempo[k][0] = x->o.hint_tempo[0]; x->bar_tempo[k][1] = x->o.hint_tempo[1]; }
  }
  return 1;
}

// monophone line: the highest (lowest) note per row, overlaps cut
static int skyline(const PN *notes, int n, int top, LN *out, int part)
{
  int k, nout = 0, cur_end = -1, cur_p = -1000, havecur = 0;
  // by_row in insertion order -> processed in sorted row order
  int *rows = (int *)malloc(sizeof(int) * (n + 1)), nr = 0;
  int *bd = (int *)malloc(sizeof(int) * (n + 1)), *bp = (int *)malloc(sizeof(int) * (n + 1));
  for (k = 0; k < n; k++)
  {
    int j, f = -1;
    for (j = nr - 1; j >= 0 && j >= nr - 32; j--)
      if (rows[j] == notes[k].r) { f = j; break; }
    if (f < 0)
      for (j = 0; j < nr; j++)
        if (rows[j] == notes[k].r) { f = j; break; }
    if (f < 0)
    {
      rows[nr] = notes[k].r;
      bd[nr] = notes[k].d;
      bp[nr] = notes[k].p;
      nr++;
    }
    else if (top ? notes[k].p > bp[f] : notes[k].p < bp[f])
    {
      bd[f] = notes[k].d;
      bp[f] = notes[k].p;
    }
  }
  // sort by row
  {
    int a, b;
    for (a = 1; a < nr; a++)
    {
      int r = rows[a], d = bd[a], p = bp[a];
      b = a - 1;
      while ((b >= 0) && (rows[b] > r)) { rows[b + 1] = rows[b]; bd[b + 1] = bd[b]; bp[b + 1] = bp[b]; b--; }
      rows[b + 1] = r; bd[b + 1] = d; bp[b + 1] = p;
    }
  }
  for (k = 0; k < nr; k++)
  {
    int r = rows[k], d = bd[k], p = bp[k];
    if ((havecur) && (r < cur_end))
    {
      if ((top && p < cur_p) || ((!top) && p > cur_p)) continue;
      out[nout - 1].d = r - out[nout - 1].r;
    }
    out[nout].r = r;
    out[nout].d = d;
    out[nout].p = p;
    out[nout].part = part;
    nout++;
    cur_end = r + d;
    cur_p = p;
    havecur = 1;
  }
  free(rows);
  free(bd);
  free(bp);
  return nout;
}

static int part_has_in(PART *p, int r0, int r1)
{
  int k;
  for (k = 0; k < p->n; k++)
    if ((p->notes[k].r >= r0) && (p->notes[k].r < r1)) return 1;
  return 0;
}

static double clash_weight(const double *w, int tones)
{
  PYSUM s;
  int m;
  ps_init(&s);
  for (m = 0; m < 12; m++)
    if ((w[m]) && (!((tones >> m) & 1)) && (((tones >> ((m + 1) % 12)) & 1) || ((tones >> ((m + 11) % 12)) & 1))) ps_add(&s, w[m]);
  return ps_get(&s);
}

static int tonesof(int root, int q)
{
  int k, t = 0;
  for (k = 0; k < sg_qlen[q]; k++) t |= 1 << ((root + sg_qiv[q][k]) % 12);
  return t;
}

static int popcount12(int t)
{
  int k, n = 0;
  for (k = 0; k < 12; k++)
    if ((t >> k) & 1) n++;
  return n;
}

static int row_clashes(IMP *x, int r0, int r1, int root, int q, unsigned char *mark)
{
  int tones = tonesof(root, q), k, n = 0;
  for (k = r0; (k < r1) && (k < x->total_rows); k++)
    if (clash_weight(x->snd[k], tones) >= 0.9)
    {
      n++;
      if (mark) mark[k - r0] = 1;
    }
  return n;
}

static void detect_chords(IMP *x, const PN *src, int nsrc)
{
  int beat = x->beat_rows, bi, k, T = x->total_rows;
  int mode = x->o.harmony;               // 0 normal, 1 careful, 2 loose
  int nbeats = (T + beat - 1) / beat, prevr = -1, prevq = 0, haveprev = 0;
  double (*pcw)[12] = (double (*)[12])calloc(nbeats + 1, sizeof(double) * 12);
  unsigned char *haspcw = (unsigned char *)calloc(nbeats + 1, 1);
  double k_clash = mode == 2 ? 0.0 : mode == 1 ? 2.6 : 1.6, limit = mode == 2 ? 1.1 : mode == 1 ? 0.15 : 0.34;
  const int *quals = mode == 1 ? QUALS_CAREFUL : QUALS;
  int nq = mode == 1 ? 6 : 9;
  for (k = 0; k < nsrc; k++)
  {
    int r;
    for (r = src[k].r; (r < src[k].r + src[k].d) && (r < T); r++)
    {
      pcw[r / beat][src[k].p % 12] += 1.0;
      haspcw[r / beat] = 1;
    }
  }
  for (k = 0; k < x->nbass; k++)
  {
    int r;
    for (r = x->bass[k].r; (r < x->bass[k].r + x->bass[k].d) && (r < T); r++)
    {
      pcw[r / beat][x->bass[k].p % 12] += 1.5;
      haspcw[r / beat] = 1;
    }
  }
  // sounding rows
  x->snd = (double (*)[12])calloc(T + 1, sizeof(double) * 12);
  {
    int p;
    for (p = 0; p < x->nparts; p++)
      for (k = 0; k < x->parts[p].n; k++)
      {
        PN *n = &x->parts[p].notes[k];
        int r;
        for (r = n->r; (r < T) && (r < n->r + n->d); r++) x->snd[r][n->p % 12] += 1.0;
      }
    for (k = 0; k < x->nlead; k++)
    {
      int r;
      for (r = x->lead[k].r; (r < T) && (r < x->lead[k].r + x->lead[k].d); r++) x->snd[r][x->lead[k].p % 12] += 0.6;
    }
    for (k = 0; k < x->nbass; k++)
    {
      int r;
      for (r = x->bass[k].r; (r < T) && (r < x->bass[k].r + x->bass[k].d); r++) x->snd[r][x->bass[k].p % 12] += 0.6;
    }
  }
  x->nbeats = nbeats;
  x->chords = (CHORD *)calloc(nbeats + 1, sizeof(CHORD));
  x->haschord = (int *)calloc(nbeats + 1, sizeof(int));
  for (bi = 0; bi < nbeats; bi++)
  {
    double *w = pcw[bi], tot = 0, allw[12], sumall = 0, bestsc = 0, oldsc = 0;
    int r0, r1, bass_pc = -1, br = 0, bq = 0, orr = 0, oq = 0, haveb = 0, haveo = 0, root, qi, m;
    if (!haspcw[bi]) continue;
    for (m = 0; m < 12; m++) tot += w[m];
    if (tot < beat * 0.8) continue;
    x->harmony_beats++;
    r0 = bi * beat;
    r1 = (bi + 1) * beat < T ? (bi + 1) * beat : T;
    for (m = 0; m < 12; m++)
    {
      PYSUM s;
      int kk;
      ps_init(&s);
      for (kk = r0; kk < r1; kk++) ps_add(&s, x->snd[kk][m]);
      allw[m] = ps_get(&s);
    }
    for (k = 0; k < x->nbass; k++)
      if ((x->bass[k].r <= bi * beat) && (bi * beat < x->bass[k].r + x->bass[k].d)) bass_pc = x->bass[k].p % 12;
    {
      PYSUM s;
      ps_init(&s);
      for (m = 0; m < 12; m++) ps_add(&s, allw[m]);
      sumall = ps_get(&s);
    }
#define SCORE(rt, qq, clash, out) do { \
      int tn = tonesof(rt, qq), ii; double inside = 0, sc_; \
      for (ii = 0; ii < 12; ii++) if ((tn >> ii) & 1) inside += w[ii]; \
      sc_ = inside - 0.9 * (tot - inside) - 0.15 * tot * (popcount12(tn) - 3) / 3.0; \
      if (bass_pc == (rt)) sc_ += 0.25 * tot; \
      if (((qq) == Q_DIM) || ((qq) == Q_AUG) || ((qq) == Q_SUS4) || ((qq) == Q_POW)) sc_ -= 0.12 * tot; \
      if ((haveprev) && (prevr == (rt)) && (prevq == (qq))) sc_ += 0.12 * tot; \
      if (clash) sc_ -= k_clash * clash_weight(allw, tn) * tot / (sumall > 1.0 ? sumall : 1.0); \
      out = sc_; } while (0)
    for (root = 0; root < 12; root++)
      for (qi = 0; qi < 9; qi++)
      {
        double sc;
        SCORE(root, QUALS[qi], 0, sc);
        if ((!haveo) || (sc > oldsc)) { haveo = 1; oldsc = sc; orr = root; oq = QUALS[qi]; }
      }
    for (root = 0; root < 12; root++)
      for (qi = 0; qi < nq; qi++)
      {
        double sc;
        SCORE(root, quals[qi], 1, sc);
        if ((!haveb) || (sc > bestsc)) { haveb = 1; bestsc = sc; br = root; bq = quals[qi]; }
      }
#undef SCORE
    {
      int none = 0;
      if (mode != 2)
      {
        int n = r1 - r0, bad = row_clashes(x, r0, r1, br, bq, NULL);
        if (bad > limit * n)
        {
          int ar[2], aq[2], na = 0, bestk = 0, bk, bv = 0;
          ar[na] = br; aq[na++] = Q_POW;
          if (bass_pc >= 0) { ar[na] = bass_pc; aq[na++] = Q_POW; }
          for (bk = 0; bk < na; bk++)
          {
            int v = row_clashes(x, r0, r1, ar[bk], aq[bk], NULL);
            if ((bk == 0) || (v < bv)) { bv = v; bestk = bk; }
          }
          if (row_clashes(x, r0, r1, ar[bestk], aq[bestk], NULL) <= limit * n)
          {
            br = ar[bestk];
            bq = aq[bestk];
            x->harmony_simplified++;
          }
          else
          {
            none = 1;
            x->harmony_rests++;
          }
        }
      }
      if ((!none) && ((br != orr) || (bq != oq))) x->harmony_changed++;
      if (!none)
      {
        x->chords[bi].root = br;
        x->chords[bi].q = bq;
        x->haschord[bi] = 1;
        prevr = br;
        prevq = bq;
        haveprev = 1;
      }
    }
  }
  free(pcw);
  free(haspcw);
}

static int cmpln(const void *a, const void *b)
{
  const LN *x = (const LN *)a, *y = (const LN *)b;
  return x->r - y->r;
}

static void stable_sort_ln(LN *v, int n)
{
  int a, b;
  for (a = 1; a < n; a++)
  {
    LN t = v[a];
    b = a - 1;
    while ((b >= 0) && (v[b].r > t.r)) { v[b + 1] = v[b]; b--; }
    v[b + 1] = t;
  }
}

static void extract(IMP *x)
{
  int bar = x->bar_rows, b, k, nmel = 0, *mel = (int *)malloc(sizeof(int) * (x->nparts + 1)), caplead;
  for (k = 0; k < x->nparts; k++)
    if ((x->parts[k].role == R_MEL) || (x->parts[k].role == R_MEL2)) mel[nmel++] = k;
  // sorted by -mel (stable)
  {
    int a, bb;
    for (a = 1; a < nmel; a++)
    {
      int t = mel[a];
      bb = a - 1;
      while ((bb >= 0) && (x->parts[mel[bb]].mel < x->parts[t].mel)) { mel[bb + 1] = mel[bb]; bb--; }
      mel[bb + 1] = t;
    }
  }
  x->mel_source = (int *)malloc(sizeof(int) * (x->nbars + 1));
  caplead = 1024;
  x->lead = (LN *)malloc(sizeof(LN) * caplead);
  x->nlead = 0;
  for (b = 0; b < x->nbars; b++)
  {
    int r0 = b * bar, r1 = (b + 1) * bar, src = -1, j, n = 0, nl;
    PN *sel;
    LN *line;
    x->mel_source[b] = -1;
    for (j = 0; j < nmel; j++)
      if (part_has_in(&x->parts[mel[j]], r0, r1)) { src = mel[j]; break; }
    if (src < 0)
    {
      double bm = 0;
      for (j = 0; j < x->nparts; j++)
        if ((x->parts[j].role != R_BASS) && (part_has_in(&x->parts[j], r0, r1)))
          if ((src < 0) || (x->parts[j].mean > bm)) { src = j; bm = x->parts[j].mean; }
    }
    if (src < 0) continue;
    x->mel_source[b] = src;
    sel = (PN *)malloc(sizeof(PN) * (x->parts[src].n + 1));
    for (j = 0; j < x->parts[src].n; j++)
      if ((x->parts[src].notes[j].r >= r0) && (x->parts[src].notes[j].r < r1)) sel[n++] = x->parts[src].notes[j];
    line = (LN *)malloc(sizeof(LN) * (n + 1));
    nl = skyline(sel, n, 1, line, src);
    if (x->nlead + nl >= caplead) x->lead = (LN *)realloc(x->lead, sizeof(LN) * (caplead = (x->nlead + nl) * 2 + 64));
    memcpy(x->lead + x->nlead, line, sizeof(LN) * nl);
    x->nlead += nl;
    free(sel);
    free(line);
  }
  stable_sort_ln(x->lead, x->nlead);
  for (k = 0; k + 1 < x->nlead; k++)
    if (x->lead[k].r + x->lead[k].d > x->lead[k + 1].r) x->lead[k].d = x->lead[k + 1].r - x->lead[k].r;
  // bass
  {
    int nb = 0, tot = 0, j;
    PN *bn;
    for (k = 0; k < x->nparts; k++)
      if (x->parts[k].role == R_BASS) tot += x->parts[k].n;
    if (tot)
    {
      int first = -1;
      bn = (PN *)malloc(sizeof(PN) * (tot + 1));
      for (k = 0; k < x->nparts; k++)
        if (x->parts[k].role == R_BASS)
        {
          if (first < 0) first = k;
          for (j = 0; j < x->parts[k].n; j++) bn[nb++] = x->parts[k].notes[j];
        }
      x->bass = (LN *)malloc(sizeof(LN) * (nb + 1));
      x->nbass = skyline(bn, nb, 0, x->bass, first);
      free(bn);
    }
    else
    {
      // low notes of the accompaniment (left hand), well below the melody
      int *lead_at = (int *)malloc(sizeof(int) * (x->total_rows + 1)), np = 0, nl, r;
      PN *pool;
      LN *line;
      for (r = 0; r <= x->total_rows; r++) lead_at[r] = -1000;
      for (k = 0; k < x->nlead; k++)
        for (r = x->lead[k].r; (r < x->lead[k].r + x->lead[k].d) && (r <= x->total_rows); r++) lead_at[r] = x->lead[k].p;
      for (k = 0; k < x->nparts; k++) tot += x->parts[k].n;
      pool = (PN *)malloc(sizeof(PN) * (tot + 1));
      if (x->single)
      {
        for (k = 0; k < x->nparts; k++)
          for (j = 0; j < x->parts[k].n; j++) pool[np++] = x->parts[k].notes[j];
      }
      else
      {
        // harm_parts + mel_parts (in that order)
        for (k = 0; k < x->nparts; k++)
          if (x->parts[k].role == R_HARM)
            for (j = 0; j < x->parts[k].n; j++) pool[np++] = x->parts[k].notes[j];
        for (k = 0; k < nmel; k++)
          for (j = 0; j < x->parts[mel[k]].n; j++) pool[np++] = x->parts[mel[k]].notes[j];
      }
      line = (LN *)malloc(sizeof(LN) * (np + 1));
      nl = skyline(pool, np, 0, line, -1);
      x->bass = (LN *)malloc(sizeof(LN) * (nl + 1));
      x->nbass = 0;
      for (k = 0; k < nl; k++)
      {
        int lp = line[k].r <= x->total_rows ? lead_at[line[k].r] : -1000;
        if ((line[k].p <= 57) && ((lp == -1000) || (lp - line[k].p >= 7)))
        {
          x->bass[x->nbass] = line[k];
          x->bass[x->nbass].part = -1;
          x->nbass++;
        }
      }
      free(line);
      free(pool);
      free(lead_at);
    }
  }
  // chords per beat
  {
    int beat = ((x->timesig[1] == 2) || (x->timesig[1] == 4) || (x->timesig[1] == 8)) ? x->q * 4 / x->timesig[1] : x->q, ns = 0, tot = 0, j;
    if (beat < 1) beat = 1;
    x->beat_rows = beat;
    for (k = 0; k < x->nparts; k++) tot += x->parts[k].n;
    x->harm_notes = (PN *)malloc(sizeof(PN) * (tot + 1));
    if (x->single)
    {
      for (k = 0; k < x->nparts; k++)
        for (j = 0; j < x->parts[k].n; j++) x->harm_notes[ns++] = x->parts[k].notes[j];
    }
    else
    {
      for (k = 0; k < x->nparts; k++)
        if (x->parts[k].role == R_HARM)
          for (j = 0; j < x->parts[k].n; j++) x->harm_notes[ns++] = x->parts[k].notes[j];
      if (!ns)
        for (k = 0; k < nmel; k++)
          for (j = 0; j < x->parts[mel[k]].n; j++) x->harm_notes[ns++] = x->parts[mel[k]].notes[j];
    }
    x->nharm = ns;
    detect_chords(x, x->harm_notes, ns);
  }
  // counter melody: the second best melodic part per bar
  {
    int cap = 1024;
    x->counter = (LN *)malloc(sizeof(LN) * cap);
    x->ncounter = 0;
    for (b = 0; b < x->nbars; b++)
    {
      int r0 = b * bar, r1 = (b + 1) * bar, main = x->mel_source[b], best = -1, j, n = 0, nl;
      double bm = 0;
      PN *sel;
      LN *line;
      for (j = 0; j < x->nparts; j++)
      {
        if ((j == main) || (x->parts[j].role == R_BASS)) continue;
        if (!part_has_in(&x->parts[j], r0, r1)) continue;
        if ((best < 0) || (x->parts[j].mel > bm)) { best = j; bm = x->parts[j].mel; }
      }
      if (best < 0) continue;
      if ((x->single) || (x->parts[best].poly > 1.5)) continue;
      sel = (PN *)malloc(sizeof(PN) * (x->parts[best].n + 1));
      for (j = 0; j < x->parts[best].n; j++)
        if ((x->parts[best].notes[j].r >= r0) && (x->parts[best].notes[j].r < r1)) sel[n++] = x->parts[best].notes[j];
      line = (LN *)malloc(sizeof(LN) * (n + 1));
      nl = skyline(sel, n, 1, line, best);
      if (x->ncounter + nl >= cap) x->counter = (LN *)realloc(x->counter, sizeof(LN) * (cap = (x->ncounter + nl) * 2 + 64));
      memcpy(x->counter + x->ncounter, line, sizeof(LN) * nl);
      x->ncounter += nl;
      free(sel);
      free(line);
    }
  }
  // inner voices (two SIDs): all notes that are neither melody nor bass
  {
    int tot = 0, j, r;
    unsigned char *isl, *isb;
    for (k = 0; k < x->nparts; k++) tot += x->parts[k].n;
    isl = (unsigned char *)calloc((x->total_rows + 1) * 128, 1);
    isb = (unsigned char *)calloc((x->total_rows + 1) * 128, 1);
    for (k = 0; k < x->nlead; k++)
      if ((x->lead[k].r <= x->total_rows) && (x->lead[k].p >= 0) && (x->lead[k].p < 128)) isl[x->lead[k].r * 128 + x->lead[k].p] = 1;
    for (k = 0; k < x->nbass; k++)
      if ((x->bass[k].r <= x->total_rows) && (x->bass[k].p >= 0) && (x->bass[k].p < 128)) isb[x->bass[k].r * 128 + x->bass[k].p] = 1;
    x->inner = malloc(sizeof(*x->inner) * (tot + 1));
    x->ninner = 0;
    for (k = 0; k < x->nparts; k++)
      for (j = 0; j < x->parts[k].n; j++)
      {
        PN *n = &x->parts[k].notes[j];
        if ((isl[n->r * 128 + n->p]) || (isb[n->r * 128 + n->p])) continue;
        x->inner[x->ninner].r = n->r;
        x->inner[x->ninner].d = n->d;
        x->inner[x->ninner].p = n->p;
        x->inner[x->ninner].v = n->v;
        x->inner[x->ninner].part = k;
        x->ninner++;
      }
    // sort by (row, -pitch), stable
    {
      int a, b2;
      for (a = 1; a < x->ninner; a++)
      {
        typeof(*x->inner) t = x->inner[a];
        b2 = a - 1;
        while ((b2 >= 0) && ((x->inner[b2].r > t.r) || ((x->inner[b2].r == t.r) && (x->inner[b2].p < t.p)))) { x->inner[b2 + 1] = x->inner[b2]; b2--; }
        x->inner[b2 + 1] = t;
      }
    }
    free(isl);
    free(isb);
    (void)r;
  }
  // harmony onsets and the program of the accompaniment
  {
    int best = -1;
    x->harm_onset = (unsigned char *)calloc(x->total_rows + 1, 1);
    for (k = 0; k < x->nharm; k++)
      if (x->harm_notes[k].r <= x->total_rows) x->harm_onset[x->harm_notes[k].r] = 1;
    for (k = 0; k < x->nparts; k++)
      if (x->parts[k].role == R_HARM)
        if ((best < 0) || (x->parts[k].n > x->parts[best].n)) best = k;
    x->harm_prog = best >= 0 ? x->parts[best].program : (x->nparts ? x->parts[0].program : 0);
  }
  free(mel);
}

static void pick_instruments(IMP *x)
{
  int beats_with_kick = 0, nbeats = x->total_rows / (x->q > 1 ? x->q : 1), b, k, bass = -1;
  double bpm = 3000.0 / ((x->bar_tempo[0][0] + x->bar_tempo[0][1]) / 2.0 * x->q);
  for (b = 0; b < nbeats; b++)
    if (x->drums[b * x->q] & DL_K) beats_with_kick++;
  x->dance = (x->drum_notes > 0) && (beats_with_kick > 0.7 * nbeats) && (bpm >= 110);
  x->kit = x->dance ? &DANCE_KITS[0] : &BAND_KITS[0];
  snprintf(x->kickbass, 48, "%s", x->dance ? "BD-Bass Stampf" : "BD-Bass Puls");
  for (k = 0; k < x->nparts; k++)
    if (x->parts[k].role == R_BASS) { bass = k; break; }
  snprintf(x->i_bass, 48, "%s", gm_bass(bass >= 0 ? x->parts[bass].program : (x->nparts ? x->parts[0].program : 33), x->dance));
  if ((!x->drum_notes) && (bass < 0) && (x->nparts) && (x->parts[0].program < 8)) snprintf(x->i_bass, 48, "Tiefes Piano");
  snprintf(x->t_arp, 48, "%s", gm_arp(x->harm_prog));
  snprintf(x->t_pad, 48, "%s", ((x->harm_prog >= 40) && (x->harm_prog < 56)) ? "Arp Streicher" : "Arp Flaeche");
  if (x->hasvar)
  {
    const char *alts[64];
    int na = tb_alternatives(x->i_bass, NULL, 0, alts, 64), nn = 0;
    static const char *pads[5] = {"Arp Flaeche", "Arp Streicher", "Arp Langsam", "Arp String Machine", "Arp Glas"};
    const char *arps[8];
    if (na > 6) na = 6;
    if ((na) && (rng_random(&x->var_rng) < 0.8)) snprintf(x->i_bass, 48, "%s", alts[rng_below(&x->var_rng, na)]);
    for (k = 0; k < 6; k++)
    {
      const char *a = ARPFAM(x->t_arp, k);
      if (!a) break;
      if (strcmp(a, x->t_arp)) arps[nn++] = a;
    }
    arps[nn++] = x->t_arp;
    {
      char tmp[48];
      snprintf(tmp, 48, "%s", arps[rng_below(&x->var_rng, nn)]);
      snprintf(x->t_arp, 48, "%s", tmp);
    }
    snprintf(x->t_pad, 48, "%s", pads[rng_below(&x->var_rng, 5)]);
    x->kit = x->dance ? &DANCE_KITS[rng_below(&x->var_rng, 3)] : &BAND_KITS[rng_below(&x->var_rng, 3)];
  }
  if (x->o.hints)
  {
    if (x->o.hint_dance >= 0) x->dance = x->o.hint_dance;
    if (x->o.hint_bass[0]) snprintf(x->i_bass, 48, "%s", x->o.hint_bass);
    if (x->o.hint_kickbass[0]) snprintf(x->kickbass, 48, "%s", x->o.hint_kickbass);
    if (x->o.hint_arp[0]) snprintf(x->t_arp, 48, "%s", x->o.hint_arp);
    if (x->o.hint_pad[0]) snprintf(x->t_pad, 48, "%s", x->o.hint_pad);
  }
  x->kitv = *x->kit;
  x->kit = &x->kitv;
  if (x->o.hints)
  {
    const char **f[8] = {&x->kitv.kick, &x->kitv.snare, &x->kitv.clap, &x->kitv.hat, &x->kitv.ohat, &x->kitv.crash, &x->kitv.ride, &x->kitv.perc};
    int j;
    for (j = 0; j < 8; j++)
      if (x->o.hint_kit[j][0]) *f[j] = x->o.hint_kit[j];
  }
  if (x->o.accomp == ACC_PADS)
  {
    if ((strcmp(x->t_pad, "Arp Langsam")) && (strcmp(x->t_pad, "Arp Flaeche")) && (strcmp(x->t_pad, "Arp Streicher"))) snprintf(x->t_arp, 48, "Arp Langsam");
    else snprintf(x->t_arp, 48, "%s", x->t_pad);
  }
}

static const char *lead_name(IMP *x, int program)
{
  const char *base;
  int k;
  for (k = 0; k < x->o.nhint_lead; k++)
    if (x->o.hint_lead_prog[k] == program) return x->o.hint_lead_name[k];
  base = gm_lead(program);
  if (!x->hasvar) return base;
  if (!x->lead_map_p[program])
  {
    const char *alts[64];
    int na = tb_alternatives(base, NULL, 0, alts, 64);
    SGRNG r;
    if (na > 5) na = 5;
    rng_seeds(&r, "%d-%d", x->o.variant, program);
    if ((na) && (rng_random(&r) < 0.85)) snprintf(x->lead_map[program], 48, "%s", alts[rng_below(&r, na)]);
    else snprintf(x->lead_map[program], 48, "%s", base);
    x->lead_map_p[program] = 1;
  }
  return x->lead_map[program];
}

static int harm_level(IMP *x, int s0, int s1, double *out)
{
  int k, n = 0, ng = 0, any = 0;
  static int *gr = NULL, cap = 0;
  static double *gv = NULL;
  if (cap < x->nharm + 1)
  {
    cap = x->nharm + 1;
    gr = (int *)realloc(gr, sizeof(int) * cap);
    gv = (double *)realloc(gv, sizeof(double) * cap);
  }
  for (k = 0; k < x->nharm; k++)
    if ((x->harm_notes[k].r >= s0) && (x->harm_notes[k].r < s1)) { any = 1; break; }
  for (k = 0; k < x->nharm; k++)
  {
    PN *h = &x->harm_notes[k];
    int ok = any ? ((h->r >= s0) && (h->r < s1)) : ((h->r < s1) && (h->r + h->d > s0)), j;
    if (!ok) continue;
    n++;
    for (j = ng - 1; j >= 0; j--)
      if (gr[j] == h->r) break;
    if (j < 0)
    {
      gr[ng] = h->r;
      gv[ng] = 0;
      j = ng++;
    }
    gv[j] += pow(10, h->v / 10.0);
  }
  if (!n) return 0;
  {
    PYSUM s;
    ps_init(&s);
    for (k = 0; k < ng; k++) ps_add(&s, gv[k]);
    *out = 10 * log10(ps_get(&s) / ng);
  }
  return 1;
}

static int drum_ins(IMP *x, int letter, int s)
{
  int li, gm = 0, k;
  const char *n;
  for (li = 0; li < 9; li++)
    if ((1 << li) == letter) gm = x->drum_gm[s * 9 + li];
  for (k = 0; k < x->o.nhint_gm; k++)
    if (x->o.hint_gm_note[k] == gm) return imp_use(x, x->o.hint_gm_ins[k]);
  if ((n = gm_drum_ins(gm))) return imp_use(x, n);
  if ((gm == 35) && (!x->dance)) return imp_use(x, "Kick Akustik");
  if ((gm == 40) && (!x->dance)) return imp_use(x, "Snare Rock");
  if (letter == DL_T)
  {
    static const char *toms[3] = {"Tom hoch 2", "Tom mittel 2", "Tom Stand"};
    int i = (s % x->bar_rows) * 3 / x->bar_rows;
    return imp_use(x, toms[i < 2 ? i : 2]);
  }
  switch (letter)
  {
    case DL_K: return imp_use(x, x->kit->kick);
    case DL_S: return imp_use(x, x->kit->snare);
    case DL_C: return imp_use(x, x->kit->clap);
    case DL_H: return imp_use(x, x->kit->hat);
    case DL_O: return imp_use(x, x->kit->ohat);
    case DL_X: return imp_use(x, x->kit->crash);
    case DL_R: return imp_use(x, x->kit->ride);
  }
  return imp_use(x, x->kit->perc);
}

static int addname(char names[][48], int *n, int max, const char *name)
{
  int k;
  for (k = 0; k < *n; k++)
    if (!strcmp(names[k], name)) return 0;
  if (*n < max) snprintf(names[(*n)++], 48, "%s", name);
  return 1;
}

static int hasname(char names[][48], int n, const char *name)
{
  int k;
  for (k = 0; k < n; k++)
    if (!strcmp(names[k], name)) return 1;
  return 0;
}

static int counter_ins(IMP *x, const char *base)
{
  char name[64];
  int k;
  COMP *c = x->c;
  snprintf(name, sizeof name, "%.12s 2", base);
  for (k = 0; k < c->numins; k++)
    if (!strcmp(c->ins[k].name, name)) break;
  if (k >= c->numins)
  {
    const SGINS *src = sg_findins(base);
    SGINS n;
    int s;
    if ((!src) || (c->numins >= SG_MAXINS)) return imp_use(x, "Sanfter Lead");
    n = *src;
    s = (src->sr >> 4) - 3;
    if (s < 4) s = 4;
    snprintf(n.name, sizeof n.name, "%s", name);
    {
      char en[64];
      sg_en(name, en, sizeof en);
      snprintf(n.en, sizeof n.en, "%.47s", en);
    }
    n.sr = (s << 4) | (src->sr & 0x0f);
    n.filt[0] = 0;
    snprintf(n.loudref, sizeof n.loudref, "%s", src->loudref[0] ? src->loudref : src->name);
    n.derived = 1;
    c->ins[c->numins] = n;
    c->arp_tpl[c->numins] = -1;
    k = c->numins++;
  }
  addname(x->counter_names, &x->ncounter_names, 64, name);
  return k;
}

static void render_counter(IMP *x, SGTRACK *t, int r0, int r1)
{
  int k;
  for (k = 0; k < x->ncounter; k++)
  {
    LN *n = &x->counter[k];
    if ((n->r >= r0) && (n->r < r1))
    {
      const char *base = n->part >= 0 ? lead_name(x, x->parts[n->part].program) : "Sanfter Lead";
      char b2[48];
      int name;
      double lv;
      snprintf(b2, 48, "%s", base);
      if ((x->sids >= 2) && (hasname(x->lead_names, x->nlead_names, b2)))
      {
        const char *used[64];
        int u;
        for (u = 0; u < x->nlead_names; u++) used[u] = x->lead_names[u];
        snprintf(b2, 48, "%s", tb_distinct(b2, used, x->nlead_names, 0));
      }
      name = counter_ins(x, b2);
      lv = 0;
      {
        int has = n->part >= 0 ? part_lv(&x->parts[n->part], n->r, n->p, &lv) : 0;
        tr_note_lv(t, n->r, n->p - 12, name, n->d < r1 - n->r ? n->d : r1 - n->r, has, lv);
      }
    }
  }
}

static const char *broken_name(IMP *x)
{
  static char out[48];
  int fam = x->harm_prog / 8;
  const char *base = fam == 0 ? "Piano" : fam == 1 ? "Vibraphon" : fam == 2 ? "Orgel Perkussiv" : fam == 3 ? "Gitarre clean" :
                     fam == 5 ? "Pizzicato" : fam == 6 ? "Harfe" : "Synth Piano";
  snprintf(out, 48, "%s", base);
  if (x->hasvar)
  {
    const char *alts[64];
    int na = tb_alternatives(base, NULL, 0, alts, 64);
    if (na > 4) na = 4;
    if (na)
    {
      SGRNG r;
      rng_seeds(&r, "%d-broken", x->o.variant);
      alts[na] = base;
      snprintf(out, 48, "%s", alts[rng_below(&r, na + 1)]);
    }
  }
  return out;
}

static void render_broken(IMP *x, SGTRACK *t, int r0, int r1)
{
  int beat = x->beat_rows, step = beat / 2 > 1 ? beat / 2 : 1, name = imp_use(x, broken_name(x)), k = 0, r;
  for (r = r0 - r0 % step; r < r1; r += step)
  {
    int bi, root, q, base, seq[4], n;
    double *w, lv;
    if (r < r0) continue;
    bi = r / beat;
    if ((bi >= x->nbeats) || (!x->haschord[bi])) { k = 0; continue; }
    root = x->chords[bi].root;
    q = x->chords[bi].q;
    base = 48 + root < 55 ? 48 + root : 36 + root;
    seq[0] = base;
    seq[1] = base + sg_qiv[q][2 % sg_qlen[q]];
    seq[2] = base + sg_qiv[q][1] + 12;
    seq[3] = base + sg_qiv[q][2 % sg_qlen[q]];
    n = seq[k % 4];
    k++;
    w = x->snd[r < x->total_rows - 1 ? r : x->total_rows - 1];
    if (((w[(n + 1) % 12] >= 0.9) || (w[(n + 11) % 12] >= 0.9)) && (w[n % 12] < 0.9)) continue;
    {
      int has = harm_level(x, r, r + step, &lv);
      tr_note_lv(t, r, n, name, step, has, lv);
    }
  }
}

static void render_harm(IMP *x, SGTRACK *t, int r0, int r1, int force_rhythm, const char *tpl, int sustained)
{
  int beat = x->beat_rows, rhythmic, n = 0, r, bi, s, careful = x->o.harmony != 2;
  struct { int s0, s1; CHORD c; } spans[4096];
  int nspans = 0, hc = 0;
  CHORD cur = {0};
  if ((x->o.accomp == ACC_BROKEN) && (!tpl))
  {
    render_broken(x, t, r0, r1);
    return;
  }
  if (x->o.accomp == ACC_STABS) force_rhythm = 1;
  else if (x->o.accomp == ACC_PADS) sustained = 1;
  for (r = r0; r < r1; r++)
    if ((r <= x->total_rows) && (x->harm_onset[r])) n++;
  rhythmic = n >= (r1 - r0) / (double)beat * 1.5;
  if (force_rhythm) rhythmic = 1;
  if (sustained) rhythmic = 0;
  {
    int start = 0;
    for (bi = r0 / beat; bi < (r1 + beat - 1) / beat; bi++)
    {
      int has = (bi < x->nbeats) && (x->haschord[bi]);
      if ((has != hc) || (has && ((x->chords[bi].root != cur.root) || (x->chords[bi].q != cur.q))))
      {
        if ((hc) && (nspans < 4096))
        {
          spans[nspans].s0 = start;
          spans[nspans].s1 = bi * beat;
          spans[nspans].c = cur;
          nspans++;
        }
        hc = has;
        if (has) cur = x->chords[bi];
        start = bi * beat;
      }
    }
    if ((hc) && (nspans < 4096))
    {
      int e = ((r1 + beat - 1) / beat) * beat;
      spans[nspans].s0 = start;
      spans[nspans].s1 = r1 < e ? r1 : e;
      spans[nspans].c = cur;
      nspans++;
    }
  }
  for (s = 0; s < nspans; s++)
  {
    int s0 = spans[s].s0 > r0 ? spans[s].s0 : r0, s1 = spans[s].s1 < r1 ? spans[s].s1 : r1, root = spans[s].c.root, q = spans[s].c.q;
    int ins, note = 48 + root, *hits, nh = 0, h;
    unsigned char *bad;
    if (s1 <= s0) continue;
    ins = imp_use_arp(x, tpl ? tpl : x->t_arp, q);
    bad = (unsigned char *)calloc(s1 - s0 + 1, 1);
    if (careful) row_clashes(x, s0, s1, root, q, bad);
#define PUT(hh, ee) do { int h_ = (hh), e_ = (ee); \
      while (h_ < e_) { int k_; double lv_; int has_; \
        while ((h_ < e_) && (h_ - s0 < s1 - s0) && (bad[h_ - s0])) h_++; \
        k_ = h_; \
        while ((k_ < e_) && !((k_ - s0 < s1 - s0) && (bad[k_ - s0]))) k_++; \
        if (k_ > h_) { has_ = harm_level(x, h_, h_ + 1 > k_ ? h_ + 1 : k_, &lv_); tr_note_lv(t, h_, note, ins, k_ - h_, has_, lv_); } \
        h_ = k_; } } while (0)
    if (rhythmic)
    {
      hits = (int *)malloc(sizeof(int) * (s1 - s0 + 2));
      for (h = s0; h < s1; h++)
        if ((x->harm_onset[h]) || ((force_rhythm) && ((h - s0) % beat == 0))) hits[nh++] = h;
      if (!nh) hits[nh++] = s0;
      if (hits[0] != s0)
      {
        memmove(hits + 1, hits, sizeof(int) * nh);
        hits[0] = s0;
        nh++;
      }
      for (h = 0; h < nh; h++)
      {
        int e = h + 1 < nh ? hits[h + 1] : s1, len = e - hits[h] < beat * 2 ? e - hits[h] : beat * 2;
        if (len < 1) len = 1;
        PUT(hits[h], hits[h] + len);
      }
      free(hits);
    }
    else PUT(s0, s1);
#undef PUT
    free(bad);
  }
}

static int slot_instrument(IMP *x, int part, int slot)
{
  int k;
  while (x->npoly_ins[part] <= slot)
  {
    const char *base = lead_name(x, x->parts[part].program), **used;
    int nu = 0, p2, j;
    used = (const char **)malloc(sizeof(char *) * (x->nlead_names + 32 * (x->nparts + 1) + 1));
    for (j = 0; j < x->nlead_names; j++) used[nu++] = x->lead_names[j];
    for (j = 0; j < x->npoly_ins[part]; j++) used[nu++] = x->poly_ins[part][j];
    for (p2 = 0; p2 < x->nparts; p2++)
      for (j = 0; j < x->npoly_ins[p2]; j++) used[nu++] = x->poly_ins[p2][j];
    if (x->npoly_ins[part] >= 32) { free(used); break; }
    snprintf(x->poly_ins[part][x->npoly_ins[part]], 48, "%s", tb_distinct(base, used, nu, 0));
    x->npoly_ins[part]++;
    free(used);
  }
  k = slot < 32 ? slot : 31;
  return imp_use(x, x->poly_ins[part][k]);
}

static int alloc_poly(IMP *x, SGTRACK **freev, int nfree, int r0, int r1, const int *bn)
{
  int k, nn = 0, *lead_at, *bass_pc, dropped = 0, i;
  int (*plan)[4096][5];
  int np[32], ends[32], last[32], havelast[32];
  if (nfree > 32) nfree = 32;
  for (k = 0; k < x->ninner; k++)
    if ((x->inner[k].r >= r0) && (x->inner[k].r < r1)) nn++;
  if ((!nn) || (!nfree)) return 0;
  lead_at = (int *)malloc(sizeof(int) * (r1 - r0 + 1));
  bass_pc = (int *)malloc(sizeof(int) * (r1 - r0 + 1));
  for (k = 0; k < r1 - r0; k++) lead_at[k] = bass_pc[k] = -1;
  for (k = 0; k < x->nlead; k++)
  {
    LN *l = &x->lead[k];
    int j;
    if ((l->r < r1) && (l->r + l->d > r0))
      for (j = (l->r > r0 ? l->r : r0); j < (l->r + l->d < r1 ? l->r + l->d : r1); j++) lead_at[j - r0] = l->p % 12;
  }
  for (k = r0; k < r1; k++)
    ;
  // bass_at: rows in order of the dict (sorted rows)
  {
    int r;
    for (r = 0; r < x->total_rows; r++)
    {
      int d, j;
      if (bn[r * 3] == -1000) continue;
      d = bn[r * 3 + 1];
      if ((r < r1) && (r + d > r0))
        for (j = (r > r0 ? r : r0); j < (r + d < r1 ? r + d : r1); j++) bass_pc[j - r0] = ((bn[r * 3] % 12) + 12) % 12;
    }
  }
  plan = malloc(sizeof(*plan) * nfree);
  for (i = 0; i < nfree; i++) { np[i] = 0; ends[i] = r0; havelast[i] = 0; last[i] = 0; }
  for (k = 0; k < x->ninner; k++)
  {
    int r = x->inner[k].r, d = x->inner[k].d, p = x->inner[k].p, cand[32], nc = 0, best;
    if ((r < r0) || (r >= r1)) continue;
    for (i = 0; i < nfree; i++)
      if (ends[i] <= r) cand[nc++] = i;
    if (!nc)
    {
      int oi = -1;
      for (i = 0; i < nfree; i++)
        if ((np[i]) && (r - plan[i][np[i] - 1][0] >= 2))
          if ((oi < 0) || (plan[i][np[i] - 1][0] < plan[oi][np[oi] - 1][0])) oi = i;
      if (oi >= 0)
      {
        plan[oi][np[oi] - 1][1] = r - plan[oi][np[oi] - 1][0];
        ends[oi] = r;
        cand[nc++] = oi;
      }
    }
    if (!nc)
    {
      int snd = 0;
      for (i = 0; i < nfree; i++)
        if ((np[i]) && (plan[i][np[i] - 1][0] + plan[i][np[i] - 1][1] > r)) snd |= 1 << (plan[i][np[i] - 1][2] % 12);
      if (((snd >> (p % 12)) & 1) || (lead_at[r - r0] == p % 12) || (bass_pc[r - r0] == p % 12))
      {
        dropped++;
        continue;
      }
      free(plan);
      free(lead_at);
      free(bass_pc);
      return 0;
    }
    best = cand[0];
    for (i = 1; i < nc; i++)
    {
      int a = havelast[cand[i]] ? abs(p - last[cand[i]]) : 0, b = havelast[best] ? abs(p - last[best]) : 0;
      if ((a < b) || ((a == b) && (cand[i] < best))) best = cand[i];
    }
    if (np[best] < 4096)
    {
      plan[best][np[best]][0] = r;
      plan[best][np[best]][1] = d < r1 - r ? d : r1 - r;
      plan[best][np[best]][2] = p;
      plan[best][np[best]][3] = (int)(x->inner[k].v * 1000);
      plan[best][np[best]][4] = k;
      np[best]++;
    }
    ends[best] = r + (d < r1 - r ? d : r1 - r);
    last[best] = p;
    havelast[best] = 1;
  }
  if (dropped > 0.15 * nn)
  {
    free(plan);
    free(lead_at);
    free(bass_pc);
    return 0;
  }
  for (i = 0; i < nfree; i++)
    for (k = 0; k < np[i]; k++)
    {
      int r = plan[i][k][0], d = plan[i][k][1], p = plan[i][k][2], ik = plan[i][k][4];
      int nxt = k + 1 < np[i] ? plan[i][k + 1][0] : r1, name = slot_instrument(x, x->inner[ik].part, i), len = d < nxt - r ? d : nxt - r;
      addname(x->inner_names, &x->ninner_names, 256, x->c->ins[name].name);
      tr_note_lv(freev[i], r, p - 12, name, len > 1 ? len : 1, 1, x->inner[ik].v - 1.0);
    }
  free(plan);
  free(lead_at);
  free(bass_pc);
  return 1;
}

// bass notes per row: [note, dur, haslv] and the level
static int *bassat(IMP *x, double **lvout)
{
  int *b = (int *)malloc(sizeof(int) * 3 * (x->total_rows + 1)), k;
  double *lv = (double *)malloc(sizeof(double) * (x->total_rows + 1));
  for (k = 0; k <= x->total_rows; k++) { b[k * 3] = -1000; b[k * 3 + 1] = 0; b[k * 3 + 2] = 0; lv[k] = 0; }
  for (k = 0; k < x->nbass; k++)
  {
    LN *l = &x->bass[k];
    double a;
    if (l->r > x->total_rows) continue;
    b[l->r * 3] = l->p - 12;
    b[l->r * 3 + 1] = l->d;
    a = ((l->p >= 0) && (l->p < 128)) ? x->lvany[l->r * 128 + l->p] : NAN;
    b[l->r * 3 + 2] = !isnan(a);
    lv[l->r] = isnan(a) ? 0 : a;
  }
  *lvout = lv;
  return b;
}

static void lead_notes(IMP *x, SGTRACK *t)
{
  int k;
  for (k = 0; k < x->nlead; k++)
  {
    LN *l = &x->lead[k];
    const char *name = l->part >= 0 ? lead_name(x, x->parts[l->part].program) : "Lead Puls";
    double lv = 0;
    int has = l->part >= 0 ? part_lv(&x->parts[l->part], l->r, l->p, &lv) : 0;
    addname(x->lead_names, &x->nlead_names, 64, name);
    tr_note_lv(t, l->r, l->p - 12, imp_use(x, name), l->d, has, lv);
  }
}

static void render_block(IMP *x, SGTRACK *V, int r0, int r1, int role, const int *bn, const double *blv, int has_bass)
{
  int drums_on_3 = role == 2, bi = imp_use(x, x->i_bass), cur_note = -1000, sound_end = -1, s, havecur_lv = 0;
  int res_n = -1000, res_end = 0, res_haslv = 0;
  double cur_lv = 0, res_lv = 0;
  SGTRACK *v0 = &V[0];
  for (s = r0; s < r1; s++)
  {
    int letters = x->drums[s], on = bn[s * 3] != -1000;
    if (on) { cur_note = bn[s * 3]; havecur_lv = bn[s * 3 + 2]; cur_lv = blv[s]; }
    if ((letters & (DL_S | DL_C)) && (!drums_on_3))
    {
      if ((on) && (bn[s * 3 + 1] > 2)) { res_n = bn[s * 3]; res_end = s + bn[s * 3 + 1]; res_haslv = bn[s * 3 + 2]; res_lv = blv[s]; }
      else if ((res_n == -1000) && (cur_note != -1000) && (sound_end > s + 2)) { res_n = cur_note; res_end = sound_end; res_haslv = havecur_lv; res_lv = cur_lv; }
      tr_note_lv(v0, s, 48, drum_ins(x, (letters & DL_S) ? DL_S : DL_C, s), -1, 0, 0);
      sound_end = s;
      continue;
    }
    if (letters & DL_K)
    {
      if (on)
      {
        tr_note_lv(v0, s, bn[s * 3], imp_use(x, x->kickbass), bn[s * 3 + 1], 0, 0);
        sound_end = s + bn[s * 3 + 1];
        res_n = -1000;
      }
      else if ((cur_note != -1000) && (sound_end > s))
      {
        tr_note_lv(v0, s, cur_note, imp_use(x, x->kickbass), sound_end - s, 0, 0);
        res_n = -1000;
      }
      else if ((res_n != -1000) && (res_end > s + 1))
      {
        tr_note_lv(v0, s, res_n, imp_use(x, x->kickbass), res_end - s, 0, 0);
        sound_end = res_end;
        res_n = -1000;
      }
      else
      {
        tr_note_lv(v0, s, 48, drum_ins(x, DL_K, s), -1, 0, 0);
        sound_end = s;
      }
      continue;
    }
    if (on)
    {
      tr_note_lv(v0, s, bn[s * 3], bi, bn[s * 3 + 1], bn[s * 3 + 2], blv[s]);
      sound_end = s + bn[s * 3 + 1];
      res_n = -1000;
      continue;
    }
    if ((res_n != -1000) && (s < res_end) && (!letters))
    {
      tr_note_lv(v0, s, res_n, bi, res_end - s, res_haslv, res_lv);
      sound_end = res_end;
      res_n = -1000;
      continue;
    }
    if ((!drums_on_3) && (sound_end <= s) && (letters & ~(DL_K | DL_S | DL_C)))
    {
      int k;
      for (k = 0; k < 9; k++)
        if ((letters & DRUM_PRIO[k]) && (!(DRUM_PRIO[k] & (DL_K | DL_S | DL_C))))
        {
          tr_note_lv(v0, s, 48, drum_ins(x, DRUM_PRIO[k], s), -1, 0, 0);
          break;
        }
    }
  }
  // voice 3
  if (role == 2)
  {
    int q = x->q, k;
    SGTRACK stabs;
    for (s = r0; s < r1; s++)
    {
      int letters = x->drums[s];
      if ((q % 2 == 0) && (s % q != q / 2)) letters &= ~DL_H;
      for (k = 0; k < 9; k++)
        if ((letters & DRUM_PRIO[k]) && (DRUM_PRIO[k] != DL_K))
        {
          tr_note_lv(&V[2], s, 48, drum_ins(x, DRUM_PRIO[k], s), -1, 0, 0);
          break;
        }
    }
    tr_init(&stabs, x->total_rows + 64);
    render_harm(x, &stabs, r0, r1, 1, NULL, 0);
    tr_merge_low_imp(&V[2], &stabs, 1, 1, 2, r0, r1);
    tr_free(&stabs);
  }
  else if (role == 1) render_harm(x, &V[2], r0, r1, 0, NULL, 0);
  else if (role == 3) render_counter(x, &V[2], r0, r1);
  // voice 2: fill the rests of the melody
  {
    SGTRACK fill;
    tr_init(&fill, x->total_rows + 64);
    if (role != 1) render_harm(x, &fill, r0, r1, 0, NULL, 0);
    else if (role != 3) render_counter(x, &fill, r0, r1);
    tr_merge_low_imp(&V[1], &fill, x->beat_rows / 2 > 1 ? x->beat_rows / 2 : 1, x->beat_rows, 2, r0, r1);
    tr_free(&fill);
  }
  // voice 1 empty (no bass, no drums): counter melody / chords there
  {
    int anyd = 0;
    for (s = r0; s < r1; s++)
      if (x->drums[s]) anyd = 1;
    if ((!has_bass) && (!anyd))
    {
      SGTRACK tmp;
      tr_init(&tmp, x->total_rows + 64);
      if (role == 1) render_counter(x, &tmp, r0, r1);
      else render_harm(x, &tmp, r0, r1, 0, NULL, 0);
      tr_merge_low_imp(&V[0], &tmp, 1, 1, 2, r0, r1);
      tr_free(&tmp);
    }
  }
}

static double role_w[4];

static void arrange3(IMP *x)
{
  int bar = x->bar_rows, T = x->total_rows, b0, prev_role = -1, k, block = 2 * bar;
  SGTRACK *V = x->c->tracks;
  double *blv;
  int *bn = bassat(x, &blv);
  unsigned char *bass_sound = (unsigned char *)calloc(T + 1, 1);
  for (k = 0; k < 3; k++) tr_init(&V[k], T + 64);
  x->c->ntracks = 3;
  if (x->hasvar)
  {
    // role weights in the order harm, counter, drums
    role_w[1] = rng_uniform(&x->var_rng, 0.6, 1.6);
    role_w[3] = rng_uniform(&x->var_rng, 0.6, 1.6);
    role_w[2] = rng_uniform(&x->var_rng, 0.6, 1.6);
  }
  for (k = 0; k < x->nbass; k++)
  {
    int r;
    for (r = x->bass[k].r; (r < x->bass[k].r + x->bass[k].d) && (r <= T); r++) bass_sound[r] = 1;
  }
  lead_notes(x, &V[1]);
  for (b0 = 0; b0 < T; b0 += block)
  {
    int r0 = b0, r1 = b0 + block < T ? b0 + block : T, s, lost = 0, snares = 0, ndrum = 0, has_bass = 0, harm = 0, cnt = 0, bi, role;
    double sc[4];          // 0 none, 1 harm, 2 drums, 3 counter
    for (s = r0; s < r1; s++)
    {
      if (x->drums[s])
      {
        ndrum++;
        if ((bass_sound[s]) && (x->drums[s] & ~(DL_K | DL_S | DL_C))) lost++;
        if (x->drums[s] & (DL_S | DL_C)) snares++;
      }
      if (bn[s * 3] != -1000) has_bass = 1;
    }
    for (bi = r0 / x->beat_rows; bi <= (r1 - 1) / x->beat_rows; bi++)
      if ((bi < x->nbeats) && (x->haschord[bi])) harm++;
    for (k = 0; k < x->ncounter; k++)
      if ((x->counter[k].r >= b0) && (x->counter[k].r < b0 + block)) cnt++;
    sc[1] = harm * 1.0;
    sc[3] = cnt >= 3 ? cnt * 0.9 : 0;
    sc[2] = ndrum ? (lost * 0.3 + snares * 0.3 + (!has_bass ? ndrum * 0.25 : 0) + (x->dance ? harm * 0.8 : 0)) : 0;
    sc[0] = 0.1;
    if ((x->o.accomp == ACC_COUNTER) && (cnt >= 2)) sc[3] = sc[3] + harm * 2.0 + 5;
    else if (((x->o.accomp == ACC_ARP) || (x->o.accomp == ACC_STABS) || (x->o.accomp == ACC_PADS) || (x->o.accomp == ACC_BROKEN)) && (harm))
      sc[1] = sc[1] + 4;
    if (x->hasvar)
    {
      sc[1] *= role_w[1];
      sc[3] *= role_w[3];
      sc[2] *= role_w[2];
    }
    if (x->voices == 2)
    {
      sc[0] = 1;
      sc[1] = sc[2] = sc[3] = -1e9;
    }
    if (prev_role >= 0) sc[prev_role] *= 1.25;
    // max over the dict order: harm, counter, drums, none
    {
      static const int order[4] = {1, 3, 2, 0};
      int i;
      role = order[0];
      for (i = 1; i < 4; i++)
        if (sc[order[i]] > sc[role]) role = order[i];
      if (x->voices == 2) role = 0;
    }
    prev_role = role;
    render_block(x, V, r0, r1, role, bn, blv, has_bass);
  }
  for (k = 0; k < 3; k++) tr_finalize(&V[k], T);
  free(bn);
  free(blv);
  free(bass_sound);
}

static void arrange6(IMP *x, int nextra)
{
  int bar = x->bar_rows, T = x->total_rows, b0, k, block = 2 * bar;
  SGTRACK *V = x->c->tracks;
  double *blv;
  int *bn = bassat(x, &blv);
  const char *pad = x->t_pad;
  for (k = 0; k < 6; k++) tr_init(&V[k], T + 64);
  lead_notes(x, &V[1]);
  {
    int bi = imp_use(x, x->i_bass), r;
    for (r = 0; r < T; r++)
      if (bn[r * 3] != -1000) tr_note_lv(&V[0], r, bn[r * 3], bi, bn[r * 3 + 1], bn[r * 3 + 2], blv[r]);
  }
  for (b0 = 0; b0 < T; b0 += block)
  {
    int r0 = b0, r1 = b0 + block < T ? b0 + block : T, s, has_drums = 0, cnt = 0, nf = 0, anybass = 0;
    SGTRACK *freev[40];
    for (s = r0; s < r1; s++)
      if (x->drums[s]) has_drums = 1;
    for (s = r0; s < r1; s++)
    {
      int letters = x->drums[s], main = 0, rest = 0, i;
      if (!letters) continue;
      for (i = 0; i < 9; i++)
        if (letters & DRUM_PRIO[i])
        {
          int isMain = DRUM_PRIO[i] & (DL_X | DL_S | DL_C | DL_K | DL_T);
          if ((isMain) && (!main)) main = DRUM_PRIO[i];
          if ((!isMain) && (!rest)) rest = DRUM_PRIO[i];
        }
      if (main)
      {
        tr_note_lv(&V[3], s, 48, drum_ins(x, main, s), -1, 0, 0);
        if ((letters & DL_K) && (main != DL_K))
          if ((!tr_sounding(&V[0], s, 2)) && (!((s < V[0].rows) && (V[0].ev[s].type != EV_NONE))))
            tr_note_lv(&V[0], s, 48, drum_ins(x, DL_K, s), -1, 0, 0);
      }
      if (rest) tr_note_lv(&V[5], s, 48, drum_ins(x, rest, s), -1, 0, 0);
    }
    if (x->voices == 3) freev[nf++] = &V[2];
    freev[nf++] = &V[4];
    if (!has_drums) { freev[nf++] = &V[5]; freev[nf++] = &V[3]; }
    for (k = 0; k < x->nextra_poly; k++) freev[nf++] = &V[6 + x->extra_poly[k]];
    if (alloc_poly(x, freev, nf, r0, r1, bn)) continue;
    render_harm(x, x->voices == 3 ? &V[2] : &V[4], r0, r1, 0, NULL, 0);
    for (k = 0; k < x->ncounter; k++)
      if ((x->counter[k].r >= r0) && (x->counter[k].r < r1)) cnt++;
    if (x->voices == 3)
    {
      if (cnt >= 2) render_counter(x, &V[4], r0, r1);
      else render_harm(x, &V[4], r0, r1, 0, pad, 1);
      if (!has_drums)
      {
        if (cnt >= 2) render_harm(x, &V[5], r0, r1, 0, pad, 1);
        if (cnt < 2) render_counter(x, &V[5], r0, r1);
      }
    }
    else
    {
      if (!has_drums)
      {
        if (cnt >= 2) render_counter(x, &V[5], r0, r1);
        else render_harm(x, &V[5], r0, r1, 0, pad, 1);
      }
      else if (cnt >= 2)
      {
        SGTRACK fill;
        tr_init(&fill, T + 64);
        render_counter(x, &fill, r0, r1);
        tr_merge_low_imp(&V[1], &fill, x->beat_rows / 2 > 1 ? x->beat_rows / 2 : 1, x->beat_rows, 2, r0, r1);
        tr_free(&fill);
      }
    }
    for (s = r0; s < r1; s++)
      if (bn[s * 3] != -1000) anybass = 1;
    if (!anybass)
    {
      SGTRACK tmp;
      tr_init(&tmp, T + 64);
      render_counter(x, &tmp, r0, r1);
      tr_merge_low_imp(&V[0], &tmp, 1, 1, 2, r0, r1);
      tr_free(&tmp);
    }
  }
  for (k = 0; k < 6; k++) tr_finalize(&V[k], T);
  (void)nextra;
  free(bn);
  free(blv);
}

// rows of a track in the order the events were made (Python dict order)
static const SGTRACK *ordtrack;
static int cmpord(const void *a, const void *b)
{
  unsigned x = ordtrack->ev[*(const int *)a].ord, y = ordtrack->ev[*(const int *)b].ord;
  return x < y ? -1 : x > y;
}

static int ordrows(const SGTRACK *t, int *out)
{
  int r, n = 0;
  for (r = 0; r < t->rows; r++)
    if (t->ev[r].type != EV_NONE) out[n++] = r;
  ordtrack = t;
  qsort(out, n, sizeof(int), cmpord);
  return n;
}

static void arrangeN(IMP *x)
{
  static const int R4[6] = {1, 0, 0, 3, 4, 0}, R6[6] = {2, 0, 0, 0, 0, 0}, R8[6] = {0, 0, 0, 0, 0, 0};
  // roles: 0 poly, 1 drums2, 2 hats2, 3 bass2, 4 lead2
  int roles[18], nr = 0, k, has_kit = 0, s;
  SGTRACK *V = x->c->tracks;
  for (s = 0; s < x->total_rows; s++)
    if (x->drums[s]) has_kit = 1;
  if (x->sids >= 4) for (k = 0; k < 6; k++) roles[nr++] = R4[k];
  if (x->sids >= 6) for (k = 0; k < 6; k++) roles[nr++] = R6[k];
  if (x->sids >= 8) for (k = 0; k < 6; k++) roles[nr++] = R8[k];
  for (k = 0; k < nr; k++)
    if ((!has_kit) && ((roles[k] == 1) || (roles[k] == 2))) roles[k] = 0;
  for (k = 0; k < nr; k++) tr_init(&V[6 + k], x->total_rows + 64);
  x->nextra_poly = 0;
  for (k = 0; k < nr; k++)
    if (roles[k] == 0) x->extra_poly[x->nextra_poly++] = k;
  arrange6(x, nr);
  for (k = 0; k < nr; k++)
  {
    SGTRACK *t = &V[6 + k];
    int r, i, nrr, *rr = (int *)malloc(sizeof(int) * (x->total_rows + 256 > V[0].rows + V[1].rows + V[3].rows + V[5].rows ? x->total_rows + 256 : V[0].rows + V[1].rows + V[3].rows + V[5].rows));
    if (roles[k] == 1)
    {
      for (i = 0, nrr = ordrows(&V[3], rr); i < nrr; i++)
        if (r = rr[i], V[3].ev[r].type == EV_NOTE) tr_note_lv(t, r, V[3].ev[r].note, V[3].ev[r].ins, V[3].ev[r].dur, 0, 0);
    }
    else if (roles[k] == 2)
    {
      for (i = 0, nrr = ordrows(&V[5], rr); i < nrr; i++)
        if (r = rr[i], (V[5].ev[r].type == EV_NOTE) && (tb_isdrum(x->c->ins[V[5].ev[r].ins].cat) || !strcmp(x->c->ins[V[5].ev[r].ins].cat, "kickbass")))
          tr_note_lv(t, r, V[5].ev[r].note, V[5].ev[r].ins, V[5].ev[r].dur, 0, 0);
    }
    else if (roles[k] == 3)
    {
      const char *u[1] = {x->i_bass};
      int b2 = imp_use(x, tb_distinct(x->i_bass, u, 1, 0));
      for (i = 0, nrr = ordrows(&V[0], rr); i < nrr; i++)
        if (r = rr[i], (V[0].ev[r].type == EV_NOTE) && (!tb_isdrum(x->c->ins[V[0].ev[r].ins].cat)) && (strcmp(x->c->ins[V[0].ev[r].ins].cat, "kickbass")))
          tr_note_lv(t, r, V[0].ev[r].note + 12, b2, V[0].ev[r].dur, V[0].haslv[r], V[0].lv[r]);
    }
    else if (roles[k] == 4)
    {
      char from[64][48], to[64][48];
      int nm = 0;
      nrr = ordrows(&V[1], rr);
      for (i = 0; i < nrr; i++)
      {
        SGEV *e = &V[1].ev[r = rr[i]];
        int j, n;
        if ((e->type != EV_NOTE) || (tb_isdrum(x->c->ins[e->ins].cat)) || (!strcmp(x->c->ins[e->ins].cat, "kickbass"))) continue;
        for (j = 0; j < nm; j++)
          if (!strcmp(from[j], x->c->ins[e->ins].name)) break;
        if ((j >= nm) && (nm < 64))
        {
          const char *used[80];
          int nu = 0, u;
          for (u = 0; u < x->nlead_names; u++) used[nu++] = x->lead_names[u];
          used[nu++] = x->c->ins[e->ins].name;
          snprintf(from[nm], 48, "%s", x->c->ins[e->ins].name);
          snprintf(to[nm], 48, "%s", tb_distinct(x->c->ins[e->ins].name, used, nu, 1));
          j = nm++;
        }
        n = e->note + 12 <= 86 ? e->note + 12 : e->note;
        tr_note_lv(t, r, n, imp_use(x, to[j < nm ? j : 0]), e->dur, V[1].haslv[r], V[1].lv[r]);
      }
    }
    free(rr);
  }
  for (k = 0; k < nr; k++) tr_finalize(&V[6 + k], x->total_rows);
  x->c->ntracks = 6 + nr;
}

static void limit_instruments(IMP *x, int maximum)
{
  COMP *c = x->c;
  for (;;)
  {
    int count[SG_MAXINS], n = 0, t, r, rare = -1, repl = -1, k;
    // count in the order the names appear (tracks, rows)
    int order[SG_MAXINS], no = 0;
    memset(count, 0, sizeof count);
    for (t = 0; t < c->ntracks; t++)
    {
      int *rr = (int *)malloc(sizeof(int) * (c->tracks[t].rows + 1)), nrr = ordrows(&c->tracks[t], rr), j;
      for (j = 0; j < nrr; j++)
        if (r = rr[j], c->tracks[t].ev[r].type == EV_NOTE)
        {
          int i = c->tracks[t].ev[r].ins;
          if (!count[i]) order[no++] = i;
          count[i]++;
        }
      free(rr);
    }
    n = no;
    if (n <= maximum) return;
    for (k = 0; k < no; k++)
      if ((rare < 0) || (count[order[k]] < count[rare])) rare = order[k];
    for (k = 0; k < no; k++)
      if ((order[k] != rare) && (!strcmp(c->ins[order[k]].cat, c->ins[rare].cat)))
        if ((repl < 0) || (count[order[k]] > count[repl])) repl = order[k];
    if (repl < 0)
      for (k = 0; k < no; k++)
        if (order[k] != rare)
          if ((repl < 0) || (count[order[k]] > count[repl])) repl = order[k];
    for (t = 0; t < c->ntracks; t++)
      for (r = 0; r < c->tracks[t].rows; r++)
        if ((c->tracks[t].ev[r].type == EV_NOTE) && (c->tracks[t].ev[r].ins == rare)) c->tracks[t].ev[r].ins = repl;
  }
}

static int cmpd(const void *a, const void *b)
{
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}

static void apply_levels(IMP *x)
{
  COMP *c = x->c;
  double *frames_row = (double *)malloc(sizeof(double) * (x->nbars * x->bar_rows + 1)), *ref, k0;
  int b, t, r, n = 0, nref = 0, nm = 0;
  typedef struct { int t, r, frames; double lv; int mel; } EVL;
  EVL *evs;
  int nev = 0;
  for (b = 0; b < x->nbars; b++)
    for (r = 0; r < x->bar_rows; r++) frames_row[b * x->bar_rows + r] = (x->bar_tempo[b][0] + x->bar_tempo[b][1]) / 2.0;
  evs = (EVL *)malloc(sizeof(EVL) * 65536);
  for (t = 0; t < c->ntracks; t++)
    for (r = 0; r < c->tracks[t].rows; r++)
    {
      SGEV *e = &c->tracks[t].ev[r];
      SGINS *ins;
      double fpr, bias;
      int frames, isc;
      if ((e->type != EV_NOTE) || (!c->tracks[t].haslv[r])) continue;
      ins = &c->ins[e->ins];
      if ((tb_isdrum(ins->cat)) || (!strcmp(ins->cat, "kickbass"))) continue;
      fpr = x->nbars ? frames_row[r < x->nbars * x->bar_rows - 1 ? r : x->nbars * x->bar_rows - 1] : 6;
      frames = (int)pyround((e->dur > 0 ? e->dur : 2) * fpr);
      if (frames < 2) frames = 2;
      isc = hasname(x->counter_names, x->ncounter_names, ins->name);
      bias = !strcmp(ins->cat, "arp") ? -3.0 : isc ? -2.0 : 0.0;
      if (nev < 65536)
      {
        evs[nev].t = t;
        evs[nev].r = r;
        evs[nev].frames = frames;
        evs[nev].lv = c->tracks[t].lv[r] + bias;
        evs[nev].mel = (t == 1) && (strcmp(ins->cat, "arp")) && (!isc);
        nev++;
      }
    }
  if (!nev)
  {
    free(evs);
    free(frames_row);
    return;
  }
  ref = (double *)malloc(sizeof(double) * nev);
  for (b = 0; b < nev; b++)
    if (evs[b].mel)
    {
      SGINS *ins = &c->ins[c->tracks[evs[b].t].ev[evs[b].r].ins];
      ref[nref++] = predict_db(ins, ins->ad, ins->sr, evs[b].frames) - evs[b].lv;
      nm++;
    }
  if (!nm)
    for (b = 0; b < nev; b++)
    {
      SGINS *ins = &c->ins[c->tracks[evs[b].t].ev[evs[b].r].ins];
      ref[nref++] = predict_db(ins, ins->ad, ins->sr, evs[b].frames) - evs[b].lv;
    }
  qsort(ref, nref, sizeof(double), cmpd);
  k0 = nref % 2 ? ref[nref / 2] : (ref[nref / 2 - 1] + ref[nref / 2]) / 2.0;
  for (b = 0; b < nev; b++)
  {
    SGEV *e = &c->tracks[evs[b].t].ev[evs[b].r];
    SGINS *ins = &c->ins[e->ins];
    double target = pyround((evs[b].lv + k0) / 1.5) * 1.5;
    int cmd, data;
    if (choose_cmd(ins, target, evs[b].frames, 1.5, &cmd, &data))
    {
      e->hascmd = 1;
      memset(&e->cmd, 0, sizeof e->cmd);
      e->cmd.cmd = cmd;
      e->cmd.kind = CD_PLAIN;
      e->cmd.a = data;
      n++;
    }
  }
  x->c->nlevels = n;
  free(ref);
  free(evs);
  free(frames_row);
}

static void imp_free(IMP *x)
{
  int k;
  for (k = 0; k < x->nparts; k++)
  {
    free(x->parts[k].notes);
    free(x->parts[k].lvr);
    free(x->parts[k].lvp);
    free(x->parts[k].lvv);
  }
  free(x->parts);
  free(x->drums);
  free(x->drum_gm);
  free(x->bar_tempo);
  free(x->lvany);
  free(x->lead);
  free(x->bass);
  free(x->counter);
  free(x->mel_source);
  free(x->inner);
  free(x->chords);
  free(x->haschord);
  free(x->snd);
  free(x->harm_onset);
  free(x->harm_notes);
  free(x->poly_ins);
  free(x->npoly_ins);
  if (x->mf) mf_free(x->mf);
}

void midiopts_default(MIDIOPTS *o)
{
  memset(o, 0, sizeof *o);
  o->voices = 3;
  o->sids = 1;
  o->model = 6581;
  o->hint_dance = -1;
}

COMP *sg_import_midi(const unsigned char *data, int len, const MIDIOPTS *opts, MIDIINFO *info, char *err, int errsize)
{
  IMP *x = (IMP *)calloc(1, sizeof(IMP));
  COMP *c;
  int k;
  sg_init();
  x->o = *opts;
  x->voices = opts->voices == 2 ? 2 : 3;
  x->sids = ((opts->sids == 2) || (opts->sids == 4) || (opts->sids == 6) || (opts->sids == 8)) ? opts->sids : 1;
  x->model = opts->model == 8580 ? 8580 : 6581;
  if (opts->variant)
  {
    rng_seeds(&x->var_rng, "midi-variant-%d", opts->variant);
    x->hasvar = 1;
  }
  x->mf = read_midi(data, len, err, errsize);
  if (!x->mf)
  {
    free(x);
    return NULL;
  }
  c = (COMP *)calloc(1, sizeof(COMP));
  c->model = x->model;
  c->voices = x->voices;
  c->sids = x->sids;
  c->kind = 1;
  x->c = c;
  if (!analyze(x))
  {
    snprintf(err, errsize, "%s", x->err);
    imp_free(x);
    free(x);
    comp_free(c);
    return NULL;
  }
  x->poly_ins = calloc(x->nparts + 1, sizeof(*x->poly_ins));
  x->npoly_ins = (int *)calloc(x->nparts + 1, sizeof(int));
  extract(x);
  pick_instruments(x);
  if (x->sids >= 4) arrangeN(x);
  else if (x->sids == 2)
  {
    x->nextra_poly = 0;
    arrange6(x, 0);
    c->ntracks = 6;
  }
  else arrange3(x);
  limit_instruments(x, 60);
  apply_levels(x);
  // the song: tempo per bar, pattern length
  c->spb = x->bar_rows;
  c->bt = x->beat_rows;
  c->total_rows = x->total_rows;
  c->total_bars = x->nbars;
  c->tempo[0] = x->bar_tempo[0][0];
  c->tempo[1] = x->bar_tempo[0][1];
  {
    int lt0 = -1, lt1 = -1, b, mt = 99;
    c->ntempo = 0;
    c->nspx = 0;
    for (b = 0; b < x->nbars; b++)
    {
      int t0 = x->bar_tempo[b][0], t1 = x->bar_tempo[b][1];
      if (t0 < mt) mt = t0;
      if (t1 < mt) mt = t1;
      if ((t0 != lt0) || (t1 != lt1))
      {
        if (c->ntempo < 4096)
        {
          c->temporow[c->ntempo] = b * x->bar_rows;
          if (t0 == t1)
          {
            c->tempocmd[c->ntempo] = CMD_SETTEMPO_;
            c->tempoval[c->ntempo] = t0;
          }
          else
          {
            int j;
            for (j = 0; j < c->nspx; j++)
              if ((c->spx[j][0] == t0) && (c->spx[j][1] == t1)) break;
            if ((j >= c->nspx) && (c->nspx < 64))
            {
              c->spx[c->nspx][0] = t0;
              c->spx[c->nspx][1] = t1;
              j = c->nspx++;
            }
            c->tempocmd[c->ntempo] = CMD_FUNKTEMPO_;
            c->tempoval[c->ntempo] = j + 1;
          }
          c->ntempo++;
        }
        lt0 = t0;
        lt1 = t1;
      }
    }
    c->mintempo0 = mt;
  }
  c->plen0 = x->bar_rows * (64 / x->bar_rows > 1 ? 64 / x->bar_rows : 1);
  if (c->plen0 > 128) c->plen0 = 128;
  {
    double avg = 0;
    int b;
    for (b = 0; b < x->nbars; b++) avg += (x->bar_tempo[b][0] + x->bar_tempo[b][1]) / 2.0 * x->bar_rows;
    c->seconds = avg / 50.0;
    c->bpm = 3000.0 / ((c->tempo[0] + c->tempo[1]) / 2.0 * x->q);
  }
  snprintf(c->title, sizeof c->title, "%.60s", opts->title[0] ? opts->title : (strlen(x->mf->title) > 2 ? x->mf->title : (opts->basename[0] ? opts->basename : "midi")));
  if (info)
  {
    memset(info, 0, sizeof *info);
    info->nparts = x->nparts < 64 ? x->nparts : 64;
    for (k = 0; k < info->nparts; k++)
    {
      PART *p = &x->parts[k];
      info->track[k] = p->track;
      info->channel[k] = p->channel;
      info->program[k] = p->program;
      info->role[k] = p->role;
      info->notes[k] = p->n;
      snprintf(info->name[k], sizeof info->name[k], "%.24s", p->name[0] ? p->name : "");
    }
    info->drum_notes = x->drum_notes;
    info->q = x->q;
    info->bar_rows = x->bar_rows;
    info->nbars = x->nbars;
    info->timesig[0] = x->timesig[0];
    info->timesig[1] = x->timesig[1];
    info->midi_bpm = 60e6 / x->main_us;
    info->midi_seconds = mf_seconds(x->mf, mf_endtick(x->mf)) - mf_seconds(x->mf, (int)x->shift);
    info->dance = x->dance;
    info->harm_beats = x->harmony_beats;
    info->harm_changed = x->harmony_changed;
    info->harm_simplified = x->harmony_simplified;
    info->harm_rests = x->harmony_rests;
    info->level_commands = c->nlevels;
    snprintf(info->title, sizeof info->title, "%s", c->title);
  }
  imp_free(x);
  free(x);
  return c;
}

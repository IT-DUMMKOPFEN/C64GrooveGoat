// C64 GrooveGoat - the song plan (composer 1.1), see gsgplan.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "gsgplan.h"

const char *plan_partname[PP_NUM] = {"intro", "verse", "chorus", "bridge", "break", "outro"};

static const int partsec[PP_NUM] = {SEC_INTRO, SEC_A, SEC_B, SEC_C, SEC_C, SEC_OUTRO};

void plan_clear(SONGPLAN *p)
{
  memset(p, 0, sizeof *p);
  p->key = -1;
  p->mode = -1;
}

int plan_bars(const SONGPLAN *p)
{
  int k, n = 0;
  for (k = 0; k < p->nsec; k++) n += p->sec[k].bars;
  return n;
}

// ---------------------------------------------------------------------------
// Chords as text
// ---------------------------------------------------------------------------

static const char *SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

static int planminor = 0;           // the mode of the key being written (for the spelling)

static int flatkey(int key)
{
  // major: F Bb Eb Ab Db; minor: D G C F Bb (Eb) are written with flats
  if (planminor) return (key == 2) || (key == 7) || (key == 0) || (key == 5) || (key == 10) || (key == 3);
  return (key == 5) || (key == 10) || (key == 3) || (key == 8) || (key == 1);
}

static int isminormode(int m)
{
  return (m == M_MINOR) || (m == M_DORIAN) || (m == M_PHRYGIAN) || (m == M_HARMMINOR) || (m == M_PENTMIN) || (m == M_BLUES);
}

void plan_chordname(CHORD ch, int key, char *out, int size)
{
  static const char *suf[Q_NUM] = {"", "m", "dim", "aug", "sus4", "sus2", "maj7", "m7", "7", "5", "m6", "6"};
  const char *r = (key >= 0) && flatkey(key) ? FLAT[ch.root % 12] : SHARP[ch.root % 12];
  snprintf(out, size, "%s%s", r, (ch.q >= 0) && (ch.q < Q_NUM) ? suf[ch.q] : "");
}

static int parseroot(const char **s)
{
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};      // A B C D E F G
  const char *p = *s;
  int c = toupper((unsigned char)*p), r;
  if ((c < 'A') || (c > 'G')) return -1;
  r = base[c - 'A'];
  p++;
  while ((*p == '#') || (*p == 'b'))
  {
    // 'b' after a root is a flat (Bb), not the start of a suffix
    r += *p == '#' ? 1 : -1;
    p++;
  }
  *s = p;
  return (r + 12) % 12;
}

static int parsechord(const char *tok, CHORD *out)
{
  const char *p = tok;
  int r = parseroot(&p);
  char suf[16];
  int k;
  if (r < 0) return 0;
  for (k = 0; (*p) && (*p != '/') && (k < 15); p++) suf[k++] = (char)tolower((unsigned char)*p);
  suf[k] = 0;
  out->root = r;
  if ((!suf[0]) || (!strcmp(suf, "maj")) || (!strcmp(suf, "major")) || (!strcmp(suf, "add9"))) out->q = Q_MAJ;
  else if ((!strcmp(suf, "m")) || (!strcmp(suf, "min")) || (!strcmp(suf, "minor")) || (!strcmp(suf, "-")) || (!strcmp(suf, "madd9"))) out->q = Q_MIN;
  else if ((!strcmp(suf, "dim")) || (!strcmp(suf, "o")) || (!strcmp(suf, "m7b5")) || (!strcmp(suf, "dim7"))) out->q = Q_DIM;
  else if ((!strcmp(suf, "aug")) || (!strcmp(suf, "+"))) out->q = Q_AUG;
  else if ((!strcmp(suf, "sus4")) || (!strcmp(suf, "sus"))) out->q = Q_SUS4;
  else if (!strcmp(suf, "sus2")) out->q = Q_SUS2;
  else if ((!strcmp(suf, "maj7")) || (!strcmp(suf, "m7+")) || (!strcmp(suf, "maj9"))) out->q = Q_MAJ7;
  else if ((!strcmp(suf, "m7")) || (!strcmp(suf, "min7")) || (!strcmp(suf, "m9")) || (!strcmp(suf, "m11"))) out->q = Q_MIN7;
  else if ((!strcmp(suf, "7")) || (!strcmp(suf, "9")) || (!strcmp(suf, "13")) || (!strcmp(suf, "11"))) out->q = Q_DOM7;
  else if (!strcmp(suf, "5")) out->q = Q_POW;
  else if (!strcmp(suf, "m6")) out->q = Q_MIN6;
  else if (!strcmp(suf, "6")) out->q = Q_MAJ6;
  else if (suf[0] == 'm') out->q = Q_MIN;         // unknown minor-ish extensions
  else out->q = Q_MAJ;
  return 1;
}

int plan_parsechords(const char *text, int key, CHORD *out, int max)
{
  char tok[24];
  int n = 0, k;
  const char *s = text;
  (void)key;
  while ((*s) && (n < max))
  {
    while ((*s) && ((isspace((unsigned char)*s)) || (*s == ',') || (*s == '|'))) s++;
    if (!*s) break;
    for (k = 0; (*s) && (!isspace((unsigned char)*s)) && (*s != ',') && (*s != '|') && (k < 23); s++) tok[k++] = *s;
    tok[k] = 0;
    if ((!strcmp(tok, ".")) || (!strcmp(tok, "-")) || (!strcmp(tok, "%")))
    {
      if (n) { out[n] = out[n - 1]; n++; }
      continue;
    }
    if (parsechord(tok, &out[n])) n++;
  }
  return n;
}

int plan_mode_index(const char *name)
{
  char n[40];
  int k, j = 0;
  for (k = 0; (name[k]) && (j < 39); k++)
    if ((name[k] != ' ') && (name[k] != '_') && (name[k] != '-')) n[j++] = (char)tolower((unsigned char)name[k]);
  n[j] = 0;
  if ((!strcmp(n, "major")) || (!strcmp(n, "ionian")) || (!strcmp(n, "maj"))) return M_MAJOR;
  if ((!strcmp(n, "minor")) || (!strcmp(n, "aeolian")) || (!strcmp(n, "naturalminor")) || (!strcmp(n, "min"))) return M_MINOR;
  if (!strcmp(n, "dorian")) return M_DORIAN;
  if (!strcmp(n, "phrygian")) return M_PHRYGIAN;
  if (!strcmp(n, "lydian")) return M_LYDIAN;
  if (!strcmp(n, "mixolydian")) return M_MIXOLYDIAN;
  if ((!strcmp(n, "harmonicminor")) || (!strcmp(n, "harmonic"))) return M_HARMMINOR;
  if ((!strcmp(n, "majorpentatonic")) || (!strcmp(n, "pentatonicmajor")) || (!strcmp(n, "pentmajor"))) return M_PENTMAJ;
  if ((!strcmp(n, "minorpentatonic")) || (!strcmp(n, "pentatonicminor")) || (!strcmp(n, "pentminor")) || (!strcmp(n, "pentatonic")))
    return M_PENTMIN;
  if ((!strcmp(n, "blues")) || (!strcmp(n, "bluesscale"))) return M_BLUES;
  return -1;
}

// ---------------------------------------------------------------------------
// The plan of a composed piece
// ---------------------------------------------------------------------------

// the scale degree of a note (1 = the keynote of the octave 'base')
static int degree_of(const COMP *c, int note, int base)
{
  int d = 0, n;
  if (note >= base)
  {
    for (n = base; n < note; n++)
      if (scale_contains(&c->scale, n + 1)) d++;
    if (!scale_contains(&c->scale, note)) d--;
    return d + 1;
  }
  for (n = base; n > note; n--)
    if (scale_contains(&c->scale, n - 1)) d--;
  return d + 1;
}

static int partof(int sec)
{
  int b = sec_base(sec);
  return b == SEC_INTRO ? PP_INTRO : b == SEC_A ? PP_VERSE : b == SEC_B ? PP_CHORUS : b == SEC_C ? PP_BRIDGE : PP_OUTRO;
}

void plan_from_comp(const COMP *c, SONGPLAN *p)
{
  int si, row = 0, k;
  char keepidea[256];
  snprintf(keepidea, sizeof keepidea, "%s", p->idea);
  plan_clear(p);
  snprintf(p->idea, sizeof p->idea, "%s", keepidea);
  snprintf(p->title, sizeof p->title, "%s", c->title);
  snprintf(p->genre, sizeof p->genre, "%s", c->stylekey);
  planminor = isminormode(c->mode);
  p->key = c->key;
  p->mode = c->mode;
  p->bpm = (int)(c->bpm + 0.5);
  for (si = 0; (si < c->nsecs) && (p->nsec < PLAN_MAXSECS); si++)
  {
    PLANSEC *s = &p->sec[p->nsec++];
    int b, nb = c->secbars[si], per, o = 0;
    CHORD bar[64];
    s->part = partof(c->secs[si]);
    s->bars = nb;
    s->energy = c->engine ? c->energy[si] : 0.6;
    for (b = 0; (b < nb) && (b < 64); b++)
    {
      int r = row + b * c->spb;
      bar[b] = (c->chord_rows) && (r < c->nchordrows) ? c->chord_rows[r] : c->tonic;
    }
    // the shortest repeating period of the chords
    for (per = 1; per < nb; per++)
    {
      int ok = 1;
      if (nb % per) continue;
      for (b = per; (b < nb) && (ok); b++)
        if ((bar[b].root != bar[b - per].root) || (bar[b].q != bar[b - per].q)) ok = 0;
      if (ok) break;
    }
    if (per > 16) per = 16;
    for (b = 0; b < per; b++)
    {
      char nm[12];
      if ((b) && (bar[b].root == bar[b - 1].root) && (bar[b].q == bar[b - 1].q)) snprintf(nm, sizeof nm, ".");
      else plan_chordname(bar[b], c->key, nm, sizeof nm);
      o += snprintf(s->chords + o, sizeof s->chords - o, "%s%s", b ? " " : "", nm);
      if (o >= (int)sizeof s->chords - 8) break;
    }
    row += nb * c->spb;
  }
  // the theme: the first notes of the lead in the first verse (or the first part with a lead)
  if ((c->engine) && (c->theme_have))
  {
    int r0 = -1, len = c->theme_mb * c->spb, o = 0, i, base;
    row = 0;
    for (si = 0; si < c->nsecs; si++)
    {
      if (sec_base(c->secs[si]) == SEC_A) { r0 = row; break; }
      row += c->secbars[si] * c->spb;
    }
    if (r0 < 0) r0 = 0;
    // the rhythm of the theme (16 steps per bar)
    memset(p->rhythm, 0, sizeof p->rhythm);
    for (i = 0; (i < c->theme_mb * 16) && (i < 64); i++) p->rhythm[i] = '.';
    for (i = 0; i < c->theme.n; i++)
    {
      int st = c->theme.v[i].pos * 16 / c->spb, d = c->theme.v[i].d * 16 / c->spb, k2;
      if (st >= 64) break;
      p->rhythm[st] = 'x';
      for (k2 = 1; (k2 < d) && (st + k2 < 64); k2++) p->rhythm[st + k2] = '-';
    }
    // the degrees: the notes the lead plays there
    base = -1;
    for (k = 0; (k < c->lead_rows.n) && (o < PLAN_MAXTHEME); k++)
    {
      const RND *x = &c->lead_rows.v[k];
      if ((x->row < r0) || (x->row >= r0 + len)) continue;
      if (base < 0)
      {
        base = x->note;
        while ((((base - c->key) % 12) + 12) % 12) base--;
      }
      p->theme[o++] = degree_of(c, x->note, base);
    }
    p->ntheme = o;
    if (!o) p->rhythm[0] = 0;
  }
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

static int jstr(char *out, int size, const char *s)
{
  int o = 0;
  if (size < 3) return 0;
  out[o++] = '"';
  for (; (*s) && (o < size - 3); s++)
  {
    unsigned char ch = *s;
    if ((ch == '"') || (ch == '\\')) { out[o++] = '\\'; out[o++] = ch; }
    else if (ch == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
    else if (ch < 32) out[o++] = ' ';
    else out[o++] = ch;
  }
  out[o++] = '"';
  out[o] = 0;
  return o;
}

int plan_to_json(const SONGPLAN *p, char *out, int size)
{
  int o = 0, k;
  char b[600];
#define PUT(...) do { o += snprintf(out + o, size - o > 0 ? size - o : 0, __VA_ARGS__); if (o >= size) o = size - 1; } while (0)
  planminor = isminormode(p->mode);
  PUT("{\n");
  jstr(b, sizeof b, p->idea); PUT("  \"idea\": %s,\n", b);
  jstr(b, sizeof b, p->title); PUT("  \"title\": %s,\n", b);
  jstr(b, sizeof b, p->genre); PUT("  \"genre\": %s,\n", b);
  PUT("  \"key\": \"%s\",\n", p->key >= 0 ? (flatkey(p->key) ? FLAT[p->key] : SHARP[p->key]) : "");
  PUT("  \"scale\": \"%s\",\n", p->mode >= 0 ? sg_modelabel[p->mode] : "");
  PUT("  \"bpm\": %d,\n", p->bpm);
  jstr(b, sizeof b, p->mood); PUT("  \"mood\": %s,\n", b);
  PUT("  \"theme\": {\"degrees\": [");
  for (k = 0; k < p->ntheme; k++) PUT("%s%d", k ? ", " : "", p->theme[k]);
  PUT("], \"rhythm\": \"%s\"},\n", p->rhythm);
  PUT("  \"sections\": [\n");
  for (k = 0; k < p->nsec; k++)
  {
    const PLANSEC *s = &p->sec[k];
    char ch[200], id[120];
    jstr(ch, sizeof ch, s->chords);
    jstr(id, sizeof id, s->idea);
    PUT("    {\"part\": \"%s\", \"bars\": %d, \"energy\": %.2f, \"chords\": %s%s%s}%s\n", plan_partname[s->part], s->bars, s->energy, ch,
        s->idea[0] ? ", \"idea\": " : "", s->idea[0] ? id : "", k + 1 < p->nsec ? "," : "");
  }
  PUT("  ]\n}\n");
#undef PUT
  return o;
}

static int partindex(const char *s)
{
  char n[24];
  int k, j = 0;
  for (k = 0; (s[k]) && (j < 23); k++)
    if (isalpha((unsigned char)s[k])) n[j++] = (char)tolower((unsigned char)s[k]);
  n[j] = 0;
  for (k = 0; k < PP_NUM; k++)
    if (!strcmp(n, plan_partname[k])) return k;
  if ((strstr(n, "pre")) || (strstr(n, "build")) || (strstr(n, "bridge"))) return PP_BRIDGE;
  if ((strstr(n, "chorus")) || (strstr(n, "hook")) || (strstr(n, "drop")) || (strstr(n, "refrain"))) return PP_CHORUS;
  if ((strstr(n, "verse")) || (strstr(n, "strophe")) || (strstr(n, "main"))) return PP_VERSE;
  if ((strstr(n, "break")) || (strstr(n, "interlude")) || (strstr(n, "solo"))) return PP_BREAK;
  if ((strstr(n, "intro")) || (strstr(n, "opening"))) return PP_INTRO;
  if ((strstr(n, "outro")) || (strstr(n, "ending")) || (strstr(n, "coda")) || (strstr(n, "end"))) return PP_OUTRO;
  return -1;
}

int plan_parse(const char *text, SONGPLAN *p, char *msg, int msgsize)
{
  const char *a, *z;
  char *buf;
  JV *j, *v;
  int k, fixes = 0, depth = 0;
  char keepidea[256];
  if (msg) msg[0] = 0;
  // the JSON object in the text (a chatbot may write words around it)
  a = strchr(text, '{');
  if (!a)
  {
    if (msg) snprintf(msg, msgsize, "No plan found (no JSON object in the text)");
    return 0;
  }
  for (z = a; *z; z++)
  {
    if (*z == '"')
    {
      for (z++; (*z) && (*z != '"'); z++)
        if ((*z == '\\') && (z[1])) z++;
      if (!*z) break;
      continue;
    }
    if (*z == '{') depth++;
    if ((*z == '}') && (!--depth)) break;
  }
  if (!*z)
  {
    if (msg) snprintf(msg, msgsize, "The plan is incomplete (the JSON object is not closed)");
    return 0;
  }
  buf = (char *)malloc(z - a + 2);
  memcpy(buf, a, z - a + 1);
  buf[z - a + 1] = 0;
  j = jv_parse(buf);
  free(buf);
  snprintf(keepidea, sizeof keepidea, "%s", p->idea);
  plan_clear(p);
  snprintf(p->idea, sizeof p->idea, "%s", jv_str(jv_get(j, "idea"), keepidea));
  snprintf(p->title, sizeof p->title, "%s", jv_str(jv_get(j, "title"), ""));
  snprintf(p->mood, sizeof p->mood, "%s", jv_str(jv_get(j, "mood"), ""));
  {
    const char *g = jv_str(jv_get(j, "genre"), "");
    int i;
    for (i = 0; (g[i]) && (i < 31); i++) p->genre[i] = (char)tolower((unsigned char)g[i]);
    p->genre[i] = 0;
  }
  {
    const char *ks = jv_str(jv_get(j, "key"), "");
    const char *q = ks;
    int r = parseroot(&q);
    p->key = r;
    // "A minor" in the key field
    if ((r >= 0) && (*q))
    {
      while (*q == ' ') q++;
      if (*q == 'm' && (q[1] == 0)) p->mode = M_MINOR;
      else if (*q) p->mode = plan_mode_index(q);
    }
  }
  if ((v = jv_get(j, "scale")) && (jv_str(v, "")[0])) p->mode = plan_mode_index(jv_str(v, ""));
  if ((v = jv_get(j, "mode")) && (p->mode < 0) && (jv_str(v, "")[0])) p->mode = plan_mode_index(jv_str(v, ""));
  p->bpm = (int)jv_num(jv_get(j, "bpm"), jv_num(jv_get(j, "tempo"), 0));
  if ((p->bpm) && ((p->bpm < 60) || (p->bpm > 200)))
  {
    p->bpm = p->bpm < 60 ? 60 : 200;
    fixes++;
  }
  // theme
  if ((v = jv_get(j, "theme")))
  {
    const JV *dg = jv_get(v, "degrees");
    const char *rh = jv_str(jv_get(v, "rhythm"), "");
    int i, o = 0, nx = 0;
    for (i = 0; (i < jv_len(dg)) && (p->ntheme < PLAN_MAXTHEME); i++) p->theme[p->ntheme++] = (int)jv_num(jv_at(dg, i), 1);
    for (i = 0; (rh[i]) && (o < 64); i++)
    {
      char ch = rh[i];
      if ((ch == 'x') || (ch == 'X') || (ch == 'o')) { p->rhythm[o++] = 'x'; nx++; }
      else if ((ch == '-') || (ch == '_') || (ch == '=')) p->rhythm[o++] = '-';
      else if (ch == '.') p->rhythm[o++] = '.';
    }
    // whole bars only
    while (o % 16) p->rhythm[o++] = '.';
    p->rhythm[o] = 0;
    if ((!nx) || (!p->ntheme))
    {
      if ((p->ntheme) || (nx)) fixes++;
      p->ntheme = 0;
      p->rhythm[0] = 0;
    }
    else if (p->ntheme != nx) fixes++;
    for (i = 0; i < p->ntheme; i++)
    {
      if (p->theme[i] < -6) { p->theme[i] = -6; fixes++; }
      if (p->theme[i] > 15) { p->theme[i] = 15; fixes++; }
    }
  }
  // the parts
  v = jv_get(j, "sections");
  if (!v) v = jv_get(j, "parts");
  if (!v) v = jv_get(j, "structure");
  for (k = 0; (k < jv_len(v)) && (p->nsec < PLAN_MAXSECS); k++)
  {
    const JV *s = jv_at(v, k);
    PLANSEC *d = &p->sec[p->nsec];
    int part = partindex(jv_str(jv_get(s, "part"), jv_str(jv_get(s, "name"), jv_str(jv_get(s, "type"), ""))));
    double e = jv_num(jv_get(s, "energy"), -1);
    int bars = (int)jv_num(jv_get(s, "bars"), jv_num(jv_get(s, "length"), 8));
    const JV *ch = jv_get(s, "chords");
    if (part < 0) { part = PP_VERSE; fixes++; }
    d->part = part;
    if (bars < 2) { bars = 2; fixes++; }
    if (bars > 32) { bars = 32; fixes++; }
    if (bars % 2) { bars++; fixes++; }
    d->bars = bars;
    if (e > 1.0) { e = e / 10.0 > 1 ? 1 : e / 10.0; fixes++; }      // 0-10 instead of 0-1
    if (e < 0) e = part == PP_CHORUS ? 0.9 : part == PP_INTRO ? 0.3 : part == PP_OUTRO ? 0.4 : part == PP_VERSE ? 0.55 : 0.35;
    d->energy = e;
    if (ch && (ch->type == JV_ARR))
    {
      int i, o = 0;
      for (i = 0; i < jv_len(ch); i++)
        o += snprintf(d->chords + o, sizeof d->chords - o > 0 ? sizeof d->chords - o : 0, "%s%s", i ? " " : "", jv_str(jv_at(ch, i), "."));
    }
    else snprintf(d->chords, sizeof d->chords, "%s", jv_str(ch, ""));
    {
      CHORD t[64];
      if ((d->chords[0]) && (!plan_parsechords(d->chords, p->key, t, 64))) { d->chords[0] = 0; fixes++; }
    }
    snprintf(d->idea, sizeof d->idea, "%s", jv_str(jv_get(s, "idea"), jv_str(jv_get(s, "note"), "")));
    p->nsec++;
  }
  jv_free(j);
  if (!p->nsec)
  {
    if (msg) snprintf(msg, msgsize, "The plan has no parts");
    return 0;
  }
  // too long: parts from the end of the middle are dropped (max. 10 minutes at the tempo)
  {
    double bpm = p->bpm ? p->bpm : 120, sec = plan_bars(p) * 4 * 60.0 / bpm;
    while ((sec > 600) && (p->nsec > 2))
    {
      memmove(&p->sec[p->nsec - 2], &p->sec[p->nsec - 1], sizeof(PLANSEC));
      p->nsec--;
      fixes++;
      sec = plan_bars(p) * 4 * 60.0 / bpm;
    }
  }
  if (msg)
  {
    if (fixes) snprintf(msg, msgsize, "Plan read, %d thing%s repaired", fixes, fixes == 1 ? "" : "s");
    else snprintf(msg, msgsize, "Plan read and checked: ok");
  }
  return 1;
}

int plan_prompt(const SONGPLAN *p, const char *genre_hint, int seconds, int sids, char *out, int size)
{
  int o = 0, k;
#define PUT(...) do { o += snprintf(out + o, size - o > 0 ? size - o : 0, __VA_ARGS__); if (o >= size) o = size - 1; } while (0)
  PUT("You are the arranger of C64 GrooveGoat, a music program for the SID sound chip of the Commodore 64 "
      "(%d SID chip%s, %d voices). Plan a song for this idea:\n\n%s\n\n", sids, sids > 1 ? "s" : "", sids * 3,
      p->idea[0] ? p->idea : "a catchy song");
  PUT("Answer with one JSON object only, no other text, in exactly this form:\n"
      "{\"title\": \"Night Drive\", \"genre\": \"synthwave\", \"key\": \"A\", \"scale\": \"minor\", \"bpm\": 110, \"mood\": \"melancholic, driving\",\n"
      " \"theme\": {\"degrees\": [1, 2, 3, 5, 3, 2, 1], \"rhythm\": \"x-x-x---x-x-x---\"},\n"
      " \"sections\": [\n"
      "  {\"part\": \"intro\", \"bars\": 8, \"energy\": 0.3, \"chords\": \"Am . F . C . G .\"},\n"
      "  {\"part\": \"verse\", \"bars\": 16, \"energy\": 0.5, \"chords\": \"Am F C G\"},\n"
      "  {\"part\": \"chorus\", \"bars\": 8, \"energy\": 0.9, \"chords\": \"F G Am Am\"}\n"
      " ]}\n\n");
  PUT("Rules:\n"
      "- genre: one of: ");
  {
    char seen[600] = "";
    for (k = 0; k < sg_numstyles; k++)
    {
      char t[40];
      snprintf(t, sizeof t, "|%s|", sg_styles[k].key);
      if (strstr(seen, t)) continue;
      if (strlen(seen) + strlen(t) < sizeof seen - 1) strcat(seen, t);
      PUT("%s%s", k ? ", " : "", sg_styles[k].key);
    }
  }
  PUT(".%s%s%s\n", genre_hint && genre_hint[0] ? " Wanted: " : "", genre_hint ? genre_hint : "", genre_hint && genre_hint[0] ? "." : "");
  PUT("- part: intro, verse, chorus, bridge, break or outro. Typical: intro, verse, chorus, verse, chorus, bridge or break, "
      "chorus, outro.\n"
      "- bars: 4, 8, 12 or 16. The whole song should last about %d seconds at the tempo (4 beats per bar).\n"
      "- energy: 0.0 (quiet) to 1.0 (peak). Build tension towards the choruses; a bridge or break drops; the last chorus is the "
      "peak; the outro falls.\n"
      "- chords: one chord per bar, separated by spaces; \".\" holds the chord of the bar before; the list repeats until the part "
      "ends. Use chords that fit the key and scale, for example C, Cm, C7, Cmaj7, Cm7, Cdim, Csus4, C5.\n"
      "- theme: the main motif of the song, 1 or 2 bars. rhythm: 16 steps per bar, \"x\" starts a note, \"-\" holds it, \".\" "
      "is a rest. degrees: one scale degree for every \"x\" (1 = keynote, 8 = the keynote an octave higher, 0 and below go "
      "under the keynote). A singable motif with a clear shape.\n"
      "- title: short and catchy. mood: a few words.\n", seconds > 0 ? seconds : 150);
#undef PUT
  return o;
}

// ---------------------------------------------------------------------------
// Composer hooks
// ---------------------------------------------------------------------------

#define PL(c) ((const SONGPLAN *)(c)->plan)

void plan_hook_progs(COMP *c)
{
  const SONGPLAN *p = PL(c);
  int k, s, done[SEC_NUM] = {0};
  if (!p) return;
  for (k = 0; k < p->nsec; k++)
  {
    int sec = partsec[p->sec[k].part], n;
    CHORD t[16];
    if (done[sec]) continue;
    n = plan_parsechords(p->sec[k].chords, c->key, t, 16);
    if (!n) continue;
    memcpy(c->progs[sec], t, sizeof(CHORD) * n);
    c->nprog[sec] = n;
    c->chord_bars[sec] = 1;
    done[sec] = 1;
  }
  for (s = SEC_A2; s <= SEC_C2; s++)
  {
    memcpy(c->progs[s], c->progs[sec_base(s)], sizeof c->progs[0]);
    c->nprog[s] = c->nprog[sec_base(s)];
    c->chord_bars[s] = c->chord_bars[sec_base(s)];
  }
}

// 4-8 SIDs: parts of the same kind take the chords and the energy of the
// first one, so they share their patterns (the room for patterns is small)
static int samekind(const SONGPLAN *p, const COMP *c, int si)
{
  int k;
  if (c->sids < 4) return si;
  for (k = 0; k < si; k++)
    if (p->sec[k].part == p->sec[si].part) return k;
  return si;
}

void plan_hook_structure(COMP *c)
{
  const SONGPLAN *p = PL(c);
  int k;
  if (!p) return;
  c->nsecs = 0;
  for (k = 0; (k < p->nsec) && (c->nsecs < SG_MAXSECS); k++)
  {
    c->secs[c->nsecs] = partsec[p->sec[k].part];
    c->secbars[c->nsecs] = p->sec[k].bars > 64 ? 64 : p->sec[k].bars;
    c->energy[c->nsecs] = p->sec[samekind(p, c, k)].energy;
    c->nsecs++;
  }
  c->total_bars = 0;
  for (k = 0; k < c->nsecs; k++) c->total_bars += c->secbars[k];
  c->total_rows = c->total_bars * c->spb;
  c->seconds = c->total_bars * c->bar_sec;
  if (p->title[0]) snprintf(c->title, sizeof c->title, "%s", p->title);
}

void plan_hook_theme(COMP *c)
{
  const SONGPLAN *p = PL(c);
  int i, n = 0, len, bars;
  MOTIF *m = &c->theme;
  if ((!p) || (!p->ntheme) || (!p->rhythm[0])) return;
  len = (int)strlen(p->rhythm);
  bars = len / 16;
  if (bars < 1) return;
  if (bars > 4) bars = 4;
  m->n = 0;
  for (i = 0; (i < bars * 16) && (m->n < 96); i++)
  {
    int d = 1, st, deg, prevdeg;
    if (p->rhythm[i] != 'x') continue;
    while ((i + d < bars * 16) && (p->rhythm[i + d] == '-')) d++;
    st = i * c->spb / 16;
    deg = p->theme[n % p->ntheme];
    prevdeg = n ? p->theme[(n - 1) % p->ntheme] : deg;
    m->v[m->n].pos = st;
    m->v[m->n].d = d * c->spb / 16 > 0 ? d * c->spb / 16 : 1;
    m->v[m->n].strong = (st % (2 * c->bt) == 0) || (m->v[m->n].d >= c->bt);
    m->v[m->n].dlt = deg - prevdeg;
    if (m->v[m->n].dlt > 3) m->v[m->n].dlt = 3;
    if (m->v[m->n].dlt < -3) m->v[m->n].dlt = -3;
    m->v[m->n].deg = deg;
    m->n++;
    n++;
  }
  if (!m->n) return;
  m->degs = 1;
  c->theme_mb = bars;
  c->theme_have = 1;
}

int plan_hook_chords(COMP *c, int si, int nbars, CHORD *out)
{
  const SONGPLAN *p = PL(c);
  CHORD t[64];
  int n, b;
  if ((!p) || (si < 0) || (si >= p->nsec)) return 0;
  n = plan_parsechords(p->sec[samekind(p, c, si)].chords, c->key, t, 64);
  if (!n) return 0;
  for (b = 0; b < nbars; b++) out[b] = t[b % n];
  return 1;
}

char *plan_readfile(const char *path)
{
  FILE *f = fopen(path, "rb");
  long n;
  char *d;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((n < 0) || (n > 1000000)) { fclose(f); return NULL; }
  d = (char *)malloc(n + 1);
  n = (long)fread(d, 1, n, f);
  d[n] = 0;
  fclose(f);
  return d;
}

int plan_writefile(const char *path, const SONGPLAN *p)
{
  static char out[32768];
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  plan_to_json(p, out, sizeof out);
  fputs(out, f);
  fclose(f);
  return 1;
}

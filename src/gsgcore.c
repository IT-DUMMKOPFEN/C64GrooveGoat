// C64 GrooveGoat - core: JSON, random numbers, music theory,
// instrument data, tracks

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "gsg.h"

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

static const char *jp;

static void jws(void)
{
  while ((*jp == ' ') || (*jp == '\n') || (*jp == '\t') || (*jp == '\r')) jp++;
}

static JV *jnew(int type)
{
  JV *v = (JV *)calloc(1, sizeof(JV));
  v->type = type;
  return v;
}

static char *jstring(void)
{
  int cap = 32, n = 0;
  char *s = (char *)malloc(cap);
  jp++;
  while ((*jp) && (*jp != '"'))
  {
    int c = (unsigned char)*jp++;
    if (c == '\\')
    {
      c = *jp++;
      switch (c)
      {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'u':
        {
          unsigned u = 0;
          int k;
          for (k = 0; (k < 4) && (*jp); k++)
          {
            int h = *jp++;
            u = u * 16 + (isdigit(h) ? h - '0' : (tolower(h) - 'a' + 10));
          }
          c = u < 128 ? (int)u : '?';
        }
        break;
        default: break;
      }
    }
    if (n + 2 >= cap) s = (char *)realloc(s, cap *= 2);
    s[n++] = c;
  }
  if (*jp == '"') jp++;
  s[n] = 0;
  return s;
}

static JV *jvalue(void);

static void jpush(JV *v, JV *item, char *key)
{
  v->items = (JV **)realloc(v->items, sizeof(JV *) * (v->n + 1));
  if (v->type == JV_OBJ) v->keys = (char **)realloc(v->keys, sizeof(char *) * (v->n + 1));
  v->items[v->n] = item;
  if (v->type == JV_OBJ) v->keys[v->n] = key;
  v->n++;
}

static JV *jvalue(void)
{
  JV *v;
  jws();
  switch (*jp)
  {
    case '{':
    v = jnew(JV_OBJ);
    jp++;
    for (;;)
    {
      char *k;
      jws();
      if (*jp == '}') { jp++; break; }
      if (*jp != '"') break;
      k = jstring();
      jws();
      if (*jp == ':') jp++;
      jpush(v, jvalue(), k);
      jws();
      if (*jp == ',') jp++;
    }
    return v;
    case '[':
    v = jnew(JV_ARR);
    jp++;
    for (;;)
    {
      jws();
      if (*jp == ']') { jp++; break; }
      if (!*jp) break;
      jpush(v, jvalue(), NULL);
      jws();
      if (*jp == ',') jp++;
    }
    return v;
    case '"':
    v = jnew(JV_STR);
    v->str = jstring();
    return v;
    case 't': jp += 4; v = jnew(JV_BOOL); v->num = 1; return v;
    case 'f': jp += 5; v = jnew(JV_BOOL); v->num = 0; return v;
    case 'n': jp += 4; return jnew(JV_NULL);
    default:
    {
      char *e;
      v = jnew(JV_NUM);
      v->num = strtod(jp, &e);
      if (e == jp) e++;
      jp = e;
      return v;
    }
  }
}

JV *jv_parse(const char *s)
{
  jp = s;
  return jvalue();
}

void jv_free(JV *v)
{
  int c;
  if (!v) return;
  for (c = 0; c < v->n; c++)
  {
    jv_free(v->items[c]);
    if (v->keys) free(v->keys[c]);
  }
  free(v->items);
  free(v->keys);
  free(v->str);
  free(v);
}

JV *jv_get(const JV *o, const char *key)
{
  int c;
  if ((!o) || (o->type != JV_OBJ)) return NULL;
  for (c = 0; c < o->n; c++)
    if (!strcmp(o->keys[c], key)) return o->items[c];
  return NULL;
}

JV *jv_at(const JV *a, int i)
{
  if ((!a) || ((a->type != JV_ARR) && (a->type != JV_OBJ)) || (i < 0) || (i >= a->n)) return NULL;
  return a->items[i];
}

int jv_len(const JV *a) { return a && ((a->type == JV_ARR) || (a->type == JV_OBJ)) ? a->n : 0; }
double jv_num(const JV *v, double def) { return v && ((v->type == JV_NUM) || (v->type == JV_BOOL)) ? v->num : def; }
const char *jv_str(const JV *v, const char *def) { return v && v->type == JV_STR ? v->str : def; }
int jv_true(const JV *v) { return v && (((v->type == JV_BOOL) || (v->type == JV_NUM)) ? v->num != 0 : v->type != JV_NULL); }

// ---------------------------------------------------------------------------
// Random numbers: MT19937 with the methods of Python's random module
// ---------------------------------------------------------------------------

static void mt_init(SGRNG *r, uint32_t s)
{
  int i;
  r->mt[0] = s;
  for (i = 1; i < 624; i++) r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + i;
  r->mti = 624;
}

static void mt_initarray(SGRNG *r, const uint32_t *key, int len)
{
  int i = 1, j = 0, k;
  mt_init(r, 19650218u);
  for (k = (624 > len ? 624 : len); k; k--)
  {
    r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525u)) + key[j] + j;
    i++;
    j++;
    if (i >= 624) { r->mt[0] = r->mt[623]; i = 1; }
    if (j >= len) j = 0;
  }
  for (k = 623; k; k--)
  {
    r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941u)) - i;
    i++;
    if (i >= 624) { r->mt[0] = r->mt[623]; i = 1; }
  }
  r->mt[0] = 0x80000000u;
}

static uint32_t mt_next(SGRNG *r)
{
  uint32_t y;
  if (r->mti >= 624)
  {
    int k;
    for (k = 0; k < 624; k++)
    {
      y = (r->mt[k] & 0x80000000u) | (r->mt[(k + 1) % 624] & 0x7fffffffu);
      r->mt[k] = r->mt[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0);
    }
    r->mti = 0;
  }
  y = r->mt[r->mti++];
  y ^= y >> 11;
  y ^= (y << 7) & 0x9d2c5680u;
  y ^= (y << 15) & 0xefc60000u;
  y ^= y >> 18;
  return y;
}

void rng_seed(SGRNG *r, uint32_t seed)
{
  mt_initarray(r, &seed, 1);
}

// SHA-512 (for Python's seeding from a text, random.seed(str) version 2)
static const uint64_t K512[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
  0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
  0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
  0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
  0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
  0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
  0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
  0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
  0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
  0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
  0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
  0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512(const unsigned char *msg, int len, unsigned char *out)
{
  uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  unsigned char buf[512];
  int total = ((len + 17 + 127) / 128) * 128, blk, k;
  memset(buf, 0, sizeof buf);
  memcpy(buf, msg, len);
  buf[len] = 0x80;
  {
    uint64_t bits = (uint64_t)len * 8;
    for (k = 0; k < 8; k++) buf[total - 1 - k] = (unsigned char)(bits >> (8 * k));
  }
  for (blk = 0; blk < total; blk += 128)
  {
    uint64_t w[80], a, b, c, d, e, f, g, hh;
    for (k = 0; k < 16; k++)
    {
      int j;
      w[k] = 0;
      for (j = 0; j < 8; j++) w[k] = (w[k] << 8) | buf[blk + k * 8 + j];
    }
    for (k = 16; k < 80; k++)
    {
      uint64_t s0 = ROR64(w[k - 15], 1) ^ ROR64(w[k - 15], 8) ^ (w[k - 15] >> 7);
      uint64_t s1 = ROR64(w[k - 2], 19) ^ ROR64(w[k - 2], 61) ^ (w[k - 2] >> 6);
      w[k] = w[k - 16] + s0 + w[k - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (k = 0; k < 80; k++)
    {
      uint64_t S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41), ch = (e & f) ^ (~e & g);
      uint64_t t1 = hh + S1 + ch + K512[k] + w[k];
      uint64_t S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39), maj = (a & b) ^ (a & c) ^ (b & c);
      uint64_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  for (k = 0; k < 64; k++) out[k] = (unsigned char)(h[k / 8] >> (56 - 8 * (k % 8)));
}

// seed from a text exactly like Python's random.Random("text")
void rng_seeds(SGRNG *r, const char *fmt, ...)
{
  char buf[200];
  unsigned char all[300];
  uint32_t key[80];
  va_list ap;
  int len, n, k, nk;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  len = strlen(buf);
  memcpy(all, buf, len);
  sha512((const unsigned char *)buf, len, all + len);
  n = len + 64;
  // big-endian bytes -> 32-bit words, least significant first
  nk = (n + 3) / 4;
  for (k = 0; k < nk; k++)
  {
    uint32_t v = 0;
    int b;
    for (b = 3; b >= 0; b--)
    {
      int pos = n - 1 - (k * 4 + b);
      v = (v << 8) | (pos >= 0 ? all[pos] : 0);
    }
    key[k] = v;
  }
  while ((nk > 1) && (!key[nk - 1])) nk--;
  mt_initarray(r, key, nk);
}

double rng_random(SGRNG *r)
{
  uint32_t a = mt_next(r) >> 5, b = mt_next(r) >> 6;
  return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

int rng_below(SGRNG *r, int n)
{
  int k = 0;
  uint32_t v;
  if (n <= 0) return 0;          // n = 1 still draws, as Python's _randbelow
  while ((k < 32) && (((uint32_t)n >> k) != 0)) k++;     // n.bit_length() as in Python
  do v = mt_next(r) >> (32 - k);
  while (v >= (uint32_t)n);
  return (int)v;
}

double rng_uniform(SGRNG *r, double a, double b)
{
  return a + (b - a) * rng_random(r);
}

int rng_wchoice(SGRNG *r, const double *w, int n)
{
  double tot, x;
  int c;
  PYSUM ps;
  ps_init(&ps);
  for (c = 0; c < n; c++) ps_add(&ps, w[c]);
  tot = ps_get(&ps);
  x = rng_uniform(r, 0, tot);
  for (c = 0; c < n; c++)
  {
    x -= w[c];
    if (x <= 0) return c;
  }
  return n - 1;
}

int jv_wchoice(SGRNG *r, const JV *list)
{
  double w[256];
  int c, n = jv_len(list);
  if (n <= 0) return -1;
  if (n > 256) n = 256;
  for (c = 0; c < n; c++) w[c] = jv_num(jv_at(jv_at(list, c), 1), 1);
  return rng_wchoice(r, w, n);
}

JV *jv_wval(const JV *list, int i)
{
  return jv_at(jv_at(list, i), 0);
}

const char *jv_wstr(SGRNG *r, const JV *list)
{
  int i = jv_wchoice(r, list);
  return i < 0 ? NULL : jv_str(jv_wval(list, i), NULL);
}

const char *jv_choicestr(SGRNG *r, const JV *list)
{
  int n = jv_len(list);
  if (n <= 0) return NULL;
  return jv_str(jv_at(list, rng_below(r, n)), NULL);
}

// ---------------------------------------------------------------------------
// Music theory
// ---------------------------------------------------------------------------

const char *sg_qname[Q_NUM] = {"maj", "min", "dim", "aug", "sus4", "sus2", "maj7", "min7", "dom7", "pow", "min6", "maj6"};
const char *sg_qlabel[Q_NUM] = {"", "m", "dim", "aug", "sus4", "sus2", "maj7", "m7", "7", "5", "m6", "6"};
const char *sg_qfile[Q_NUM] = {"maj", "min", "dim", "aug", "sus4", "sus2", "maj7", "m7", "7", "5", "m6", "6"};
const int sg_qiv[Q_NUM][4] = {{0, 4, 7}, {0, 3, 7}, {0, 3, 6}, {0, 4, 8}, {0, 5, 7}, {0, 2, 7},
                              {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 7, 12}, {0, 3, 7, 9}, {0, 4, 7, 9}};
const int sg_qlen[Q_NUM] = {3, 3, 3, 3, 3, 3, 4, 4, 4, 3, 4, 4};
const char *sg_keyname[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

int sg_qindex(const char *name)
{
  int q;
  for (q = 0; q < Q_NUM; q++)
    if (!strcmp(sg_qname[q], name)) return q;
  return 0;
}

int sg_parsechord(const char *sym, CHORD *c)
{
  static const struct { const char *r; int v; } roman[] = {
    {"VII", 11}, {"VI", 9}, {"IV", 5}, {"V", 7}, {"III", 4}, {"II", 2}, {"I", 0}};
  const char *p = sym;
  int acc = 0, root = -1, upper = 0, k;
  if (*p == 'b') { acc = -1; p++; }
  else if (*p == '#') { acc = 1; p++; }
  for (k = 0; k < 7; k++)
  {
    int l = strlen(roman[k].r), m;
    for (m = 0; m < l; m++)
      if (toupper((unsigned char)p[m]) != roman[k].r[m]) break;
    if (m == l)
    {
      // the whole numeral must be in one case
      int up = isupper((unsigned char)p[0]) ? 1 : 0, ok = 1;
      for (m = 0; m < l; m++)
        if ((isupper((unsigned char)p[m]) ? 1 : 0) != up) ok = 0;
      if (!ok) continue;
      root = roman[k].v;
      upper = up;
      p += l;
      break;
    }
  }
  if (root < 0) return 0;
  root += acc;
  if (!*p) c->q = upper ? Q_MAJ : Q_MIN;
  else if (!strcmp(p, "7")) c->q = upper ? Q_DOM7 : Q_MIN7;
  else if (!strcmp(p, "maj7")) c->q = Q_MAJ7;
  else if (!strcmp(p, "m7")) c->q = Q_MIN7;
  else if (!strcmp(p, "o")) c->q = Q_DIM;
  else if (!strcmp(p, "+")) c->q = Q_AUG;
  else if (!strcmp(p, "5")) c->q = Q_POW;
  else if (!strcmp(p, "6")) c->q = upper ? Q_MAJ6 : Q_MIN6;
  else if (!strcmp(p, "sus4")) c->q = Q_SUS4;
  else if (!strcmp(p, "sus2")) c->q = Q_SUS2;
  else return 0;
  c->root = ((root % 12) + 12) % 12;
  return 1;
}

const char *sg_modename[M_NUM] = {"major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "harmonic_minor",
                                  "pent_major", "pent_minor", "blues"};
const char *sg_modelabel[M_NUM] = {"major", "minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "harmonic minor",
                                   "major pentatonic", "minor pentatonic", "blues scale"};
static const int modesteps[M_NUM][8] = {
  {0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1}, {0, 2, 3, 5, 7, 9, 10, -1}, {0, 1, 3, 5, 7, 8, 10, -1},
  {0, 2, 4, 6, 7, 9, 11, -1}, {0, 2, 4, 5, 7, 9, 10, -1}, {0, 2, 3, 5, 7, 8, 11, -1}, {0, 2, 4, 7, 9, -1},
  {0, 3, 5, 7, 10, -1}, {0, 3, 5, 6, 7, 10, -1}};

int sg_modeindex(const char *name)
{
  int m;
  for (m = 0; m < M_NUM; m++)
    if (!strcmp(sg_modename[m], name)) return m;
  return M_MINOR;
}

void scale_init(SCALE *s, int key, int mode)
{
  int c;
  s->key = key;
  s->mode = mode;
  s->nsteps = 0;
  s->pcs = 0;
  for (c = 0; (c < 8) && (modesteps[mode][c] >= 0); c++)
  {
    s->steps[s->nsteps++] = modesteps[mode][c];
    s->pcs |= 1 << ((key + modesteps[mode][c]) % 12);
  }
}

int scale_contains(const SCALE *s, int n)
{
  return (s->pcs >> (((n % 12) + 12) % 12)) & 1;
}

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------

static SGINS *ins = NULL;
static int numins = 0;
static int *sorted = NULL;          // indices sorted by name (hand-made first for equal names)
static SGARP arps[64];
static int numarps = 0;
static SGFILTER *filters = NULL;
static int numfilters = 0;
static char (*remfrom)[48], (*remto)[48];
static int numrem = 0;
static char (*mapfrom)[48], (*mapto)[48];
static int nummap = 0;
typedef struct { int model; char name[48]; double v; } PUNCHE;
static PUNCHE *punch = NULL;
static int numpunch = 0;
static int *gen[2];
static int numgen_[2];

static int hexn(const char *s, int n)
{
  int v = 0, c;
  for (c = 0; c < n; c++)
  {
    int h = s[c];
    v = v * 16 + (isdigit(h) ? h - '0' : (tolower(h) - 'a' + 10));
  }
  return v;
}

static int parseprog(const char *s, int *l, int *r, int max)
{
  int n = 0, len = strlen(s);
  while ((len >= 5) && (n < max))
  {
    l[n] = hexn(s, 2);
    r[n] = hexn(s + 2, 3);
    n++;
    s += 5;
    len -= 5;
  }
  return n;
}

// split one line at tabs (in place)
static int splittabs(char *line, char **f, int max)
{
  int n = 0;
  f[n++] = line;
  while ((*line) && (n < max))
  {
    if (*line == '\t')
    {
      *line = 0;
      f[n++] = line + 1;
    }
    line++;
  }
  return n;
}

static char *jointext(const char **parts)
{
  int len = 0, c;
  char *s;
  for (c = 0; parts[c]; c++) len += strlen(parts[c]);
  s = (char *)malloc(len + 1);
  s[0] = 0;
  len = 0;
  for (c = 0; parts[c]; c++)
  {
    strcpy(s + len, parts[c]);
    len += strlen(parts[c]);
  }
  return s;
}

static int cmpins(const void *a, const void *b)
{
  int ia = *(const int *)a, ib = *(const int *)b;
  int r = strcmp(ins[ia].name, ins[ib].name);
  if (r) return r;
  return ia - ib;
}

void sg_init(void)
{
  char *text, *line, *next;
  int c;
  if (ins) return;
  ins = (SGINS *)calloc(sg_numinstr + 1, sizeof(SGINS));
  text = jointext(sg_instrtext);
  for (line = text; *line; line = next)
  {
    char *f[24];
    int n;
    SGINS *i = &ins[numins];
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    else next = line + strlen(line);
    n = splittabs(line, f, 24);
    if (n < 16) continue;
    snprintf(i->name, sizeof i->name, "%s", f[0]);
    snprintf(i->cat, sizeof i->cat, "%s", f[1]);
    snprintf(i->fam, sizeof i->fam, "%s", f[2]);
    snprintf(i->theme, sizeof i->theme, "%s", f[3]);
    i->model = atoi(f[4]);
    i->ad = atoi(f[5]);
    i->sr = atoi(f[6]);
    i->gt = atoi(f[7]);
    i->fw = atoi(f[8]);
    i->hasvib = f[9][0] != 0;
    if (i->hasvib) sscanf(f[9], "%d,%d", &i->vib[0], &i->vib[1]);
    i->vibdelay = atoi(f[10]);
    snprintf(i->filt, sizeof i->filt, "%s", f[11]);
    i->nw = parseprog(f[12], i->wl, i->wr, 64);
    i->np = parseprog(f[13], i->pl, i->pr, 32);
    snprintf(i->en, sizeof i->en, "%s", f[14]);
    i->level = f[15][0] ? atof(f[15]) : -99;
    if (n > 16) snprintf(i->group, sizeof i->group, "%s", f[16]);
    numins++;
  }
  free(text);
  sorted = (int *)malloc(sizeof(int) * numins);
  for (c = 0; c < numins; c++) sorted[c] = c;
  qsort(sorted, numins, sizeof(int), cmpins);
  for (c = 0; c < 2; c++)
  {
    int k;
    gen[c] = (int *)malloc(sizeof(int) * numins);
    numgen_[c] = 0;
    for (k = 0; k < numins; k++)
      if (ins[k].model == (c ? 8580 : 6581)) gen[c][numgen_[c]++] = k;
  }

  // arpeggio templates
  {
    char *t = strdup(sg_arptext);
    for (line = t; *line; line = next)
    {
      char *f[12];
      SGARP *a = &arps[numarps];
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      if (splittabs(line, f, 12) < 9) continue;
      snprintf(a->name, sizeof a->name, "%s", f[0]);
      a->ad = atoi(f[1]);
      a->sr = atoi(f[2]);
      a->wavebyte = atoi(f[3]);
      a->speed = atoi(f[4]);
      a->octave = atoi(f[5]);
      a->np = parseprog(f[6], a->pl, a->pr, 32);
      snprintf(a->filt, sizeof a->filt, "%s", f[7]);
      snprintf(a->en, sizeof a->en, "%s", f[8]);
      if (numarps < 63) numarps++;
    }
    free(t);
  }
  // filter programs
  {
    char *t = jointext(sg_filtertext);
    int cap = 0;
    for (line = t; *line; line = next)
    {
      char *f[4];
      int n;
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      n = splittabs(line, f, 4);
      if (n < 2) continue;
      if (numfilters >= cap) filters = (SGFILTER *)realloc(filters, sizeof(SGFILTER) * (cap = cap * 2 + 64));
      snprintf(filters[numfilters].name, sizeof filters[numfilters].name, "%s", f[0]);
      filters[numfilters].n = parseprog(f[1], filters[numfilters].l, filters[numfilters].r, 64);
      filters[numfilters].scale8580 = (n > 2) && f[2][0] ? atof(f[2]) : 0;
      numfilters++;
    }
    free(t);
  }
  // REMOVED, MAP8580, PUNCH
  {
    char *t = strdup(sg_removed);
    remfrom = calloc(64, 48);
    remto = calloc(64, 48);
    for (line = t; *line; line = next)
    {
      char *f[3];
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      if ((splittabs(line, f, 3) < 2) || (numrem >= 64)) continue;
      snprintf(remfrom[numrem], 48, "%s", f[0]);
      snprintf(remto[numrem], 48, "%s", f[1]);
      numrem++;
    }
    free(t);
    t = jointext(sg_map8580);
    mapfrom = calloc(256, 48);
    mapto = calloc(256, 48);
    for (line = t; *line; line = next)
    {
      char *f[3];
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      if ((splittabs(line, f, 3) < 2) || (nummap >= 256)) continue;
      snprintf(mapfrom[nummap], 48, "%s", f[0]);
      snprintf(mapto[nummap], 48, "%s", f[1]);
      nummap++;
    }
    free(t);
    t = jointext(sg_punchtext);
    {
      int cap = 0;
      for (line = t; *line; line = next)
      {
        char *f[4];
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        else next = line + strlen(line);
        if (splittabs(line, f, 4) < 3) continue;
        if (numpunch >= cap) punch = (PUNCHE *)realloc(punch, sizeof(PUNCHE) * (cap = cap * 2 + 256));
        punch[numpunch].model = atoi(f[0]);
        snprintf(punch[numpunch].name, 48, "%s", f[1]);
        punch[numpunch].v = atof(f[2]);
        numpunch++;
      }
    }
    free(t);
  }
}

static const char *removed(const char *name)
{
  int c;
  for (c = 0; c < numrem; c++)
    if (!strcmp(remfrom[c], name)) return remto[c];
  return name;
}

const SGINS *sg_findins(const char *name)
{
  int lo = 0, hi = numins - 1;
  sg_init();
  name = removed(name);
  while (lo <= hi)
  {
    int m = (lo + hi) / 2, r = strcmp(ins[sorted[m]].name, name);
    if (!r)
    {
      while ((m > 0) && (!strcmp(ins[sorted[m - 1]].name, name))) m--;
      return &ins[sorted[m]];
    }
    if (r < 0) lo = m + 1;
    else hi = m - 1;
  }
  return NULL;
}

const SGARP *sg_findarp(const char *name)
{
  int c;
  sg_init();
  name = removed(name);
  for (c = 0; c < numarps; c++)
    if (!strcmp(arps[c].name, name)) return &arps[c];
  return NULL;
}

const SGFILTER *sg_findfilter(const char *name)
{
  int c;
  sg_init();
  for (c = 0; c < numfilters; c++)
    if (!strcmp(filters[c].name, name)) return &filters[c];
  return NULL;
}

const char *sg_removed_name(const char *name) { sg_init(); return removed(name); }

const char *sg_formodel(const char *name, int model)
{
  int c;
  sg_init();
  if (model != 8580) return name;
  for (c = 0; c < nummap; c++)
    if (!strcmp(mapfrom[c], name)) return mapto[c];
  return name;
}

double sg_punch(int model, const char *name, int *found)
{
  int c;
  sg_init();
  for (c = 0; c < numpunch; c++)
    if ((punch[c].model == model) && (!strcmp(punch[c].name, name)))
    {
      if (found) *found = 1;
      return punch[c].v;
    }
  if (found) *found = 0;
  return 0;
}

int sg_mainwave(const SGINS *i)
{
  int c, w = -1;
  for (c = 0; c < i->nw; c++)
    if ((i->wl[c] != 0xff) && (i->wl[c] >= 0x10) && (i->wl[c] < 0x80)) w = i->wl[c];
  return w >= 0 ? (w & 0xf0) : 0x80;
}

int sg_timbrewave(const SGINS *i)
{
  int c, w = -1;
  for (c = 0; c < i->nw; c++)
    if ((i->wl[c] != 0xff) && (i->wl[c] >= 0x10) && (i->wl[c] < 0xe0)) w = i->wl[c];
  return w >= 0 ? (w & 0xf0) : 0;
}

int sg_numgen(int model) { sg_init(); return numgen_[model == 8580]; }
const SGINS *sg_gen(int model, int i) { return &ins[gen[model == 8580][i]]; }

void sg_makearp(const SGARP *t, int q, SGINS *o)
{
  int iv[6], n = 0, c, delay = t->speed - 1;
  memset(o, 0, sizeof *o);
  for (c = 0; c < sg_qlen[q]; c++) iv[n++] = sg_qiv[q][c];
  if (t->octave)
  {
    int has = 0;
    for (c = 0; c < n; c++)
      if (iv[c] == 12) has = 1;
    if (!has) iv[n++] = 12;
  }
  snprintf(o->name, sizeof o->name, "%s %s", t->name + 4, q == Q_MAJ ? "Dur" : q == Q_MIN ? "Moll" : q == Q_DIM ? "Dim" :
           q == Q_AUG ? "Aug" : q == Q_SUS4 ? "Sus4" : q == Q_SUS2 ? "Sus2" : q == Q_MAJ7 ? "Maj7" : q == Q_MIN7 ? "m7" :
           q == Q_DOM7 ? "7" : q == Q_POW ? "5" : q == Q_MIN6 ? "m6" : "6");
  snprintf(o->en, sizeof o->en, "%s %s", t->en + 4, sg_qfile[q]);
  {
    // the English name
    static char *at = NULL;
    char key[64], *p;
    if (!at) at = jointext(sg_arpentext);
    snprintf(key, sizeof key, "\n%s\t", o->name);
    p = strstr(at, key + 1) == at ? at - 1 : strstr(at, key);
    if (p)
    {
      char *e = p + strlen(key), *nl = strchr(e, '\n');
      int l = nl ? (int)(nl - e) : (int)strlen(e);
      snprintf(o->en, sizeof o->en, "%.*s", l < 47 ? l : 47, e);
    }
  }
  snprintf(o->cat, sizeof o->cat, "arp");
  snprintf(o->fam, sizeof o->fam, "Akkord");
  o->ad = t->ad;
  o->sr = t->sr;
  o->gt = 2;
  o->fw = 9;
  o->wl[o->nw] = t->wavebyte;
  o->wr[o->nw++] = iv[0];
  for (c = 1; c <= n; c++)
  {
    o->wl[o->nw] = delay;
    o->wr[o->nw++] = iv[c < n ? c : 0];
  }
  o->wl[o->nw] = 0xff;
  o->wr[o->nw++] = 2;
  memcpy(o->pl, t->pl, sizeof o->pl);
  memcpy(o->pr, t->pr, sizeof o->pr);
  o->np = t->np;
  snprintf(o->filt, sizeof o->filt, "%s", t->filt);
  o->level = -99;
  o->derived = 1;
}

static char (*wfrom)[40], (*wto)[40];
static int numwords = -1;

void sg_translate(const char *name, char *out, int size)
{
  char part[64];
  int n = 0, o = 0, k;
  const char *p = name;
  if (numwords < 0)
  {
    char *t = jointext(sg_wordtext), *line, *next;
    int cap = 0;
    numwords = 0;
    for (line = t; *line; line = next)
    {
      char *f[3];
      next = strchr(line, '\n');
      if (next) *next++ = 0;
      else next = line + strlen(line);
      if (splittabs(line, f, 3) < 2) continue;
      if (numwords >= cap)
      {
        cap = cap * 2 + 256;
        wfrom = realloc(wfrom, (size_t)cap * 40);
        wto = realloc(wto, (size_t)cap * 40);
      }
      snprintf(wfrom[numwords], 40, "%s", f[0]);
      snprintf(wto[numwords], 40, "%s", f[1]);
      numwords++;
    }
    free(t);
  }
  out[0] = 0;
  for (;;)
  {
    if ((*p == ' ') || (*p == '-') || (!*p))
    {
      const char *rep = NULL;
      part[n] = 0;
      for (k = 0; k < numwords; k++)
        if (!strcmp(wfrom[k], part)) { rep = wto[k]; break; }
      o += snprintf(out + o, o < size ? size - o : 0, "%s", rep ? rep : part);
      n = 0;
      if (!*p) break;
      o += snprintf(out + o, o < size ? size - o : 0, "%c", *p);
      p++;
      continue;
    }
    if (n < 63) part[n++] = *p;
    p++;
  }
  {
    char *x = strstr(out, "Kick Deep 909");
    if (x) memcpy(x, "Kick 909 Deep", 13);
  }
}

void sg_en(const char *name, char *out, int size)
{
  int c;
  sg_init();
  if (!strncmp(name, "Echo ", 5))
  {
    char rest[64];
    sg_en(name + 5, rest, sizeof rest);
    snprintf(out, size, "Echo %s", rest);
    return;
  }
  for (c = 0; c < numins; c++)
    if (!strcmp(ins[c].name, name))
    {
      snprintf(out, size, "%s", ins[c].en);
      return;
    }
  sg_translate(name, out, size);
}

void sg_echoof(const SGINS *src, SGINS *o)
{
  int s = (src->sr >> 4) - 6;
  char nm[64];
  if (s < 3) s = 3;
  *o = *src;
  snprintf(nm, sizeof nm, "Echo %s", src->name);
  nm[15] = 0;
  snprintf(o->name, sizeof o->name, "%.47s", nm);
  sg_en(o->name, nm, sizeof nm);
  snprintf(o->en, sizeof o->en, "%.47s", nm);
  o->sr = (s << 4) | (src->sr & 0x0f);
  o->ad = src->ad | 0x01;
  o->filt[0] = 0;
  snprintf(o->cat, sizeof o->cat, "echo");
  o->derived = 1;
}

void sg_octavebass(const SGINS *src, SGINS *o)
{
  char nm[64];
  int l;
  *o = *src;
  snprintf(nm, sizeof nm, "%.11s +8", src->name);
  // .strip()
  l = strlen(nm);
  while ((l > 0) && (nm[l - 1] == ' ')) nm[--l] = 0;
  {
    char *p = nm;
    while (*p == ' ') p++;
    snprintf(o->name, sizeof o->name, "%.47s", p);
  }
  sg_translate(o->name, nm, sizeof nm);
  snprintf(o->en, sizeof o->en, "%.47s", nm);
  snprintf(o->cat, sizeof o->cat, "lead");
  snprintf(o->fam, sizeof o->fam, "Synth");
  o->filt[0] = 0;
  o->derived = 1;
}

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

void tr_init(SGTRACK *t, int rows)
{
  t->rows = rows;
  t->ev = (SGEV *)calloc(rows + 1, sizeof(SGEV));
  t->hascmds = (unsigned char *)calloc(rows + 1, 1);
  t->cmds = (SGCMD *)calloc(rows + 1, sizeof(SGCMD));
  t->lv = (double *)calloc(rows + 1, sizeof(double));
  t->haslv = (unsigned char *)calloc(rows + 1, 1);
}

void tr_free(SGTRACK *t)
{
  free(t->ev);
  free(t->hascmds);
  free(t->cmds);
  free(t->lv);
  free(t->haslv);
  t->lv = NULL;
  t->haslv = NULL;
  t->ev = NULL;
  t->hascmds = NULL;
  t->cmds = NULL;
  t->rows = 0;
}

void tr_grow(SGTRACK *t, int rows)
{
  if (rows <= t->rows) return;
  t->ev = (SGEV *)realloc(t->ev, sizeof(SGEV) * (rows + 1));
  t->hascmds = (unsigned char *)realloc(t->hascmds, rows + 1);
  t->cmds = (SGCMD *)realloc(t->cmds, sizeof(SGCMD) * (rows + 1));
  memset(t->ev + t->rows, 0, sizeof(SGEV) * (rows + 1 - t->rows));
  memset(t->hascmds + t->rows, 0, rows + 1 - t->rows);
  memset(t->cmds + t->rows, 0, sizeof(SGCMD) * (rows + 1 - t->rows));
  t->lv = (double *)realloc(t->lv, sizeof(double) * (rows + 1));
  t->haslv = (unsigned char *)realloc(t->haslv, rows + 1);
  memset(t->lv + t->rows, 0, sizeof(double) * (rows + 1 - t->rows));
  memset(t->haslv + t->rows, 0, rows + 1 - t->rows);
  t->rows = rows;
}

unsigned sg_evord = 0;     // insertion order of events (Python dicts keep it)

void tr_note(SGTRACK *t, int row, int note, int ins_, int dur)
{
  SGEV *e;
  if (row < 0) return;
  if (row >= t->rows) tr_grow(t, row + 64);
  while (note > 92) note -= 12;
  while (note < 0) note += 12;
  e = &t->ev[row];
  {
    unsigned o = e->type != EV_NONE ? e->ord : ++sg_evord;
    memset(e, 0, sizeof *e);
    e->ord = o;
  }
  e->type = EV_NOTE;
  e->note = note;
  e->ins = ins_;
  e->dur = dur;
}

void tr_notecmd(SGTRACK *t, int row, int note, int ins_, int dur, const SGCMD *c)
{
  tr_note(t, row, note, ins_, dur);
  if ((row >= 0) && (c))
  {
    t->ev[row].hascmd = 1;
    t->ev[row].cmd = *c;
  }
}

void tr_off(SGTRACK *t, int row)
{
  if (row < 0) return;
  if (row >= t->rows) tr_grow(t, row + 64);
  if (t->ev[row].type == EV_NONE)
  {
    t->ev[row].type = EV_OFF;
    t->ev[row].ord = ++sg_evord;
  }
}

static unsigned char *busymap(const SGTRACK *t, int drumlen, int extra)
{
  unsigned char *b = (unsigned char *)calloc(t->rows + extra + 256, 1);
  int r;
  for (r = 0; r < t->rows; r++)
  {
    const SGEV *e = &t->ev[r];
    if (e->type == EV_NOTE)
    {
      int d = e->dur > 0 ? e->dur : drumlen, k;
      for (k = r; (k < r + d) && (k < t->rows + extra + 256); k++) b[k] = 1;
    }
  }
  return b;
}

int tr_busy(const SGTRACK *t, int row, int drumlen)
{
  int r;
  for (r = row; r >= 0 && r > row - 256; r--)
  {
    const SGEV *e = &t->ev[r];
    if ((r < t->rows) && (e->type == EV_NOTE))
    {
      int d = e->dur > 0 ? e->dur : drumlen;
      if (r + d > row) return 1;
    }
  }
  return 0;
}

void tr_merge_low(SGTRACK *t, const SGTRACK *low, int guard, int drumlen)
{
  unsigned char *busy = busymap(t, drumlen, low->rows);
  int r;
  if (low->rows > t->rows) tr_grow(t, low->rows);
  for (r = 0; r < low->rows; r++)
  {
    const SGEV *e = &low->ev[r];
    int k, d, blocked = 0;
    if (e->type != EV_NOTE) continue;
    for (k = r; k < r + guard; k++)
      if (busy[k]) blocked = 1;
    if (blocked) continue;
    d = e->dur > 0 ? e->dur : 1;
    k = r;
    while ((k < r + d) && (!busy[k])) k++;
    {
      unsigned o = t->ev[r].type != EV_NONE ? t->ev[r].ord : ++sg_evord;
      t->ev[r] = *e;
      t->ev[r].ord = o;
    }
    t->ev[r].dur = k - r;
  }
  free(busy);
}

void tr_finalize(SGTRACK *t, int total)
{
  int r;
  if (total > t->rows) tr_grow(t, total);
  for (r = 0; r < t->rows; r++)
  {
    SGEV *e = &t->ev[r];
    int end, nxt;
    if ((e->type != EV_NOTE) || (e->dur < 0)) continue;
    end = r + e->dur;
    nxt = r + 1;
    while ((nxt < t->rows) && (t->ev[nxt].type == EV_NONE)) nxt++;
    if (nxt >= t->rows) nxt = total;
    if ((end < nxt) && (end < total))
    {
      if (t->ev[end].type == EV_NONE) t->ev[end].ord = ++sg_evord;
      t->ev[end].type = EV_OFF;
    }
  }
}

int sg_handcount(void)
{
  int k, n = 0;
  sg_init();
  for (k = 0; k < numins; k++)
    if (!ins[k].model) n++;
  return n;
}

const SGINS *sg_hand(int k) { return &ins[k]; }     // the hand-made ones come first
int sg_arpcount(void) { sg_init(); return numarps; }
const SGARP *sg_arpat(int k) { return &arps[k]; }
int sg_arpindex(const SGARP *a) { return (int)(a - arps); }

void sg_addfilter(const char *name, const int *l, const int *r, int n)
{
  SGFILTER *f;
  sg_init();
  if (sg_findfilter(name)) return;
  filters = (SGFILTER *)realloc(filters, sizeof(SGFILTER) * (numfilters + 1));
  f = &filters[numfilters++];
  memset(f, 0, sizeof *f);
  snprintf(f->name, sizeof f->name, "%s", name);
  if (n > 64) n = 64;
  memcpy(f->l, l, sizeof(int) * n);
  memcpy(f->r, r, sizeof(int) * n);
  f->n = n;
}

void tr_note_lv(SGTRACK *t, int row, int note, int ins_, int dur, int haslv, double lv)
{
  SGEV *e;
  if (row < 0) return;
  if (row >= t->rows) tr_grow(t, row + 64);
  e = &t->ev[row];
  {
    unsigned o = e->type != EV_NONE ? e->ord : ++sg_evord;
    memset(e, 0, sizeof *e);
    e->ord = o;
  }
  e->type = EV_NOTE;
  e->note = note;
  e->ins = ins_;
  e->dur = dur;
  t->haslv[row] = haslv;
  t->lv[row] = haslv ? lv : 0;
}

int tr_sounding(const SGTRACK *t, int row, int drumlen)
{
  int r;
  for (r = row; r > row - 200 && r >= 0; r--)
  {
    const SGEV *e;
    if (r >= t->rows) continue;
    e = &t->ev[r];
    if (e->type == EV_NOTE) return r + (e->dur > 0 ? e->dur : drumlen) > row;
    if (e->type != EV_NONE) return 0;
  }
  return 0;
}

// merge_low of the MIDI importer: busy grows with every placed note; only rows r0..r1-1 (r1 < 0: all)
int tr_merge_low_imp(SGTRACK *t, const SGTRACK *low, int guard, int min_len, int drumlen, int r0, int r1)
{
  unsigned char *busy = busymap(t, drumlen, low->rows);
  int r, placed = 0, cap = t->rows + low->rows + 256;
  if (low->rows > t->rows) tr_grow(t, low->rows);
  for (r = 0; r < low->rows; r++)
  {
    const SGEV *e = &low->ev[r];
    int k, d, blocked = 0;
    if ((r1 >= 0) && ((r < r0) || (r >= r1))) continue;
    if (e->type != EV_NOTE) continue;
    for (k = r; k < r + guard; k++)
      if ((k < cap) && (busy[k])) blocked = 1;
    if (blocked) continue;
    d = e->dur > 0 ? e->dur : 1;
    k = r;
    while ((k < r + d) && (k < cap) && (!busy[k])) k++;
    if (k - r >= min_len)
    {
      int j;
      unsigned o = t->ev[r].type != EV_NONE ? t->ev[r].ord : ++sg_evord;
      memset(&t->ev[r], 0, sizeof(SGEV));
      t->ev[r].ord = o;
      t->ev[r].type = EV_NOTE;
      t->ev[r].note = e->note;
      t->ev[r].ins = e->ins;
      t->ev[r].dur = k - r;
      t->haslv[r] = low->haslv[r];
      t->lv[r] = low->lv[r];
      for (j = r; j < k; j++) busy[j] = 1;
      placed++;
    }
  }
  free(busy);
  return placed;
}

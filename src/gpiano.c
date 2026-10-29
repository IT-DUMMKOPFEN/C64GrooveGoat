//
// C64 GrooveGoat - piano keyboard visualisation
//
// Shows the notes that are playing on all voices over 8 octaves. Used by the
// editor (full colour canvas above the status bar) and by the MP4 video
// export.
//

#include <string.h>
#include "goattrk2.h"
#include "gpiano.h"

// colours of the voices as in the oscilloscopes of the video export
const PIANOCOLORS piano_night = {
  0x0b0826, 0xeeecf8, 0xc9c4e6, 0x1a1540, 0x3a3270, 0x2a2170, 0x6c5eb5,
  {0xff7a68, 0xffd84a, 0x5fd3ff}
};

const PIANOCOLORS piano_day = {
  0xc4bee6, 0xffffff, 0xe2def2, 0x2a2166, 0x51479a, 0x9c94cc, 0x8a84b0,
  {0xd23c2c, 0xb88a00, 0x0a7db0}
};

// white key index (0-6) of the semitones, -1 = black key
static const int whiteofsemi[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
// black keys: the white key they follow
static const int blackafter[12] = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1};

void piano_getstate(unsigned keyvoices[PIANO_KEYS])
{
  int c;
  memset(keyvoices, 0, PIANO_KEYS * sizeof(unsigned));
  for (c = 0; c < numchannels; c++)
  {
    int note;
    if (chn[c].mute) continue;
    // gate on in the playroutine and in the SID register (a stopped song
    // keeps the gate flag but silences the voice with waveform 0)
    if (!(chn[c].gate & 1)) continue;
    if (!(sidreg[c / 3][4 + 7 * (c % 3)] & 1)) continue;
    note = chn[c].note;
    if ((note >= 0) && (note < PIANO_KEYS)) keyvoices[note] |= 1u << c;
  }
}

static inline unsigned mixrgb(unsigned a, unsigned b, int t)  // t = 0..256 (b)
{
  unsigned r = (((a >> 16) & 0xff) * (256 - t) + ((b >> 16) & 0xff) * t) >> 8;
  unsigned g = (((a >> 8) & 0xff) * (256 - t) + ((b >> 8) & 0xff) * t) >> 8;
  unsigned bl = ((a & 0xff) * (256 - t) + (b & 0xff) * t) >> 8;
  return (r << 16) | (g << 8) | bl;
}

static void fill(unsigned *buf, int stride, int w, int h, int x0, int y0, int x1, int y1, unsigned rgb)
{
  int x, y;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > w) x1 = w;
  if (y1 > h) y1 = h;
  for (y = y0; y < y1; y++)
    for (x = x0; x < x1; x++)
      buf[y * stride + x] = rgb;
}

// Fill a key area with the colours of the voices that play it (vertical
// stripes when several voices play the same note)
static void fillvoices(unsigned *buf, int stride, int w, int h, int x0, int y0, int x1, int y1,
  unsigned voices, const PIANOCOLORS *col, int dark)
{
  int n = 0, i = 0, c;
  for (c = 0; c < 32; c++) if (voices & (1u << c)) n++;
  for (c = 0; c < 32; c++)
  {
    if (voices & (1u << c))
    {
      int sx0 = x0 + (x1 - x0) * i / n;
      int sx1 = x0 + (x1 - x0) * (i + 1) / n;
      unsigned vc = col->voice[c % 3];
      if (dark) vc = mixrgb(vc, 0x000000, 64);
      fill(buf, stride, w, h, sx0, y0, sx1, y1, vc);
      i++;
    }
  }
}

static int whitex(int k, int w)
{
  return k * w / PIANO_WHITEKEYS;
}

static void blackrect(int note, int w, int h, int *x0, int *x1, int *y1)
{
  int oct = note / 12, semi = note % 12;
  int boundary = whitex(oct * 7 + blackafter[semi] + 1, w);
  int ww = w / PIANO_WHITEKEYS;
  int bw = ww * 60 / 100;
  if (bw < 3) bw = 3;
  *x0 = boundary - bw / 2;
  *x1 = *x0 + bw;
  *y1 = h * 58 / 100;
}

void piano_render(unsigned *buf, int stride, int w, int h, const unsigned keyvoices[PIANO_KEYS],
  const PIANOCOLORS *col, const PIANOFONT *font)
{
  int note, k, x, y;
  int gap = w >= 1600 ? 2 : 1;
  int shade = h / 14 > 1 ? h / 14 : 2;

  fill(buf, stride, w, h, 0, 0, w, h, col->edge);

  // white keys
  for (k = 0; k < PIANO_WHITEKEYS; k++)
  {
    int x0 = whitex(k, w);
    int x1 = whitex(k + 1, w) - (k < PIANO_WHITEKEYS - 1 ? gap : 0);
    int semi = 0, i;
    for (i = 0; i < 12; i++) if (whiteofsemi[i] == k % 7) semi = i;
    note = (k / 7) * 12 + semi;
    if (keyvoices[note])
    {
      fillvoices(buf, stride, w, h, x0, 0, x1, h - shade, keyvoices[note], col, 0);
      fillvoices(buf, stride, w, h, x0, h - shade, x1, h, keyvoices[note], col, 1);
    }
    else
    {
      fill(buf, stride, w, h, x0, 0, x1, h - shade, col->white);
      fill(buf, stride, w, h, x0, h - shade, x1, h, col->whiteshade);
    }
    // octave label on the C keys
    if ((semi == 0) && (font) && (font->data) && (x1 - x0 >= font->w * 2))
    {
      char label[4];
      int lx, ly, n;
      label[0] = 'C';
      label[1] = '0' + k / 7;
      label[2] = 0;
      lx = x0 + ((x1 - x0) - font->w * 2) / 2;
      ly = h - shade - font->h - h / 48;
      if (ly >= h * 58 / 100)
      {
        for (n = 0; n < 2; n++)
        {
          const unsigned char *g = font->data + (unsigned char)label[n] * font->w * font->h;
          for (y = 0; y < font->h; y++)
          {
            for (x = 0; x < font->w; x++)
            {
              int a = g[y * font->w + x];
              int px = lx + n * font->w + x, py = ly + y;
              if ((a) && (px >= 0) && (px < w) && (py >= 0) && (py < h))
              {
                unsigned base = buf[py * stride + px];
                unsigned lc = keyvoices[note] ? mixrgb(base, 0x000000, 140) : col->label;
                buf[py * stride + px] = mixrgb(base, lc, a);
              }
            }
          }
        }
      }
    }
  }

  // black keys
  for (note = 0; note < PIANO_KEYS; note++)
  {
    int x0, x1, y1;
    if (blackafter[note % 12] < 0) continue;
    blackrect(note, w, h, &x0, &x1, &y1);
    if (keyvoices[note])
      fillvoices(buf, stride, w, h, x0, 0, x1, y1, keyvoices[note], col, 1);
    else
    {
      fill(buf, stride, w, h, x0, 0, x1, y1, col->black);
      // a little highlight at the front edge
      fill(buf, stride, w, h, x0 + (x1 - x0) / 5, y1 - shade - 1, x1 - (x1 - x0) / 5, y1 - 1, col->blackhi);
    }
  }
}

int piano_keyat(int px, int py, int w, int h)
{
  int note, k;
  if ((px < 0) || (py < 0) || (px >= w) || (py >= h)) return -1;
  for (note = 0; note < PIANO_KEYS; note++)
  {
    int x0, x1, y1;
    if (blackafter[note % 12] < 0) continue;
    blackrect(note, w, h, &x0, &x1, &y1);
    if ((px >= x0) && (px < x1) && (py < y1)) return note;
  }
  k = px * PIANO_WHITEKEYS / w;
  if (k >= PIANO_WHITEKEYS) k = PIANO_WHITEKEYS - 1;
  {
    int semi, i;
    semi = 0;
    for (i = 0; i < 12; i++) if (whiteofsemi[i] == k % 7) semi = i;
    return (k / 7) * 12 + semi;
  }
}

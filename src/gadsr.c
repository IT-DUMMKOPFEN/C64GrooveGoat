//
// C64 GrooveGoat - graphical ADSR envelope
//

#include "goattrk2.h"
#include "gadsr.h"

// SID envelope times in milliseconds (from the 6581 datasheet)
static const float attacktime[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
static const float decaytime[16] = {6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000};

float adsr_attackms(int a) { return attacktime[a & 15]; }
float adsr_decayms(int d) { return decaytime[d & 15]; }

void adsr_timestr(char *buf, float ms)
{
  if (ms < 1000) sprintf(buf, "%d ms", (int)ms);
  else if (ms < 10000)
  {
    if ((int)ms % 1000) sprintf(buf, "%.1f s", ms / 1000.0f);
    else sprintf(buf, "%d s", (int)(ms / 1000));
  }
  else sprintf(buf, "%d s", (int)(ms / 1000));
}

static inline void plot(unsigned char *buf, int w, int h, int x, int y, int a)
{
  unsigned char *p;
  if ((x < 0) || (y < 0) || (x >= w) || (y >= h) || (a <= 0)) return;
  if (a > 255) a = 255;
  p = buf + y * CANVAS_STRIDE + x;
  if (*p < a) *p = a;
}

// Envelope level 0..1 at horizontal position x
typedef struct
{
  int x0, xa, xd, xs, xr;
  float sustain;
} ADSRSHAPE;

static float level(const ADSRSHAPE *s, float x)
{
  if (x < s->x0) return 0;
  if (x < s->xa) return (x - s->x0) / (float)(s->xa - s->x0);
  if (x < s->xd)
  {
    float t = (x - s->xa) / (float)(s->xd - s->xa);
    return s->sustain + (1 - s->sustain) * (expf(-4.5f * t) - expf(-4.5f) * t);
  }
  if (x < s->xs) return s->sustain;
  if (x < s->xr)
  {
    float t = (x - s->xs) / (float)(s->xr - s->xs);
    return s->sustain * (expf(-4.5f * t) - expf(-4.5f) * t);
  }
  return 0;
}

void drawadsrgraph(int id, int x, int y, int cw, int ch, int ad, int sr, int color)
{
  unsigned char *buf = canvas_begin(id, cw, ch);
  int w = cw * 16, h = ch * 32;
  int top = 6, base = h - 5;
  int a = ad >> 4, d = ad & 15, s = sr >> 4, r = sr & 15;
  int hold = w * 14 / 100;
  float unit = (w - 12 - 3 * 8 - hold) / 45.0f;
  ADSRSHAPE sh;
  int px;
  float prevy = (float)base;

  if (!buf) return;
  sh.x0 = 6;
  sh.xa = sh.x0 + 8 + (int)(a * unit);
  sh.xd = sh.xa + 8 + (int)(d * unit);
  sh.xs = sh.xd + hold;
  sh.xr = sh.xs + 8 + (int)(r * unit);
  sh.sustain = s / 15.0f;

  // baseline and the peak level as faint guides
  for (px = 2; px < w - 2; px++)
  {
    plot(buf, w, h, px, base + 1, 70);
    if ((px & 7) < 3) plot(buf, w, h, px, top, 45);
  }
  // gate off (start of release): dotted line
  for (px = top; px < base; px++)
    if ((px & 7) < 4) plot(buf, w, h, sh.xs, px, 110);

  for (px = 0; px < w; px++)
  {
    float l = level(&sh, (float)px);
    float fy = base - l * (base - top);
    int y0 = (int)floorf(fy < prevy ? fy : prevy);
    int y1 = (int)ceilf(fy > prevy ? fy : prevy);
    int py;
    if ((px >= sh.x0) && (px <= sh.xr))
    {
      // filled area with a vertical fade
      for (py = (int)fy; py <= base; py++)
        plot(buf, w, h, px, py, 30 + 50 * (base - py) / (base - top + 1));
      // the curve (about 3 pixels thick)
      for (py = y0 - 2; py <= y1 + 2; py++)
      {
        int dist = py < y0 ? y0 - py : (py > y1 ? py - y1 : 0);
        plot(buf, w, h, px, py, dist == 0 ? 255 : (dist == 1 ? 200 : 70));
      }
    }
    prevy = fy;
  }
  canvas_draw(id, x, y, color);
}

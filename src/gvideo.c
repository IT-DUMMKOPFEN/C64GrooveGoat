//
// C64 GrooveGoat - MP4 video export
//
// Renders a 1280x720 oscilloscope view of all SID voices, encodes it with
// minih264 (H.264 baseline) and the audio with faac (AAC-LC) and muxes both
// with minimp4 into an MP4 file.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <SDL.h>

#include "gcommon.h"
#include "gsid.h"
#include "gres.h"
#include "gvideo.h"
#include "goattrk2.h"

#define H264E_MAX_THREADS 4
#include "codecs/minih264/minih264e.h"
#include "codecs/minimp4/minimp4.h"
#include "codecs/faac/faac.h"

#define HIST 16384             // oscilloscope history per voice (power of 2)
#define SCOPEWIN 1100          // displayed samples (about 23 ms at 48 kHz)
#define SCOPESEARCH 1100       // trigger search range
#define MAXVOICES (MAX_SIDS * 3)

#define HEADER_H 112
#define AREA_X 16
#define AREA_Y 132
#define AREA_W (VIDEO_W - 32)
#define AREA_H (VIDEO_H - AREA_Y - 14)

typedef struct
{
  int x, y, w, h;
} RECT;

typedef struct
{
  SDL_Thread *thread;
  SDL_sem *start;
  SDL_sem *done;
  void (*callback)(void *);
  void *job;
  int terminated;
} H264THREAD;

struct GGVIDEO
{
  VIDEOPARAMS p;
  char filename[1024];
  char title[64];
  char author[64];
  char info[128];
  FILE *fh;
  // video
  unsigned char *bg;       // static background (RGB)
  unsigned char *frame;    // current frame (RGB)
  unsigned char *yuv;      // YUV 4:2:0
  H264E_persist_t *enc;
  H264E_scratch_t *scratch;
  H264THREAD threads[H264E_MAX_THREADS];
  int numthreads;
  MP4E_mux_t *mux;
  mp4_h26x_writer_t writer;
  int framesdone;
  // audio
  faac_encoder *aac;
  int aactrack;
  int aacframe;            // samples per channel per AAC frame
  short *aacbuf;
  int aacfill;             // sample frames in aacbuf
  unsigned char *aacout;
  int aacoutsize;
  int64_t audioframes;     // audio sample frames received
  // oscilloscope
  short *hist;             // [voice][HIST]
  int64_t scopewritten;    // samples written per voice
  RECT panel[MAXVOICES];
  int error;
  char errortext[160];
};

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

typedef struct
{
  unsigned bgtop, bgbottom, header, panel, border, title, author, info, dim, grid;
  unsigned voice[3];
  int glow;
} VTHEME;

static const VTHEME nighttheme = {
  0x2c2274, 0x130e3a, 0x352879, 0x110c34, 0x4b3ea3, 0xffffff, 0xbcb6ec, 0x948adf, 0x6c5eb5, 0x2a2170,
  {0xff7a68, 0xffd84a, 0x5fd3ff}, 1
};

static const VTHEME daytheme = {
  0xf4f2fc, 0xdcd7f0, 0x352879, 0xfbfaff, 0xc4bee6, 0xffffff, 0xdcd7f5, 0xb4aaf0, 0x8a84b0, 0xe6e2f6,
  {0xd23c2c, 0xb88a00, 0x0a7db0}, 0
};

static const unsigned rainbow[5] = {0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef};

static const VTHEME *vt = &nighttheme;

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static inline void blendpx(unsigned char *buf, int x, int y, unsigned rgb, int a)
{
  unsigned char *p;
  if ((x < 0) || (y < 0) || (x >= VIDEO_W) || (y >= VIDEO_H) || (a <= 0)) return;
  if (a > 255) a = 255;
  p = buf + (y * VIDEO_W + x) * 3;
  p[0] += (((int)((rgb >> 16) & 0xff) - p[0]) * a) / 255;
  p[1] += (((int)((rgb >> 8) & 0xff) - p[1]) * a) / 255;
  p[2] += (((int)(rgb & 0xff) - p[2]) * a) / 255;
}

static void fillrect(unsigned char *buf, int x, int y, int w, int h, unsigned rgb, int a)
{
  int i, j;
  for (j = y; j < y + h; j++)
    for (i = x; i < x + w; i++)
      blendpx(buf, i, j, rgb, a);
}

static void roundrect(unsigned char *buf, int x, int y, int w, int h, int r, unsigned rgb, int a)
{
  int i, j;
  for (j = 0; j < h; j++)
  {
    for (i = 0; i < w; i++)
    {
      int dx = 0, dy = 0;
      if (i < r) dx = r - i;
      if (i >= w - r) dx = i - (w - r - 1);
      if (j < r) dy = r - j;
      if (j >= h - r) dy = j - (h - r - 1);
      if ((dx) && (dy))
      {
        float d = sqrtf((float)(dx * dx + dy * dy)) - r;
        if (d >= 0.5f) continue;
        if (d > -0.5f)
        {
          blendpx(buf, x + i, y + j, rgb, (int)(a * (0.5f - d)));
          continue;
        }
      }
      blendpx(buf, x + i, y + j, rgb, a);
    }
  }
}

static void roundframe(unsigned char *buf, int x, int y, int w, int h, int r, unsigned rgb, int a)
{
  int i, j;
  for (j = 0; j < h; j++)
  {
    for (i = 0; i < w; i++)
    {
      float d;
      int dx = 0, dy = 0;
      if (i < r) dx = r - i;
      if (i >= w - r) dx = i - (w - r - 1);
      if (j < r) dy = r - j;
      if (j >= h - r) dy = j - (h - r - 1);
      if ((dx) && (dy))
        d = r - sqrtf((float)(dx * dx + dy * dy));
      else
      {
        int e = i;
        if (w - 1 - i < e) e = w - 1 - i;
        if (j < e) e = j;
        if (h - 1 - j < e) e = h - 1 - j;
        d = (float)e;
      }
      // 1 pixel outline
      if ((d > -0.5f) && (d < 1.5f))
      {
        float cov = d < 0.5f ? d + 0.5f : 1.5f - d;
        if (cov > 1.0f) cov = 1.0f;
        blendpx(buf, x + i, y + j, rgb, (int)(a * cov));
      }
    }
  }
}

// Text from the 16x32 font atlas (bilinear scaling) or the 8x16 atlas for
// 16 pixel high text. Returns the width in pixels.
static int drawtext(unsigned char *buf, int x, int y, int h, unsigned rgb, int a, const char *text)
{
  int cw = h / 2;
  int n = 0;
  if (h == 16)
  {
    for (; *text; text++, n++)
    {
      const unsigned char *g = res_font1 + (unsigned char)*text * 8 * 16;
      int i, j;
      for (j = 0; j < 16; j++)
        for (i = 0; i < 8; i++)
          if (g[j * 8 + i]) blendpx(buf, x + n * 8 + i, y + j, rgb, g[j * 8 + i] * a / 255);
    }
    return n * 8;
  }
  for (; *text; text++, n++)
  {
    const unsigned char *g = res_font2 + (unsigned char)*text * 16 * 32;
    int i, j;
    for (j = 0; j < h; j++)
    {
      float fy = (j + 0.5f) * 32.0f / h - 0.5f;
      int y0 = (int)floorf(fy);
      float wy = fy - y0;
      for (i = 0; i < cw; i++)
      {
        float fx = (i + 0.5f) * 16.0f / cw - 0.5f;
        int x0 = (int)floorf(fx);
        float wx = fx - x0;
        float v = 0;
        int k, l;
        for (k = 0; k < 2; k++)
        {
          for (l = 0; l < 2; l++)
          {
            int sx = x0 + l, sy = y0 + k;
            float wgt = (l ? wx : 1 - wx) * (k ? wy : 1 - wy);
            if ((sx < 0) || (sy < 0) || (sx >= 16) || (sy >= 32)) continue;
            v += g[sy * 16 + sx] * wgt;
          }
        }
        if (v > 0.5f) blendpx(buf, x + n * cw + i, y + j, rgb, (int)(v * a / 255));
      }
    }
  }
  return n * cw;
}

static int textwidth(int h, const char *text)
{
  return (int)strlen(text) * (h / 2);
}

static void drawimage_rgba(unsigned char *buf, int x, int y, const char *name)
{
  const RESIMAGE *im = res_getimage(name);
  int i, j;
  if (!im) return;
  for (j = 0; j < im->h; j++)
  {
    for (i = 0; i < im->w; i++)
    {
      const unsigned char *s = im->rgba + (j * im->w + i) * 4;
      blendpx(buf, x + i, y + j, (s[0] << 16) | (s[1] << 8) | s[2], s[3]);
    }
  }
}

static void fittext(char *dest, const char *src, int maxchars)
{
  int n = (int)strlen(src);
  if (n <= maxchars)
  {
    strcpy(dest, src);
    return;
  }
  if (maxchars < 4) maxchars = 4;
  memcpy(dest, src, maxchars - 3);
  strcpy(dest + maxchars - 3, "...");
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static void layout(GGVIDEO *v)
{
  int n = v->p.nsids;
  int rows, cols, s, c;
  int gap = 10;

  if (n <= 1) { rows = 3; cols = 1; }
  else if (n == 2) { rows = 3; cols = 2; }
  else if (n <= 4) { rows = n; cols = 3; }
  else { rows = 4; cols = 6; }

  for (s = 0; s < n; s++)
  {
    for (c = 0; c < 3; c++)
    {
      int row, col, x, y, w, h;
      int extra = 0;
      if (n == 1) { row = c; col = 0; }
      else if (n == 2) { row = c; col = s; }
      else if (n <= 4) { row = s; col = c; }
      else { row = s / 2; col = (s & 1) * 3 + c; extra = (s & 1) ? 1 : 0; }
      w = (AREA_W - (cols - 1) * gap - (n > 4 ? 12 : 0)) / cols;
      h = (AREA_H - (rows - 1) * gap) / rows;
      x = AREA_X + col * (w + gap) + extra * 12;
      y = AREA_Y + row * (h + gap);
      v->panel[s * 3 + c].x = x;
      v->panel[s * 3 + c].y = y;
      v->panel[s * 3 + c].w = w;
      v->panel[s * 3 + c].h = h;
    }
  }
}

static void timestr(char *buf, double sec)
{
  int t = (int)sec;
  if (t < 0) t = 0;
  sprintf(buf, "%d:%02d", t / 60, t % 60);
}

static void buildbackground(GGVIDEO *v)
{
  unsigned char *b = v->bg;
  int x, y, i;
  char buf[160];

  // vertical gradient
  for (y = 0; y < VIDEO_H; y++)
  {
    float t = (float)y / (VIDEO_H - 1);
    unsigned r = (unsigned)(((vt->bgtop >> 16) & 0xff) * (1 - t) + ((vt->bgbottom >> 16) & 0xff) * t);
    unsigned g = (unsigned)(((vt->bgtop >> 8) & 0xff) * (1 - t) + ((vt->bgbottom >> 8) & 0xff) * t);
    unsigned bl = (unsigned)((vt->bgtop & 0xff) * (1 - t) + (vt->bgbottom & 0xff) * t);
    for (x = 0; x < VIDEO_W; x++)
    {
      b[(y * VIDEO_W + x) * 3] = r;
      b[(y * VIDEO_W + x) * 3 + 1] = g;
      b[(y * VIDEO_W + x) * 3 + 2] = bl;
    }
  }
  // header
  fillrect(b, 0, 0, VIDEO_W, HEADER_H, vt->header, 255);
  drawimage_rgba(b, 14, 8, "logo96");
  fittext(buf, v->title, 50);
  drawtext(b, 128, 14, 32, vt->title, 255, buf);
  fittext(buf, v->author, 70);
  drawtext(b, 128, 52, 24, vt->author, 255, buf);
  drawtext(b, 128, 84, 16, vt->info, 255, v->info);
  sprintf(buf, "%s %s", PROGRAMNAME, PROGRAMVERSION);
  drawtext(b, VIDEO_W - 20 - textwidth(16, buf), 84, 16, vt->info, 255, buf);
  for (i = 0; i < 5; i++)
    fillrect(b, VIDEO_W - 20 - textwidth(16, buf) - 70 + i * 12, 84, 10, 16, rainbow[i], 255);

  // scope panels
  for (i = 0; i < v->p.nsids * 3; i++)
  {
    RECT *r = &v->panel[i];
    int s = i / 3, c = i % 3;
    unsigned vc = vt->voice[c];
    roundrect(b, r->x, r->y, r->w, r->h, 8, vt->panel, vt->glow ? 170 : 255);
    roundframe(b, r->x, r->y, r->w, r->h, 8, vt->border, 255);
    // centre line
    fillrect(b, r->x + 8, r->y + 24 + (r->h - 30) / 2, r->w - 26, 1, vt->grid, 255);
    if (r->w >= 300)
      sprintf(buf, "SID %d \xb7 Voice %d", s + 1, c + 1);
    else
      sprintf(buf, "S%d \xb7 V%d", s + 1, c + 1);
    drawtext(b, r->x + 10, r->y + 5, 16, vc, 255, buf);
  }
}

// ---------------------------------------------------------------------------
// Oscilloscope
// ---------------------------------------------------------------------------

static inline int histval(GGVIDEO *v, int ch, int64_t pos)
{
  if ((pos < 0) || (pos >= v->scopewritten) || (pos < v->scopewritten - HIST)) return 0;
  return v->hist[ch * HIST + (int)(pos & (HIST - 1))];
}

static void drawscope(GGVIDEO *v, int ch, int64_t centre)
{
  RECT *r = &v->panel[ch];
  unsigned col = vt->voice[ch % 3];
  int px = r->x + 8, pw = r->w - 26;
  int py = r->y + 24, ph = r->h - 30;
  int mid = py + ph / 2;
  float scale = (ph / 2 - 3) / 2048.0f;
  int64_t start = centre - SCOPEWIN / 2;
  int64_t s0 = start - SCOPESEARCH / 2, i;
  int peak = 0, armed = 0, x;
  int prevy = mid;

  // Trigger: first rising zero crossing after the signal was clearly
  // negative, so that periodic waveforms stand still
  for (i = s0 - SCOPESEARCH / 2; i < s0 + SCOPESEARCH; i++)
  {
    int a = histval(v, ch, i);
    if (a < 0) a = -a;
    if (a > peak) peak = a;
  }
  if (peak > 64)
  {
    int thr = peak / 8;
    for (i = s0; i < s0 + SCOPESEARCH; i++)
    {
      int a = histval(v, ch, i);
      if (a < -thr) armed = 1;
      if ((armed) && (a >= 0) && (histval(v, ch, i - 1) < 0))
      {
        start = i;
        break;
      }
    }
  }

  for (x = 0; x < pw; x++)
  {
    float p0 = (float)x * SCOPEWIN / pw;
    float p1 = (float)(x + 1) * SCOPEWIN / pw;
    int64_t i0 = (int64_t)p0, i1 = (int64_t)p1;
    float f0 = p0 - i0;
    float v0 = histval(v, ch, start + i0) * (1 - f0) + histval(v, ch, start + i0 + 1) * f0;
    float mn = v0, mx = v0;
    int y0, y1, y;
    for (i = i0 + 1; i <= i1; i++)
    {
      float s = (float)histval(v, ch, start + i);
      if (s < mn) mn = s;
      if (s > mx) mx = s;
    }
    y0 = mid - (int)(mx * scale);
    y1 = mid - (int)(mn * scale);
    // connect to the previous column
    if (prevy < y0) y0 = prevy;
    if (prevy > y1) y1 = prevy;
    prevy = mid - (int)(((mn + mx) * 0.5f) * scale);
    if (y0 < py) y0 = py;
    if (y1 >= py + ph) y1 = py + ph - 1;
    if (vt->glow)
    {
      for (y = y0 - 4; y <= y1 + 4; y++)
      {
        int d = y < y0 ? y0 - y : (y > y1 ? y - y1 : 0);
        static const int glowa[5] = {0, 70, 45, 25, 10};
        if (d) blendpx(v->frame, px + x, y, col, glowa[d]);
      }
    }
    for (y = y0 - 1; y <= y1 + 1; y++)
      blendpx(v->frame, px + x, y, col, (y < y0 || y > y1) ? 110 : 255);
  }
}

static void drawvoiceinfo(GGVIDEO *v, int ch)
{
  RECT *r = &v->panel[ch];
  int s = ch / 3, c = ch % 3;
  unsigned char *reg = &sidreg[s][c * 7];
  unsigned char ctrl = reg[4];
  unsigned col = vt->voice[c];
  char buf[40];
  int env, h, x;

  buf[0] = 0;
  if (ctrl & 0x08) strcat(buf, "TEST");
  else
  {
    if (ctrl & 0x10) strcat(buf, "TRI");
    if (ctrl & 0x20) strcat(buf, buf[0] ? "+SAW" : "SAW");
    if (ctrl & 0x40)
    {
      if (r->w >= 300)
      {
        char pw[16];
        sprintf(pw, "%sPUL %d%%", buf[0] ? "+" : "", (((reg[3] & 15) << 8) | reg[2]) * 100 / 4096);
        strcat(buf, pw);
      }
      else strcat(buf, buf[0] ? "+PUL" : "PUL");
    }
    if (ctrl & 0x80) strcat(buf, buf[0] ? "+NOI" : "NOISE");
  }
  if ((sidreg[s][0x17] & (1 << c)) && (r->w >= 200)) strcat(buf, r->w >= 300 ? "  FILTER" : " FLT");
  x = r->x + r->w - 20 - textwidth(16, buf);
  if (r->w < 300)
  {
    // short labels only, keep clear of "S1 V1"
    if (x < r->x + 80) x = r->x + 80;
  }
  if ((ctrl & 0xf0) || (ctrl & 0x08)) drawtext(v->frame, x, r->y + 5, 16, vt->dim, 255, buf);

  // gate indicator
  fillrect(v->frame, r->x + r->w - 14, r->y + 9, 6, 6, (ctrl & 1) ? col : vt->grid, 255);

  // envelope level bar
  env = sid_getenv(s, c);
  h = (r->h - 34) * env / 255;
  fillrect(v->frame, r->x + r->w - 12, r->y + r->h - 6 - (r->h - 34), 3, r->h - 34, vt->grid, 255);
  fillrect(v->frame, r->x + r->w - 12, r->y + r->h - 6 - h, 3, h, col, 200);
}

static void renderframe(GGVIDEO *v, int f)
{
  char buf[64], buf2[32];
  double t = (double)f / v->p.fps;
  double total = (double)v->p.totalframes / v->p.fps;
  int64_t centre = (int64_t)((f + 0.5) * v->p.rate / v->p.fps);
  int i, w, prog;

  memcpy(v->frame, v->bg, VIDEO_W * VIDEO_H * 3);

  // time
  timestr(buf, t);
  timestr(buf2, total);
  w = textwidth(32, buf);
  drawtext(v->frame, VIDEO_W - 20 - textwidth(24, buf2) - 16 - w, 14, 32, 0xffffff, 255, buf);
  drawtext(v->frame, VIDEO_W - 20 - textwidth(24, buf2), 20, 24, vt == &daytheme ? 0xdcd7f5 : 0xbcb6ec, 255, buf2);
  fillrect(v->frame, VIDEO_W - 20 - textwidth(24, buf2) - 11, 22, 2, 22, 0x948adf, 255);

  // progress bar in C64 rainbow colours
  prog = v->p.totalframes > 1 ? (int)((double)f * VIDEO_W / (v->p.totalframes - 1)) : VIDEO_W;
  for (i = 0; i < VIDEO_W; i++)
  {
    unsigned c = rainbow[i * 5 / VIDEO_W];
    blendpx(v->frame, i, HEADER_H, c, i < prog ? 255 : 60);
    blendpx(v->frame, i, HEADER_H + 1, c, i < prog ? 255 : 60);
    blendpx(v->frame, i, HEADER_H + 2, c, i < prog ? 255 : 60);
    blendpx(v->frame, i, HEADER_H + 3, c, i < prog ? 160 : 30);
  }

  for (i = 0; i < v->p.nsids * 3; i++)
  {
    drawscope(v, i, centre);
    drawvoiceinfo(v, i);
  }
}

// RGB -> YUV 4:2:0, BT.709 limited range
static void rgb2yuv(GGVIDEO *v)
{
  unsigned char *Y = v->yuv;
  unsigned char *U = Y + VIDEO_W * VIDEO_H;
  unsigned char *V = U + VIDEO_W * VIDEO_H / 4;
  int x, y;
  for (y = 0; y < VIDEO_H; y++)
  {
    const unsigned char *p = v->frame + y * VIDEO_W * 3;
    for (x = 0; x < VIDEO_W; x++, p += 3)
      Y[y * VIDEO_W + x] = (unsigned char)(((16 << 16) + 11966 * p[0] + 40254 * p[1] + 4064 * p[2] + 32768) >> 16);
  }
  for (y = 0; y < VIDEO_H / 2; y++)
  {
    for (x = 0; x < VIDEO_W / 2; x++)
    {
      const unsigned char *p0 = v->frame + ((y * 2) * VIDEO_W + x * 2) * 3;
      const unsigned char *p1 = p0 + VIDEO_W * 3;
      int r = p0[0] + p0[3] + p1[0] + p1[3];
      int g = p0[1] + p0[4] + p1[1] + p1[4];
      int b = p0[2] + p0[5] + p1[2] + p1[5];
      int cb = (-6596 * r - 22189 * g + 28785 * b) / 4;
      int cr = (28785 * r - 26145 * g - 2640 * b) / 4;
      U[y * VIDEO_W / 2 + x] = (unsigned char)((cb + (128 << 16) + 32768) >> 16);
      V[y * VIDEO_W / 2 + x] = (unsigned char)((cr + (128 << 16) + 32768) >> 16);
    }
  }
}

// ---------------------------------------------------------------------------
// Encoder threads (SDL based pool for minih264)
// ---------------------------------------------------------------------------

static int h264threadfunc(void *arg)
{
  H264THREAD *t = (H264THREAD *)arg;
  for (;;)
  {
    SDL_SemWait(t->start);
    if (t->terminated) break;
    t->callback(t->job);
    SDL_SemPost(t->done);
  }
  return 0;
}

static void h264runjobs(void *token, void (*job_func)(void *), void *job_data[], int njobs)
{
  GGVIDEO *v = (GGVIDEO *)token;
  int i;
  for (i = 0; i < njobs; i++)
  {
    v->threads[i].callback = job_func;
    v->threads[i].job = job_data[i];
    SDL_SemPost(v->threads[i].start);
  }
  for (i = 0; i < njobs; i++)
    SDL_SemWait(v->threads[i].done);
}

// ---------------------------------------------------------------------------
// MP4 output
// ---------------------------------------------------------------------------

static int writecallback(int64_t offset, const void *buffer, size_t size, void *token)
{
  GGVIDEO *v = (GGVIDEO *)token;
#ifdef _WIN32
  if (_fseeki64(v->fh, offset, SEEK_SET)) return 1;
#else
  if (fseeko(v->fh, (off_t)offset, SEEK_SET)) return 1;
#endif
  return fwrite(buffer, 1, size, v->fh) != size;
}

static int encodeframe(GGVIDEO *v)
{
  H264E_run_param_t rp;
  H264E_io_yuv_t io;
  unsigned char *coded;
  int size;

  rgb2yuv(v);
  memset(&rp, 0, sizeof rp);
  rp.frame_type = 0;
  rp.encode_speed = 5;
  rp.desired_frame_bytes = 4000000 / 8 / v->p.fps;
  rp.qp_min = 20;
  rp.qp_max = 40;
  io.yuv[0] = v->yuv;
  io.yuv[1] = v->yuv + VIDEO_W * VIDEO_H;
  io.yuv[2] = v->yuv + VIDEO_W * VIDEO_H + VIDEO_W * VIDEO_H / 4;
  io.stride[0] = VIDEO_W;
  io.stride[1] = VIDEO_W / 2;
  io.stride[2] = VIDEO_W / 2;
  {
    int e = H264E_encode(v->enc, v->scratch, &rp, &io, &coded, &size);
    if (e)
    {
      snprintf(v->errortext, sizeof v->errortext, "H.264 encoder error %d at frame %d", e, v->framesdone);
      return 0;
    }
  }
  {
    int e = mp4_h26x_write_nal(&v->writer, coded, size, 90000 / v->p.fps);
    if (e != MP4E_STATUS_OK)
    {
      snprintf(v->errortext, sizeof v->errortext, "MP4 video error %d at frame %d (%d bytes)", e, v->framesdone, size);
      return 0;
    }
  }
  return 1;
}

static int encodeaudioframe(GGVIDEO *v, const short *pcm, int frames)
{
  uint32_t written = 0;
  faac_status st;
  st = faac_encoder_encode(v->aac, pcm, frames * v->p.channels, v->aacout, v->aacoutsize, &written);
  if (st < 0)
  {
    snprintf(v->errortext, sizeof v->errortext, "AAC encoder error: %s", faac_strerror(st));
    return 0;
  }
  if (written)
  {
    int e = MP4E_put_sample(v->mux, v->aactrack, v->aacout, written, v->aacframe, MP4E_SAMPLE_RANDOM_ACCESS);
    if (e != MP4E_STATUS_OK)
    {
      snprintf(v->errortext, sizeof v->errortext, "MP4 audio error %d", e);
      return 0;
    }
  }
  return 1;
}

GGVIDEO *video_open(const VIDEOPARAMS *p, char *error, int errorsize)
{
  GGVIDEO *v = (GGVIDEO *)calloc(1, sizeof(GGVIDEO));
  H264E_create_param_t cp;
  int sp, ss, i;

  error[0] = 0;
  if (!v)
  {
    snprintf(error, errorsize, "Out of memory");
    return NULL;
  }
  v->p = *p;
  vt = p->daytheme ? &daytheme : &nighttheme;
  strncpy(v->filename, p->filename, sizeof v->filename - 1);
  strncpy(v->title, p->title && p->title[0] ? p->title : "Untitled", sizeof v->title - 1);
  strncpy(v->author, p->author ? p->author : "", sizeof v->author - 1);
  strncpy(v->info, p->info ? p->info : "", sizeof v->info - 1);
  v->p.filename = v->filename;
  if (v->p.nsids < 1) v->p.nsids = 1;
  if (v->p.nsids > MAX_SIDS) v->p.nsids = MAX_SIDS;

  v->bg = (unsigned char *)malloc(VIDEO_W * VIDEO_H * 3);
  v->frame = (unsigned char *)malloc(VIDEO_W * VIDEO_H * 3);
  v->yuv = (unsigned char *)malloc(VIDEO_W * VIDEO_H * 3 / 2);
  v->hist = (short *)calloc(MAXVOICES * HIST, sizeof(short));
  if ((!v->bg) || (!v->frame) || (!v->yuv) || (!v->hist))
  {
    snprintf(error, errorsize, "Out of memory");
    video_close(v, 1);
    return NULL;
  }
  layout(v);
  buildbackground(v);

  v->fh = fopen(v->filename, "wb");
  if (!v->fh)
  {
    snprintf(error, errorsize, "Cannot create %s", v->filename);
    video_close(v, 1);
    return NULL;
  }

  // H.264 encoder
  memset(&cp, 0, sizeof cp);
  cp.width = VIDEO_W;
  cp.height = VIDEO_H;
  cp.gop = p->fps * 2;
  cp.vbv_size_bytes = 4000000 / 8;
  cp.const_input_flag = 1;
  cp.enableNEON = 1;
  cp.num_layers = 1;
  {
    int cpus = SDL_GetCPUCount();
    v->numthreads = cpus > 1 ? (cpus > H264E_MAX_THREADS ? H264E_MAX_THREADS : cpus) : 0;
  }
  for (i = 0; i < v->numthreads; i++)
  {
    v->threads[i].start = SDL_CreateSemaphore(0);
    v->threads[i].done = SDL_CreateSemaphore(0);
    // explicit stack size: macOS secondary threads only get 512 KB by default
    v->threads[i].thread = SDL_CreateThreadWithStackSize(h264threadfunc, "h264", 4 * 1024 * 1024, &v->threads[i]);
  }
  cp.max_threads = v->numthreads;
  cp.token = v;
  cp.run_func_in_thread = v->numthreads ? h264runjobs : NULL;
  if (H264E_sizeof(&cp, &sp, &ss))
  {
    snprintf(error, errorsize, "Video encoder: bad parameters");
    video_close(v, 1);
    return NULL;
  }
  v->enc = (H264E_persist_t *)calloc(1, sp);
  v->scratch = (H264E_scratch_t *)calloc(1, ss);
  if ((!v->enc) || (!v->scratch) || (H264E_init(v->enc, &cp)))
  {
    snprintf(error, errorsize, "Video encoder: init failed");
    video_close(v, 1);
    return NULL;
  }

  // MP4 muxer
  v->mux = MP4E_open(0, 0, v, writecallback);
  if ((!v->mux) || (mp4_h26x_write_init(&v->writer, v->mux, VIDEO_W, VIDEO_H, 0) != MP4E_STATUS_OK))
  {
    snprintf(error, errorsize, "MP4 writer: init failed");
    video_close(v, 1);
    return NULL;
  }

  // AAC encoder
  {
    faac_params fp;
    faac_encoder_info info;
    const uint8_t *asc;
    uint32_t asclen;
    MP4E_track_t tr;

    faac_params_init(&fp, sizeof fp);
    fp.sample_rate = p->rate;
    fp.num_channels = p->channels;
    fp.mpeg_version = FAAC_MPEG4;
    fp.object_type = FAAC_OBJ_LOW;
    fp.joint_mode = p->channels > 1 ? FAAC_JOINT_MS : FAAC_JOINT_NONE;
    fp.use_tns = 1;
    fp.bit_rate = 96000;
    fp.output_format = FAAC_STREAM_RAW;
    fp.input_format = FAAC_INPUT_16BIT;
    if (faac_encoder_open(&fp, &v->aac) != FAAC_OK)
    {
      snprintf(error, errorsize, "AAC encoder: init failed");
      video_close(v, 1);
      return NULL;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    faac_encoder_get_info(v->aac, &info);
    v->aacframe = info.frame_samples;
    v->aacoutsize = info.max_output_bytes;
    v->aacout = (unsigned char *)malloc(v->aacoutsize);
    v->aacbuf = (short *)malloc(v->aacframe * p->channels * sizeof(short));
    memset(&tr, 0, sizeof tr);
    tr.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
    strcpy((char *)tr.language, "und");
    tr.track_media_kind = e_audio;
    tr.time_scale = p->rate;
    tr.default_duration = 0;
    tr.u.a.channelcount = p->channels;
    v->aactrack = MP4E_add_track(v->mux, &tr);
    if ((!v->aacout) || (!v->aacbuf) || (v->aactrack < 0) ||
        (faac_encoder_asc(v->aac, &asc, &asclen) != FAAC_OK) ||
        (MP4E_set_dsi(v->mux, v->aactrack, asc, asclen) != MP4E_STATUS_OK))
    {
      snprintf(error, errorsize, "AAC encoder: setup failed");
      video_close(v, 1);
      return NULL;
    }
  }
  {
    char comment[256];
    snprintf(comment, sizeof comment, "%s by %s - made with %s %s", v->title, v->author, PROGRAMNAME, PROGRAMVERSION);
    MP4E_set_text_comment(v->mux, comment);
  }
  return v;
}

int video_addaudio(GGVIDEO *v, const short *pcm, int frames)
{
  int ch = v->p.channels;
  while (frames > 0)
  {
    int n = v->aacframe - v->aacfill;
    if (n > frames) n = frames;
    memcpy(v->aacbuf + v->aacfill * ch, pcm, n * ch * sizeof(short));
    v->aacfill += n;
    pcm += n * ch;
    frames -= n;
    v->audioframes += n;
    if (v->aacfill == v->aacframe)
    {
      if (!encodeaudioframe(v, v->aacbuf, v->aacfill)) v->error = 1;
      v->aacfill = 0;
    }
  }
  return !v->error;
}

void video_addscope(GGVIDEO *v, const short *scope, int stride, int samples)
{
  int s, c, i;
  for (i = 0; i < samples; i++)
  {
    int pos = (int)((v->scopewritten + i) & (HIST - 1));
    for (s = 0; s < v->p.nsids; s++)
      for (c = 0; c < 3; c++)
        v->hist[(s * 3 + c) * HIST + pos] = scope[(s * stride + i) * 3 + c];
  }
  v->scopewritten += samples;
}

int video_update(GGVIDEO *v, int final)
{
  while ((v->framesdone < v->p.totalframes) && (!v->error))
  {
    int64_t need = (int64_t)((v->framesdone + 0.5) * v->p.rate / v->p.fps) + SCOPEWIN / 2 + SCOPESEARCH;
    if ((!final) && (v->scopewritten < need)) break;
    renderframe(v, v->framesdone);
    if (!encodeframe(v)) v->error = 1;
    v->framesdone++;
  }
  return !v->error;
}

int video_framesdone(GGVIDEO *v)
{
  return v->framesdone;
}

int video_close(GGVIDEO *v, int abort)
{
  int ok = 1, i;
  if (!v) return 0;
  if ((v->aac) && (!abort) && (!v->error))
  {
    // remaining samples and encoder flush
    if (v->aacfill) encodeaudioframe(v, v->aacbuf, v->aacfill);
    for (i = 0; i < 16; i++)
    {
      uint32_t written = 0;
      if (faac_encoder_encode(v->aac, NULL, 0, v->aacout, v->aacoutsize, &written) < 0) break;
      if (!written) break;
      MP4E_put_sample(v->mux, v->aactrack, v->aacout, written, v->aacframe, MP4E_SAMPLE_RANDOM_ACCESS);
    }
  }
  if (v->mux)
  {
    if (MP4E_close(v->mux) != MP4E_STATUS_OK) ok = 0;
    mp4_h26x_write_close(&v->writer);
  }
  if (v->aac) faac_encoder_close(&v->aac);
  for (i = 0; i < v->numthreads; i++)
  {
    v->threads[i].terminated = 1;
    SDL_SemPost(v->threads[i].start);
    SDL_WaitThread(v->threads[i].thread, NULL);
    SDL_DestroySemaphore(v->threads[i].start);
    SDL_DestroySemaphore(v->threads[i].done);
  }
  if (v->fh)
  {
    if (fclose(v->fh)) ok = 0;
    if ((abort) || (v->error) || (!ok)) remove(v->filename);
  }
  if (v->error) ok = 0;
  free(v->enc);
  free(v->scratch);
  free(v->bg);
  free(v->frame);
  free(v->yuv);
  free(v->hist);
  free(v->aacbuf);
  free(v->aacout);
  free(v);
  return ok && !abort;
}

const char *video_errortext(GGVIDEO *v)
{
  return v->errortext[0] ? v->errortext : "unknown error";
}

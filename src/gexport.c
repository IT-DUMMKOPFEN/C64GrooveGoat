//
// C64 GrooveGoat - audio & video export
//
// Offline rendering of a subtune with the editor playroutine and the
// emulated SIDs into
//  - WAV (16 bit PCM, stereo or mono, optionally one file per voice)
//  - MP3 (LAME)
//  - MP4 video (1280x720 oscilloscope view of all voices, H.264 + AAC)
//

#define GEXPORT_C

// lame.h first: its MPEG_mode enum uses names that gaudio.h defines as macros
#include "codecs/lame/lame.h"
#include "goattrk2.h"
#include "gres.h"
#include "gexport.h"
#include "gvideo.h"

enum { AV_WAV = 0, AV_MP3, AV_MP4 };

typedef struct
{
  int format;
  int subtune;
  int automatic;    // length from the song (until it loops)
  int seconds;      // custom length
  int fade;         // fade-out in seconds
  int stereo;
  int rate;         // WAV/MP3 sample rate
  int bitrate;      // MP3 kbps
  int stems;        // WAV: also every voice in its own file
  int respectmute;  // leave muted voices muted
  int videotheme;   // 0 night, 1 day
} AVSETTINGS;

extern int initted;
extern unsigned customclockrate;

static AVSETTINGS avs = {AV_WAV, 0, 1, 180, 5, 1, 44100, 320, 0, 0, 0};

static const char *formatnames[] = {"WAV audio", "MP3 audio", "MP4 video (720p)"};
static const char *formatext[] = {".wav", ".mp3", ".mp4"};
static const int fadeoptions[] = {0, 2, 3, 5, 8, 10};
static const int bitrates[] = {128, 160, 192, 256, 320};

static int cancelled;
static Uint32 lastprogress;
static int cli;

// ---------------------------------------------------------------------------
// Offline renderer
// ---------------------------------------------------------------------------

typedef struct
{
  CHN chn[MAX_CHN];
  SIDFILT filt[MAX_SIDS];
  unsigned char reg[MAX_SIDS][NUMSIDREGS];
  int songinit, lastsonginit, psnum, followplay, playframes;
  int timemin, timesec, timeframe;
} PLAYSTATE;

typedef struct
{
  int rate;
  int stereo;
  double spf;          // output samples per playroutine call
  double acc;
  int frame;
  int totalframes;     // playroutine calls
  int fadeframes;
  float gain;
} RENDER;

static PLAYSTATE saved;
static int soundstopped;

static double playfps(void)
{
  double base = editorInfo.ntsc ? 60.0 : 50.0;
  if (editorInfo.multiplier) return base * editorInfo.multiplier;
  return base / 2;
}

// Stop the editor audio and remember the play state
static void render_enter(void)
{
  stopsong();
  if (initted)
  {
    // let the output fade out before the realtime mixer is detached
    sid_setvolume(0);
    SDL_Delay(40);
    sound_uninit();
    soundstopped = 1;
  }
  memcpy(saved.chn, chn, sizeof chn);
  memcpy(saved.filt, sidfilt, sizeof sidfilt);
  memcpy(saved.reg, sidreg, sizeof saved.reg);
  saved.songinit = songinit;
  saved.lastsonginit = lastsonginit;
  saved.psnum = psnum;
  saved.followplay = followplay;
  saved.playframes = playframes;
  saved.timemin = timemin;
  saved.timesec = timesec;
  saved.timeframe = timeframe;
}

static void render_leave(void)
{
  sid_setscope(NULL, 0);
  sid_setsolosid(-1);
  memcpy(chn, saved.chn, sizeof chn);
  memcpy(sidfilt, saved.filt, sizeof sidfilt);
  memcpy(sidreg, saved.reg, sizeof saved.reg);
  songinit = saved.songinit;
  lastsonginit = saved.lastsonginit;
  psnum = saved.psnum;
  followplay = saved.followplay;
  playframes = saved.playframes;
  timemin = saved.timemin;
  timesec = saved.timesec;
  timeframe = saved.timeframe;
  if (soundstopped)
  {
    sid_setvolume(mastervolume);
    do_sound_init();
    sid_setvolume(mastervolume);
    soundstopped = 0;
  }
}

// Start a rendering pass. solo = SID to render alone (-1 all), voicemask =
// voices to mute on that SID.
static void render_start(RENDER *r, int rate, int stereo, int frames, int fadeframes, int solo, int mutemask)
{
  int c;
  memset(r, 0, sizeof *r);
  r->rate = rate;
  r->stereo = stereo;
  r->spf = rate / playfps();
  r->totalframes = frames;
  r->fadeframes = fadeframes;
  r->gain = 1.0f;

  srand(1);
  sid_init(rate, editorInfo.sidmodel, editorInfo.ntsc, interpolate & 1, customclockrate, interpolate >> 1, numsids);
  sid_setpanning(numsids, stereo ? stereomode : 0);
  sid_setsolosid(solo);
  for (c = 0; c < MAX_SIDS; c++) sid_setvoicemask(c, 0);
  if (solo >= 0) sid_setvoicemask(solo, mutemask);
  sid_setvolumeimmediate(100);

  // Pre-roll: switch the master volume on and let the output settle, so
  // that the DC step of the 6581 (the "volume click") is not exported
  {
    static short tmp[4096 * 2];
    int left = rate / 3;
    for (c = 0; c < MAX_SIDS; c++)
    {
      memset(sidreg[c], 0, NUMSIDREGS);
      sidreg[c][0x18] = 0x0f;
    }
    while (left > 0)
    {
      int n = left > 4096 ? 4096 : left;
      sid_fillbuffer(tmp, n, editorInfo.adparam, stereo);
      left -= n;
    }
  }

  memcpy(chn, saved.chn, sizeof chn);
  if (!avs.respectmute)
    for (c = 0; c < MAX_CHN; c++) chn[c].mute = 0;
  initsong(avs.subtune, PLAY_BEGINNING);
  // the first call only initializes the song
  playroutine();
}

// Render one playroutine frame. Returns the number of sample frames.
static int render_frame(RENDER *r, short *out)
{
  int n, c;
  double ideal;

  playroutine();
  r->acc += r->spf;
  n = (int)r->acc;
  r->acc -= n;
  if (n <= 0) return 0;
  sid_fillbuffer(out, n, editorInfo.adparam, r->stereo);
  ideal = r->frame;

  // fade-out over the last frames (raised cosine)
  if ((r->fadeframes > 0) && (r->frame >= r->totalframes - r->fadeframes))
  {
    int ch = r->stereo ? 2 : 1;
    for (c = 0; c < n; c++)
    {
      double pos = (r->frame - (r->totalframes - r->fadeframes)) + (double)c / n;
      double t = pos / r->fadeframes;
      float g;
      if (t > 1.0) t = 1.0;
      g = (float)(0.5 + 0.5 * cos(t * 3.14159265358979));
      if (ch == 2)
      {
        out[c * 2] = (short)(out[c * 2] * g);
        out[c * 2 + 1] = (short)(out[c * 2 + 1] * g);
      }
      else out[c] = (short)(out[c] * g);
    }
  }
  (void)ideal;
  r->frame++;
  return n;
}

// ---------------------------------------------------------------------------
// Progress display
// ---------------------------------------------------------------------------

static int progress(const char *what, double frac)
{
  Uint32 now = SDL_GetTicks();
  if (cli)
  {
    static int lastpct = -1;
    int pct = (int)(frac * 100);
    if ((pct / 10 != lastpct / 10) || (frac >= 1.0))
    {
      printf("\r%s %3d%%", what, pct);
      fflush(stdout);
      lastpct = pct;
    }
    return 1;
  }
  if ((now - lastprogress < 60) && (frac < 1.0)) return !cancelled;
  lastprogress = now;
  {
    int w = 74, h = 9;
    int x0 = (MAX_COLUMNS - w) / 2, y0 = (MAX_ROWS - h) / 2 - 2;
    int barw = w - 6, fill, c;
    char buf[100];
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    dlg_drawframe(x0, y0, w, h, "Exporting");
    printtext(x0 + 3, y0 + 2, MKCOL(15, C_PANEL), what);
    fill = (int)(frac * barw + 0.5);
    for (c = 0; c < barw; c++)
    {
      if (c < fill) printglyph(x0 + 3 + c, y0 + 4, MKCOL(C_RB1 + c * 5 / barw, C_PANEL), GL_FULL);
      else printglyph(x0 + 3 + c, y0 + 4, MKCOL(C_LINE, C_PANEL), GL_SHADE1);
    }
    sprintf(buf, "%3d%%", (int)(frac * 100));
    printtext(x0 + w - 8, y0 + 2, MKCOL(8, C_PANEL), buf);
    printtext(x0 + 3, y0 + 6, MKCOL(3, C_PANEL), "ESC = cancel");
    fliptoscreen();
    if ((getenv("GG_PROGRESSSHOT")) && (frac > 0.3))
    {
      static int shotdone = 0;
      if (!shotdone) savescreenshot(getenv("GG_PROGRESSSHOT"));
      shotdone = 1;
    }
    win_checkmessages();
    if ((win_keytable[KEY_ESC]) || (win_quitted))
    {
      win_keytable[KEY_ESC] = 0;
      win_quitted = 0;
      cancelled = 1;
    }
  }
  return !cancelled;
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------

static void writewavheader(FILE *h, int rate, int channels, unsigned datasize)
{
  fwrite("RIFF", 4, 1, h);
  fwritele32(h, datasize + 36);
  fwrite("WAVEfmt ", 8, 1, h);
  fwritele32(h, 16);
  fwritele16(h, 1);
  fwritele16(h, channels);
  fwritele32(h, rate);
  fwritele32(h, rate * channels * 2);
  fwritele16(h, channels * 2);
  fwritele16(h, 16);
  fwrite("data", 4, 1, h);
  fwritele32(h, datasize);
}

static void writesamples(FILE *h, const short *buf, int count)
{
  // WAV is little endian
  unsigned char tmp[4096];
  while (count > 0)
  {
    int n = count > 2048 ? 2048 : count, c;
    for (c = 0; c < n; c++)
    {
      tmp[c * 2] = buf[c] & 0xff;
      tmp[c * 2 + 1] = (buf[c] >> 8) & 0xff;
    }
    fwrite(tmp, n * 2, 1, h);
    buf += n;
    count -= n;
  }
}

// Render one pass into a WAV file
static int renderwavpass(const char *name, const char *what, int frames, int fadeframes, int stereo, int solo, int mutemask,
  double progbase, double progscale)
{
  RENDER r;
  short buf[8192 * 2];
  unsigned datasize = 0;
  int ch = stereo ? 2 : 1;
  FILE *h = fopen(name, "wb");
  if (!h) return -1;
  writewavheader(h, avs.rate, ch, 0);
  render_start(&r, avs.rate, stereo, frames, fadeframes, solo, mutemask);
  while (r.frame < frames)
  {
    int n = render_frame(&r, buf);
    writesamples(h, buf, n * ch);
    datasize += n * ch * 2;
    if ((r.frame & 15) == 0)
    {
      if (!progress(what, progbase + progscale * r.frame / frames)) break;
    }
  }
  fseek(h, 0, SEEK_SET);
  writewavheader(h, avs.rate, ch, datasize);
  if (fclose(h)) return -1;
  if (cancelled)
  {
    remove(name);
    return 0;
  }
  return 1;
}

static int rendermp3(const char *name, int frames, int fadeframes)
{
  RENDER r;
  short buf[8192 * 2];
  unsigned char *mp3buf;
  int mp3size = 8192 * 2 + 8192 + 7200;
  int ch = avs.stereo ? 2 : 1;
  int n, ok = 1;
  FILE *h;
  lame_global_flags *gf = lame_init();
  if (!gf) return -1;
  lame_set_in_samplerate(gf, avs.rate);
  lame_set_out_samplerate(gf, avs.rate);
  lame_set_num_channels(gf, ch);
  lame_set_mode(gf, (MPEG_mode)(ch == 2 ? 1 : 3)); // JOINT_STEREO : MONO
  lame_set_brate(gf, avs.bitrate);
  lame_set_quality(gf, 2);
  lame_set_bWriteVbrTag(gf, 1);
  id3tag_init(gf);
  id3tag_add_v2(gf);
  if (songname[0]) id3tag_set_title(gf, songname);
  if (authorname[0]) id3tag_set_artist(gf, authorname);
  {
    char comment[80];
    snprintf(comment, sizeof comment, "Made with %s %s", PROGRAMNAME, PROGRAMVERSION);
    id3tag_set_comment(gf, comment);
    if (copyrightname[0])
    {
      // the copyright field usually starts with the year
      char year[5];
      if ((isdigit((unsigned char)copyrightname[0])) && (isdigit((unsigned char)copyrightname[3])))
      {
        memcpy(year, copyrightname, 4);
        year[4] = 0;
        id3tag_set_year(gf, year);
      }
    }
  }
  id3tag_set_genre(gf, "Electronic");
  if (lame_init_params(gf) < 0)
  {
    lame_close(gf);
    return -1;
  }
  h = fopen(name, "wb+");
  mp3buf = (unsigned char *)malloc(mp3size);
  if ((!h) || (!mp3buf))
  {
    if (h) fclose(h);
    free(mp3buf);
    lame_close(gf);
    return -1;
  }
  // ID3v2 tag at the start
  {
    unsigned char tag[4096];
    size_t len = lame_get_id3v2_tag(gf, tag, sizeof tag);
    if ((len > 0) && (len <= sizeof tag)) fwrite(tag, len, 1, h);
  }
  render_start(&r, avs.rate, avs.stereo, frames, fadeframes, -1, 0);
  while (r.frame < frames)
  {
    int samples = render_frame(&r, buf);
    if (ch == 2)
      n = lame_encode_buffer_interleaved(gf, buf, samples, mp3buf, mp3size);
    else
      n = lame_encode_buffer(gf, buf, NULL, samples, mp3buf, mp3size);
    if (n < 0)
    {
      ok = -1;
      break;
    }
    if (n > 0) fwrite(mp3buf, n, 1, h);
    if ((r.frame & 15) == 0)
    {
      if (!progress("Encoding MP3...", (double)r.frame / frames)) break;
    }
  }
  if ((ok > 0) && (!cancelled))
  {
    n = lame_encode_flush(gf, mp3buf, mp3size);
    if (n > 0) fwrite(mp3buf, n, 1, h);
    {
      unsigned char tag[128];
      // ID3v1 tag at the end
      size_t len = lame_get_id3v1_tag(gf, tag, sizeof tag);
      if ((len > 0) && (len <= sizeof tag)) fwrite(tag, len, 1, h);
    }
    // Xing/LAME info frame (exact length for players)
    lame_mp3_tags_fid(gf, h);
  }
  free(mp3buf);
  lame_close(gf);
  if (fclose(h)) ok = -1;
  if ((cancelled) || (ok < 0))
  {
    remove(name);
    return cancelled ? 0 : -1;
  }
  return 1;
}

static int rendermp4(const char *name, int frames, int fadeframes, char *error, int errorsize)
{
  RENDER r;
  VIDEOPARAMS vp;
  GGVIDEO *v;
  static short buf[8192 * 2];
  static short scope[MAX_SIDS * 4096 * 3];
  char info[128];
  int rate = 48000;
  int stereo = avs.stereo;
  double fps = editorInfo.ntsc ? 60 : 50;
  int videoframes;

  videoframes = (int)(frames / playfps() * fps + 0.5);
  sprintf(info, "%d SID%s  \xb7  %s  \xb7  %s  \xb7  Subtune %d", numsids, numsids > 1 ? "s" : "",
    editorInfo.sidmodel ? "MOS 8580" : "MOS 6581", editorInfo.ntsc ? "NTSC" : "PAL", avs.subtune + 1);
  memset(&vp, 0, sizeof vp);
  vp.filename = name;
  vp.fps = (int)fps;
  vp.rate = rate;
  vp.channels = stereo ? 2 : 1;
  vp.nsids = numsids;
  vp.totalframes = videoframes;
  vp.daytheme = avs.videotheme;
  vp.title = songname;
  vp.author = authorname;
  vp.info = info;
  v = video_open(&vp, error, errorsize);
  if (!v) return -1;

  render_start(&r, rate, stereo, frames, fadeframes, -1, 0);
  sid_setscope(scope, 4096);
  while (r.frame < frames)
  {
    int n = render_frame(&r, buf);
    video_addaudio(v, buf, n);
    video_addscope(v, scope, 4096, n);
    if (!video_update(v, 0)) break;
    if ((r.frame & 3) == 0)
    {
      if (!progress("Rendering video...", (double)video_framesdone(v) / videoframes)) break;
    }
  }
  sid_setscope(NULL, 0);
  if ((!cancelled) && (!video_update(v, 1)))
  {
    video_close(v, 1);
    snprintf(error, errorsize, "Video encoding failed");
    return -1;
  }
  if (!video_close(v, cancelled))
  {
    if (cancelled) return 0;
    snprintf(error, errorsize, "Writing %s failed", name);
    return -1;
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Length
// ---------------------------------------------------------------------------

typedef struct
{
  int songframes;     // until the song loops / stops
  int stopped;
  int frames;         // to render
  int fadeframes;
} LENGTH;

static void calclength(LENGTH *l, int songframes, int stopped)
{
  double fps = playfps();
  l->songframes = songframes;
  l->stopped = stopped;
  if (avs.automatic)
  {
    if ((songframes <= 0) || (stopped))
    {
      // the song ends by itself: one extra second for the release
      l->frames = songframes + (int)fps;
      l->fadeframes = 0;
    }
    else
    {
      l->fadeframes = (int)(avs.fade * fps);
      l->frames = songframes + l->fadeframes;
    }
  }
  else
  {
    l->frames = (int)(avs.seconds * fps);
    l->fadeframes = (int)(avs.fade * fps);
    if (l->fadeframes > l->frames) l->fadeframes = l->frames;
  }
  if (l->frames < 1) l->frames = 1;
}

static void fmtlength(char *buf, double sec)
{
  int t = (int)(sec + 0.5);
  sprintf(buf, "%d:%02d", t / 60, t % 60);
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

static int countsubtunes(void)
{
  int c, n = 0;
  for (c = 0; c < MAX_SONGS; c++)
    if (songvalid(c)) n = c + 1;
  return n ? n : 1;
}

static int avdialog(void)
{
  int sel = 0;
  int w = 84, h = 22;
  int x0 = (MAX_COLUMNS - w) / 2;
  int y0 = (MAX_ROWS - h) / 2 - 1;
  int rows[16], numrows, c;
  int first = 1;
  int lastsub = -1, songframes = 0, stopped = 0;
  int subtunes = countsubtunes();

  if (avs.subtune >= subtunes) avs.subtune = 0;
  key = 0;
  rawkey = 0;
  for (;;)
  {
    char buf[160], t1[16], t2[16];
    int hitrow = -1, hitbtn = 0;
    LENGTH len;
    int y;

    if (lastsub != avs.subtune)
    {
      songframes = calcsonglengthex(avs.subtune, &stopped);
      lastsub = avs.subtune;
    }
    calclength(&len, songframes, stopped);

    // 0 format, 1 subtune, 2 length mode, 3 custom time, 4 fade, 5 channels,
    // 6 sample rate, 7 bitrate, 8 stems, 9 respect mute, 10 video theme,
    // 14 export, 15 cancel
    numrows = 0;
    rows[numrows++] = 0;
    rows[numrows++] = 1;
    rows[numrows++] = 2;
    if (!avs.automatic) rows[numrows++] = 3;
    rows[numrows++] = 4;
    rows[numrows++] = 5;
    if (avs.format != AV_MP4) rows[numrows++] = 6;
    if (avs.format == AV_MP3) rows[numrows++] = 7;
    if (avs.format == AV_WAV) rows[numrows++] = 8;
    rows[numrows++] = 9;
    if (avs.format == AV_MP4) rows[numrows++] = 10;
    rows[numrows++] = 14;
    rows[numrows++] = 15;
    if (first)
    {
      sel = numrows - 2;
      first = 0;
    }
    if (sel >= numrows) sel = numrows - 1;

    printstatus();
    dlg_drawframe(x0, y0, w, h, "Export audio / video");
    for (c = 0; c < numrows; c++)
    {
      int r = rows[c];
      int isel = (c == sel);
      int bg = isel ? C_SELECT : C_PANEL;
      int ry = y0 + 2 + c;
      buf[0] = 0;
      switch (r)
      {
        case 0: sprintf(buf, "Format            " GS_PREV " %s " GS_NEXT, formatnames[avs.format]); break;
        case 1: sprintf(buf, "Subtune           " GS_PREV " %02X " GS_NEXT "   (%d in this song)", avs.subtune, subtunes); break;
        case 2:
          fmtlength(t1, songframes / playfps());
          if (avs.automatic)
            sprintf(buf, "Length            " GS_PREV " Song length %s%s " GS_NEXT, t1, stopped ? " (song ends)" : " + fade-out");
          else
            sprintf(buf, "Length            " GS_PREV " Custom " GS_NEXT "   (song length %s)", t1);
          break;
        case 3:
          fmtlength(t1, avs.seconds);
          sprintf(buf, "Custom length     %s   (LEFT/RIGHT 1 s, SHIFT 10 s, ENTER type)", t1);
          break;
        case 4:
          if (avs.fade) sprintf(buf, "Fade-out          " GS_PREV " %d seconds " GS_NEXT, avs.fade);
          else sprintf(buf, "Fade-out          " GS_PREV " Off " GS_NEXT);
          if ((avs.automatic) && (stopped)) strcat(buf, "   (not needed, the song ends)");
          break;
        case 5: sprintf(buf, "Channels          " GS_PREV " %s " GS_NEXT, avs.stereo ? "Stereo" : "Mono"); break;
        case 6: sprintf(buf, "Sample rate       " GS_PREV " %d Hz " GS_NEXT, avs.rate); break;
        case 7: sprintf(buf, "MP3 bitrate       " GS_PREV " %d kbit/s CBR " GS_NEXT, avs.bitrate); break;
        case 8: sprintf(buf, "[%c] Also save every voice as its own mono WAV (%d files)", avs.stems ? 'x' : ' ', numchannels); break;
        case 9: sprintf(buf, "[%c] Keep muted voices muted", avs.respectmute ? 'x' : ' '); break;
        case 10: sprintf(buf, "Video colours     " GS_PREV " %s " GS_NEXT, avs.videotheme ? "Day (light)" : "Night (C64 blue)"); break;
      }
      if (r == 14)
      {
        printtext(x0 + w - 24, y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), "  Export  ");
        if ((mousey == y0 + h - 2) && (mousex >= x0 + w - 24) && (mousex < x0 + w - 14)) hitbtn = 1;
        continue;
      }
      if (r == 15)
      {
        printtext(x0 + w - 12, y0 + h - 2, MKCOL(C_BUTTONTEXT, isel ? C_BUTTONHI : C_BUTTON), "  Cancel  ");
        if ((mousey == y0 + h - 2) && (mousex >= x0 + w - 12) && (mousex < x0 + w - 2)) hitbtn = 2;
        continue;
      }
      printblankc(x0 + 2, ry, MKCOL(8, bg), w - 4);
      printtext(x0 + 3, ry, MKCOL(isel ? 15 : 8, bg), buf);
      if ((mousey == ry) && (mousex >= x0 + 2) && (mousex < x0 + w - 2)) hitrow = c;
    }

    // Summary
    y = y0 + 13;
    fmtlength(t1, len.frames / playfps());
    fmtlength(t2, len.fadeframes / playfps());
    printtext(x0 + 3, y, MKCOL(14, C_PANEL), "Result:");
    if (avs.format == AV_MP4)
      sprintf(buf, "%s, 1280x720 at %d fps, H.264 + AAC %s 48 kHz", t1, editorInfo.ntsc ? 60 : 50, avs.stereo ? "stereo" : "mono");
    else if (avs.format == AV_MP3)
      sprintf(buf, "%s, MP3 %d kbit/s, %s, %d Hz, about %d KB", t1, avs.bitrate, avs.stereo ? "stereo" : "mono", avs.rate,
        (int)(len.frames / playfps() * avs.bitrate / 8));
    else
      sprintf(buf, "%s, 16 bit PCM, %s, %d Hz, %.1f MB", t1, avs.stereo ? "stereo" : "mono", avs.rate,
        len.frames / playfps() * avs.rate * (avs.stereo ? 4 : 2) / 1048576.0);
    printtext(x0 + 21, y, MKCOL(8, C_PANEL), buf);
    if (len.fadeframes)
    {
      sprintf(buf, "Fades out during the last %s", t2);
      printtext(x0 + 21, y + 1, MKCOL(8, C_PANEL), buf);
    }
    sprintf(buf, "%d x %s, %s, %s, %s", numsids, editorInfo.sidmodel ? "8580" : "6581", editorInfo.ntsc ? "NTSC" : "PAL",
      interpolate >= 2 ? "reSID-fp" : "reSID", interpolate & 1 ? "interpolation" : "fast sampling");
    printtext(x0 + 3, y + 2, MKCOL(14, C_PANEL), "SIDs:");
    printtext(x0 + 21, y + 2, MKCOL(8, C_PANEL), buf);
    if (avs.format == AV_MP4)
    {
      sprintf(buf, "%d oscilloscopes, song name, author, play time", numchannels);
      printtext(x0 + 3, y + 3, MKCOL(14, C_PANEL), "Video:");
      printtext(x0 + 21, y + 3, MKCOL(8, C_PANEL), buf);
    }
    if ((avs.format == AV_WAV) && (avs.stems))
    {
      printtext(x0 + 3, y + 3, MKCOL(14, C_PANEL), "Voice files:");
      printtext(x0 + 21, y + 3, MKCOL(8, C_PANEL), numsids > 1 ? "NAME_sid1_voice1.wav ... (mono, full level, no panning)" : "NAME_voice1.wav ... NAME_voice3.wav (mono, full level)");
    }
    printtext(x0 + 3, y0 + h - 4, MKCOL(3, C_PANEL), "UP/DOWN select, LEFT/RIGHT/SPACE change, ENTER export, ESC cancel");

    fliptoscreen();
    getkey();
    if (win_quitted)
    {
      win_quitted = 0;
      return 0;
    }
    if ((mouseb) && (!prevmouseb))
    {
      if (hitbtn == 1)
      {
        sel = numrows - 2;
        rawkey = KEY_ENTER;
      }
      else if (hitbtn == 2) return 0;
      else if (hitrow >= 0)
      {
        sel = hitrow;
        rawkey = (mouseb & MOUSEB_RIGHT) ? KEY_LEFT : KEY_RIGHT;
        if ((rows[sel] >= 8) && (rows[sel] <= 9)) rawkey = KEY_SPACE;
        if (rows[sel] == 3) rawkey = KEY_ENTER;
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
        int dir = rawkey == KEY_LEFT ? -1 : 1;
        int i;
        if ((rawkey == KEY_ENTER) && (r != 3) && (r != 15)) r = 14;
        switch (r)
        {
          case 0: avs.format = (avs.format + dir + 3) % 3; break;
          case 1: avs.subtune = (avs.subtune + dir + subtunes) % subtunes; break;
          case 2: avs.automatic ^= 1; break;
          case 3:
          if (rawkey == KEY_ENTER)
          {
            char tb[16];
            fmtlength(tb, avs.seconds);
            if (dlg_edittext("Custom length", "Length (m:ss or seconds):", tb, 8, 1))
            {
              int m = 0, s = 0;
              if (strchr(tb, ':')) sscanf(tb, "%d:%d", &m, &s);
              else s = atoi(tb);
              if (m * 60 + s > 0) avs.seconds = m * 60 + s;
            }
          }
          else
          {
            avs.seconds += dir * (shiftpressed ? 10 : 1);
            if (avs.seconds < 1) avs.seconds = 1;
          }
          if (avs.seconds > 60 * 60) avs.seconds = 60 * 60;
          break;
          case 4:
          for (i = 0; i < 6; i++) if (fadeoptions[i] == avs.fade) break;
          if (i >= 6) i = 0;
          i = (i + dir + 6) % 6;
          avs.fade = fadeoptions[i];
          break;
          case 5: avs.stereo ^= 1; break;
          case 6: avs.rate = avs.rate == 44100 ? 48000 : 44100; break;
          case 7:
          for (i = 0; i < 5; i++) if (bitrates[i] == avs.bitrate) break;
          if (i >= 5) i = 4;
          i = (i + dir + 5) % 5;
          avs.bitrate = bitrates[i];
          break;
          case 8: avs.stems ^= 1; break;
          case 9: avs.respectmute ^= 1; break;
          case 10: avs.videotheme ^= 1; break;
          case 14:
          if ((rawkey == KEY_ENTER) || (rawkey == KEY_SPACE)) return 1;
          break;
          case 15:
          if ((rawkey == KEY_ENTER) || (rawkey == KEY_SPACE)) return 0;
          break;
        }
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

static void stemname(char *dest, const char *name, int s, int v)
{
  char base[MAX_PATHNAME];
  char *dot;
  strncpy(base, name, MAX_PATHNAME - 1);
  base[MAX_PATHNAME - 1] = 0;
  dot = strrchr(base, '.');
  if (dot) *dot = 0;
  if (numsids > 1) snprintf(dest, MAX_PATHNAME, "%s_sid%d_voice%d.wav", base, s + 1, v + 1);
  else snprintf(dest, MAX_PATHNAME, "%s_voice%d.wav", base, v + 1);
}

// Render with the current settings. Returns 1 ok, 0 cancelled, -1 error.
static int doavexport(const char *name, char *error, int errorsize)
{
  LENGTH len;
  int songframes, stopped, result = 1;

  cancelled = 0;
  lastprogress = 0;
  error[0] = 0;
  if (!cli) printmainscreen();
  render_enter();
  songframes = calcsonglengthex(avs.subtune, &stopped);
  calclength(&len, songframes, stopped);

  switch (avs.format)
  {
    case AV_WAV:
    {
      double scale = avs.stems ? 1.0 / (1 + numsids) : 1.0;
      int s, v;
      result = renderwavpass(name, "Rendering WAV...", len.frames, len.fadeframes, avs.stereo, -1, 0, 0, scale);
      if (result < 0) snprintf(error, errorsize, "Cannot write %s", name);
      if ((result > 0) && (avs.stems))
      {
        for (s = 0; (s < numsids) && (result > 0); s++)
        {
          for (v = 0; (v < 3) && (result > 0); v++)
          {
            char sn[MAX_PATHNAME];
            char what[80];
            stemname(sn, name, s, v);
            if (numsids > 1) sprintf(what, "Rendering SID %d voice %d...", s + 1, v + 1);
            else sprintf(what, "Rendering voice %d...", v + 1);
            result = renderwavpass(sn, what, len.frames, len.fadeframes, 0, s, 7 & ~(1 << v),
              scale * (1 + s + v / 3.0), scale / 3);
            if (result < 0) snprintf(error, errorsize, "Cannot write %s", sn);
          }
        }
      }
    }
    break;

    case AV_MP3:
    result = rendermp3(name, len.frames, len.fadeframes);
    if (result < 0) snprintf(error, errorsize, "MP3 encoding failed (%s)", name);
    break;

    case AV_MP4:
    result = rendermp4(name, len.frames, len.fadeframes, error, errorsize);
    break;
  }
  render_leave();
  if (cli) printf("\n");
  return result;
}

void exportaudio(int video)
{
  char filename[MAX_FILENAME];
  char filter[MAX_FILENAME];
  char error[256];
  int c, result;
  const char *ext;

  stopsong();
  if (video) avs.format = AV_MP4;
  else if (avs.format == AV_MP4) avs.format = AV_WAV;
  if (!avdialog())
  {
    printmainscreen();
    key = rawkey = 0;
    return;
  }
  ext = formatext[avs.format];
  memset(filename, 0, sizeof filename);
  for (c = 0; c < (int)strlen(loadedsongfilename) && c < MAX_FILENAME - 8; c++)
  {
    if (loadedsongfilename[c] == '.') break;
    filename[c] = loadedsongfilename[c];
  }
  if (!filename[0]) strcpy(filename, "music");
  strcat(filename, ext);
  sprintf(filter, "*%s", ext);
  for (;;)
  {
    FILE *test;
    if (!fileselector(filename, packedpath, filter, avs.format == AV_MP4 ? "SAVE VIDEO" : "SAVE AUDIO", 3))
    {
      printmainscreen();
      key = rawkey = 0;
      return;
    }
    if ((!strchr(filename, '.')) && (strlen(filename) < MAX_FILENAME - 5)) strcat(filename, ext);
    test = fopen(filename, "ab");
    if (test)
    {
      fclose(test);
      break;
    }
  }
  result = doavexport(filename, error, sizeof error);
  if (result > 0)
  {
    if ((avs.format == AV_WAV) && (avs.stems))
      snprintf(statusmessage, sizeof statusmessage, "Exported %s and %d voice files", filename, numchannels);
    else
      snprintf(statusmessage, sizeof statusmessage, "Exported %s", filename);
  }
  else if (result == 0)
    snprintf(statusmessage, sizeof statusmessage, "Export cancelled");
  else
  {
    const char *l[1];
    l[0] = error[0] ? error : "Export failed";
    dlg_message("Export failed", l, 1);
  }
  printmainscreen();
  key = rawkey = 0;
}

// Command line: --render=NAME.wav/.mp3/.mp4 [--seconds=N] [--stems] [--fade=N] [--mono]
int commandlinerender(const char *name, int subtune, int seconds, int stems, int fade, int mono, int bitrate)
{
  char error[256];
  const char *ext = strrchr(name, '.');
  int result;
  cli = 1;
  avs.format = AV_WAV;
  if ((ext) && (!strcasecmp(ext, ".mp3"))) avs.format = AV_MP3;
  if ((ext) && (!strcasecmp(ext, ".mp4"))) avs.format = AV_MP4;
  avs.subtune = subtune;
  avs.automatic = seconds <= 0;
  avs.seconds = seconds > 0 ? seconds : 180;
  avs.fade = fade >= 0 ? fade : (seconds > 0 ? 0 : 5);
  avs.stems = stems;
  avs.stereo = !mono;
  if (bitrate) avs.bitrate = bitrate;
  result = doavexport(name, error, sizeof error);
  if (result <= 0)
  {
    fprintf(stderr, "Render failed: %s\n", error[0] ? error : "unknown error");
    return 0;
  }
  printf("Rendered %s\n", name);
  return 1;
}

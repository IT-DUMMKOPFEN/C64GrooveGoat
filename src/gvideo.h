#ifndef GVIDEO_H
#define GVIDEO_H

// C64 GrooveGoat - MP4 video export: oscilloscope view of every SID voice
// (1280x720 H.264 + AAC audio)

#define VIDEO_W 1280
#define VIDEO_H 720

typedef struct GGVIDEO GGVIDEO;

typedef struct
{
  const char *filename;
  int fps;              // 50 (PAL) or 60 (NTSC)
  int rate;             // audio sample rate (48000)
  int channels;         // audio channels (1 or 2)
  int nsids;            // 1, 2, 4 or 8
  int totalframes;      // number of video frames (for the progress bar)
  int daytheme;         // 0 = C64 night blue, 1 = light
  const char *title;
  const char *author;
  const char *info;     // e.g. "2 x 8580 - PAL"
} VIDEOPARAMS;

GGVIDEO *video_open(const VIDEOPARAMS *p, char *error, int errorsize);
// interleaved PCM, "frames" sample frames
int video_addaudio(GGVIDEO *v, const short *pcm, int frames);
// oscilloscope samples [sid][stride][voice]
void video_addscope(GGVIDEO *v, const short *scope, int stride, int samples);
// Render & encode all video frames whose audio is available. If "final" is
// set, the remaining frames up to totalframes are rendered too.
int video_update(GGVIDEO *v, int final);
int video_framesdone(GGVIDEO *v);
const char *video_errortext(GGVIDEO *v);
// Finish the file. Returns 1 if ok. If "abort" is set, the file is deleted.
int video_close(GGVIDEO *v, int abort);

#endif

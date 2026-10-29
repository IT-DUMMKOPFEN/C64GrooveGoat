#ifndef GPIANO_H
#define GPIANO_H

// C64 GrooveGoat - piano keyboard visualisation (editor and MP4 video)

#define PIANO_KEYS 96             // C-0 ... B-7
#define PIANO_WHITEKEYS 56

typedef struct
{
  unsigned bg;                    // background around the keys
  unsigned white, whiteshade;     // white key, its lower edge
  unsigned black, blackhi;        // black key, its highlight
  unsigned edge;                  // gaps between the white keys
  unsigned label;                 // octave labels (C0 ... C7)
  unsigned voice[3];              // colours of voice 1, 2 and 3 of a SID
} PIANOCOLORS;

typedef struct
{
  const unsigned char *data;      // 8 bit alpha glyphs, 256 characters
  int w, h;                       // glyph size in pixels
} PIANOFONT;

// Which voices hold which note: keyvoices[note] = bitmask of the channels
// whose gate is on with this note (muted voices are left out)
void piano_getstate(unsigned keyvoices[PIANO_KEYS]);

// Draw the keyboard into a 0xRRGGBB buffer of w x h pixels
void piano_render(unsigned *buf, int stride, int w, int h, const unsigned keyvoices[PIANO_KEYS],
  const PIANOCOLORS *col, const PIANOFONT *font);

// Key under a pixel position of the keyboard (black keys first), or -1
int piano_keyat(int px, int py, int w, int h);

// Colour sets for the night and the day theme
extern const PIANOCOLORS piano_night, piano_day;

#endif

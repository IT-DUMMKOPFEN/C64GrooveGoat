#ifndef GSPEECH_H
#define GSPEECH_H

// C64 GrooveGoat - speech generator: English text -> formant synthesis on
// one SID voice (instruments, wave / pulse / filter tables and pattern rows)

#define SPEECH_MAXTEXT 256

typedef struct
{
  char text[SPEECH_MAXTEXT];  // English text, or phonemes between slashes: /T AH0 G EH1 DH ER0/
  int basenote;               // note of the voice (0 = C-0 ... 95 = B-7), e.g. 33 = A-2
  int speed;                  // duration in percent (100 = normal, 150 = slower)
  int melody;                 // 0 monotone, 1 statement (falling), 2 question (rising), 3 command (strong accents)
  int voice;                  // 0 robot (pulse), 1 bright (sawtooth), 2 whisper (noise)
  int tempo;                  // ticks per pattern row the timing is calculated for (3 = 60 ms)
  int settempo;               // put an F0x tempo command on the first row
  int mode;                   // formant model (see gspeech.c), 0 = default
} SPEECHPARAMS;

void speech_defaults(SPEECHPARAMS *p);

// Text -> phoneme string ("T AH0 G EH1 DH ER0 | W IY1 | HH AH1 N T").
// Returns the number of phonemes, 0 on error (message in err).
int speech_phonemes(const char *text, char *out, int outsize, char *err, int errsize);

// Generate the speech into the song: channel chn, its current pattern, from
// row "row" on. New instruments and table entries are appended. Returns the
// number of pattern rows used, 0 on error (message in err).
int speech_generate(const SPEECHPARAMS *p, int chn, int row, char *err, int errsize);

// Length of the speech without changing the song: pattern rows and
// milliseconds. Returns the number of sounds, 0 on error.
int speech_analyze(const SPEECHPARAMS *p, int *rows, int *ms, char *err, int errsize);

// The "Speech generator" dialog (CTRL+K): inserts into the channel and row of
// the pattern cursor
void speechdialog(void);

// Text listing of the last generated speech (instrument setup, tables and
// pattern rows)
const char *speech_listing(void);

#endif

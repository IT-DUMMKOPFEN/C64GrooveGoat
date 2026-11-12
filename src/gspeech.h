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
  int melody;                 // 0 monotone, 1 statement, 2 question, 3 command ... (speech_melodyname)
  int voice;                  // speaker (speech_voicename): 0 man, 1 woman, 2 child, 3 giant, 4 robot ...
  int tempo;                  // song speed: frames (ticks) per pattern row, F02 ... F10;
                              // 0 = take it from the song (speech_songtempo)
  int settempo;               // put an F0x tempo command on the first row
  int mode;                   // formant model (see gspeech.c), 0 = default
  int share;                  // 1 = one instrument per sound (phonemes) in a sentence
  int twovoices;              // 1 = a second SID voice (the next one of the SID), hard synced:
                              //     the second formant and the hiss of voiced sounds
} SPEECHPARAMS;

void speech_defaults(SPEECHPARAMS *p);

// Speakers (voice presets): count, name, default pitch (note)
int speech_numvoices(void);
const char *speech_voicename(int v);
int speech_voicenote(int v);

// Melodies: count and name
int speech_nummelodies(void);
const char *speech_melodyname(int m);

// Speed of the song around the cursor (frames per row): the first tempo
// command in the current patterns, else the default tempo (6)
int speech_songtempo(int chn);

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

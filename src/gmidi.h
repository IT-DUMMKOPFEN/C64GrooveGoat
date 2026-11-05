#ifndef GMIDI_H
#define GMIDI_H

// MIDI keyboard input (Windows: winmm, macOS: CoreMIDI, Linux: ALSA
// sequencer). The system libraries are loaded at runtime where possible, so
// the program also runs without them.

extern unsigned midion;            // 0 = MIDI input off
extern char mididevice[];          // "" = all devices, otherwise the name
extern int midi_inmain;            // set by the main loop while it waits for input
extern int midi_pending;           // notes are waiting to be entered

void midi_init(void);
void midi_shutdown(void);
void midi_service(void);           // called from getkey(): reads and plays the notes
int midi_nextentry(void);          // next note to enter into the pattern, -1 = none
void midi_inject(int status, int d1, int d2);  // scripted tests
void midi_allnotesoff(void);
int midi_numdevices(void);
const char *midi_devicename(int i);
int midi_deviceused(int i);
int midi_active(void);             // at least one device is listening
void mididialog(void);

#endif

#ifndef GIMPROVE_H
#define GIMPROVE_H

// C64 GrooveGoat - Improve: automatic improvements of the current subtune
// (drums, bass, slides, vibrato, pulse width modulation, balance, gate
// timer fixes, echo, filter sweeps, SID chip adaptation, clean-up).
// Changed instruments are copies; only the current subtune uses them.

enum
{
  IM_DRUMS = 0, IM_BASSFULL, IM_BASSPUSH, IM_SLIDES, IM_VIBRATO, IM_PWM, IM_BALANCE, IM_FIX,
  IM_ECHO, IM_FILTER, IM_CLEANUP, IM_NUM
};

// The "Improve" dialog (CTRL+I)
void improvedialog(void);

// Command line / tests: modules = bitmask of IM_*, strength 1-3, kit (KIT_*),
// chip 0 off / 1 6581->8580 / 2 8580->6581. Returns 1 on success.
int improve_cli(int modules, int strength, int kit, int chip, char *msg, int msgsize);

#endif

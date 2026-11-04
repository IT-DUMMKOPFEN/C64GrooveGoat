#ifndef GJOIN_H
#define GJOIN_H

// C64 GrooveGoat - Join Songs: load other songs (.sng / .ggt) into the
// current song, as new subtunes or appended to the current subtune (medley).
// Identical instruments, table programs and patterns are shared.

#define JOIN_MAXFILES 16

enum { JOIN_SUBTUNES = 0, JOIN_MEDLEY };

// The "Join Songs" dialog (SHIFT+F10)
void joinsongsdialog(void);

// Command line / tests: join the files (comma separated) into the current
// song. Returns 1 on success; report / error in msg.
int joinsongs_cli(const char *files, int mode, int raise, int backup, char *msg, int msgsize);

#endif

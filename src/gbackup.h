#ifndef GBACKUP_H
#define GBACKUP_H

void backup_init(void);
void backup_tick(void);
void backup_saved(void);
void backup_shutdown(void);
int backup_copybeforesave(const char *filename);
// keep / restore the crash-recovery state around loading another song
// temporarily (Join Songs reads its source files with the song loader)
void *backup_keepstate(void);
void backup_restorestate(void *state);

#endif

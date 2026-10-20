#ifndef GBACKUP_H
#define GBACKUP_H

void backup_init(void);
void backup_tick(void);
void backup_saved(void);
void backup_shutdown(void);
int backup_copybeforesave(const char *filename);

#endif

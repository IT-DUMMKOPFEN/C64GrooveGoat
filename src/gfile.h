#ifndef GFILE_H
#define GFILE_H

#define MAX_DIRFILES 16384
#define MAX_FILENAME 256
#define MAX_PATHNAME 1024
#define FILESEL_INSTRUMENT 0x100

typedef struct
{
  char *name;
  int attribute;
} DIRENTRY;

void initpaths(void);
int fileselector(char *name, char *path, char *filter, char *title, int filemode);
void editstring(char *buffer, int maxlength);
int editstring_cursor(char *buffer);           // cursor column of a text field (0 = start)
void editstring_click(char *buffer, int column); // mouse click into a text field
int cmpname(char *string1, char *string2);

#endif


#ifndef GDIALOG_H
#define GDIALOG_H

int dlg_confirm(const char *title, const char *text);
void dlg_message(const char *title, const char **lines, int numlines);
int dlg_choose(const char *title, const char **lines, int numlines, const char **options, int numoptions, int def);
int dlg_edithex(const char *title, const char *label, unsigned *value, int digits);
void dlg_drawframe(int x, int y, int w, int h, const char *title);

#endif

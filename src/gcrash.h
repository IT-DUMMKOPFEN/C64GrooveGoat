#ifndef GCRASH_H
#define GCRASH_H

// A crash report on Windows (crash.txt in the configuration folder)
void crash_install(const char *version);
// Remembers one of the last steps of the program for the report
void crash_note(const char *fmt, ...);

#endif

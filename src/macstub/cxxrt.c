// Minimal C++ runtime support for the macOS cross build (reSID only needs
// operator new/delete). The names are the Itanium C++ ABI mangled ones.
#include <stdlib.h>

void *_Znwm(unsigned long size) { void *p = malloc(size ? size : 1); if (!p) abort(); return p; }
void *_Znam(unsigned long size) { void *p = malloc(size ? size : 1); if (!p) abort(); return p; }
void _ZdlPv(void *p) { free(p); }
void _ZdaPv(void *p) { free(p); }
void _ZdlPvm(void *p, unsigned long s) { (void)s; free(p); }
void _ZdaPvm(void *p, unsigned long s) { (void)s; free(p); }
void __cxa_pure_virtual(void) { abort(); }

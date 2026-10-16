/* Standalone wrapper around the built-in 6502 assembler (for tests) */
#include <stdio.h>
#include <stdlib.h>
#include "membuf.h"
#include "parse.h"
#include "log.h"
int main(int argc, char **argv)
{
  struct membuf src = STATIC_MEMBUF_INIT, dest = STATIC_MEMBUF_INIT;
  FILE *f; char buf[4096]; size_t n;
  if (argc < 3) { fprintf(stderr, "usage: asmtest in.s out.bin\n"); return 1; }
  LOG_INIT_CONSOLE(LOG_NORMAL);
  f = fopen(argv[1], "rb"); if (!f) return 1;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) membuf_append(&src, buf, n);
  fclose(f);
  membuf_append(&src, "\n", 1);
  if (assemble(&src, &dest)) { fprintf(stderr, "assembly failed\n"); return 2; }
  f = fopen(argv[2], "wb"); fwrite(membuf_get(&dest), membuf_memlen(&dest), 1, f); fclose(f);
  printf("%d bytes\n", membuf_memlen(&dest));
  return 0;
}

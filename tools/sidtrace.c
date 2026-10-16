/*
 * sidtrace - minimal 6502 emulator for testing exported SID files.
 *
 * Loads a PSID file (v2/v3/v4/v5 multi-SID header), calls init with the
 * given subtune and then play for N frames. After each play call the
 * current register state of every SID is printed as one line:
 *   frame: sid0regs | sid1regs | ...
 * Only writes go to SIDs; reads of SID registers return the last value.
 *
 * usage: sidtrace file.sid frames [subtune] [--prg addr]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char mem[65536];
static int c64mode = 0;
static unsigned char sidregs[16][32];
static unsigned sidbase[16];
static int numsids = 1;
static unsigned short pc;
static unsigned char a, x, y, sp, p;
static long writes;
static long maxframe;

#define FC 0x01
#define FZ 0x02
#define FI 0x04
#define FD 0x08
#define FB 0x10
#define FV 0x40
#define FN 0x80

static int sidindex(unsigned addr)
{
  int s;
  for (s = 0; s < numsids; s++)
    if ((addr >= sidbase[s]) && (addr < sidbase[s] + 0x20)) return s;
  return -1;
}


static long cycles;

static unsigned char rd(unsigned addr)
{
  addr &= 0xffff;
  if ((c64mode) && (addr == 0xd012)) return (unsigned char)((cycles / 63) % 312);
  return mem[addr];
}

static void wr(unsigned addr, unsigned char v)
{
  int s;
  addr &= 0xffff;
  s = sidindex(addr);
  if (s >= 0)
  {
    sidregs[s][addr - sidbase[s]] = v;
    writes++;
  }
  else if ((!c64mode) && (addr >= 0xd400) && (addr < 0xd800))
  {
    fprintf(stderr, "warning: write to unexpected SID address $%04X\n", addr);
  }
  mem[addr] = v;
}

static void setnz(unsigned char v)
{
  p &= ~(FZ | FN);
  if (!v) p |= FZ;
  p |= v & FN;
}

static void push(unsigned char v) { mem[0x100 + sp] = v; sp--; }
static unsigned char pop(void) { sp++; return mem[0x100 + sp]; }

static unsigned rd16(unsigned addr) { return rd(addr) | (rd(addr + 1) << 8); }
static unsigned rd16zp(unsigned addr) { return rd(addr & 0xff) | (rd((addr + 1) & 0xff) << 8); }

static void adc(unsigned char v)
{
  if (p & FD)
  {
    int lo = (a & 0x0f) + (v & 0x0f) + (p & FC);
    int hi = (a >> 4) + (v >> 4);
    if (lo > 9) { lo += 6; hi++; }
    if (hi > 9) hi += 6;
    p &= ~(FC | FZ | FN | FV);
    if (hi > 15) p |= FC;
    a = ((hi << 4) | (lo & 0x0f)) & 0xff;
    setnz(a);
    return;
  }
  {
    unsigned r = a + v + (p & FC);
    p &= ~(FC | FV);
    if (r > 0xff) p |= FC;
    if ((~(a ^ v) & (a ^ r)) & 0x80) p |= FV;
    a = r & 0xff;
    setnz(a);
  }
}

static void sbc(unsigned char v)
{
  if (p & FD)
  {
    int lo = (a & 0x0f) - (v & 0x0f) - ((p & FC) ? 0 : 1);
    int hi = (a >> 4) - (v >> 4);
    if (lo < 0) { lo -= 6; hi--; }
    if (hi < 0) hi -= 6;
    {
      unsigned r = a - v - ((p & FC) ? 0 : 1);
      p &= ~(FC | FZ | FN | FV);
      if (r < 0x100) p |= FC;
    }
    a = ((hi << 4) | (lo & 0x0f)) & 0xff;
    setnz(a);
    return;
  }
  adc(v ^ 0xff);
}

static void cmp(unsigned char r, unsigned char v)
{
  unsigned t = r - v;
  p &= ~FC;
  if (r >= v) p |= FC;
  setnz(t & 0xff);
}

static int branch(int cond)
{
  signed char off = (signed char)rd(pc++);
  if (cond) { pc += off; return 1; }
  return 0;
}

static long irqperiod = 0;
static long nextirq = 0;
static int irqcount = 0;
static int irqlimit = 0;
static void (*irqhook)(void) = 0;
static int run(unsigned addr, long maxcycles);
static int run_slice(long until) { return run(pc, until - cycles); }

/* Execute until RTS from the top level (sp returns to start) */
static int run(unsigned addr, long maxcycles)
{
  long start = cycles;
  if (!c64mode)
  {
    push(0xff); push(0xfe); /* return to $ffff */
  }
  pc = addr;
  for (;;)
  {
    unsigned char op;
    unsigned ea = 0;
    if (pc == 0xffff) return 1;
    if ((c64mode) && (irqperiod) && (cycles >= nextirq) && (!(p & FI)))
    {
      /* KERNAL IRQ entry: push PC, P, then A, X, Y and jump via $0314 */
      nextirq += irqperiod;
      push(pc >> 8); push(pc & 0xff); push(p & ~FB);
      p |= FI;
      push(a); push(x); push(y);
      pc = mem[0x314] | (mem[0x315] << 8);
      irqcount++;
    }
    if ((c64mode) && (pc == 0xea31))
    {
      if (irqhook) irqhook();
      if ((irqlimit) && (irqcount >= irqlimit)) return 1;
    }
    if ((c64mode) && (cycles - start > maxcycles)) return 1;
    if (cycles - start > maxcycles)
    {
      fprintf(stderr, "error: timeout at $%04X\n", pc);
      return 0;
    }
    op = rd(pc++);
    cycles += 3;
    switch (op)
    {
#define IMM ea = pc++;
#define ZP ea = rd(pc++);
#define ZPX ea = (rd(pc++) + x) & 0xff;
#define ZPY ea = (rd(pc++) + y) & 0xff;
#define ABS ea = rd16(pc); pc += 2;
#define ABX ea = (rd16(pc) + x) & 0xffff; pc += 2;
#define ABY ea = (rd16(pc) + y) & 0xffff; pc += 2;
#define IZX ea = rd16zp((rd(pc++) + x) & 0xff);
#define IZY ea = (rd16zp(rd(pc++)) + y) & 0xffff;
      /* LDA */
      case 0xa9: IMM a = rd(ea); setnz(a); break;
      case 0xa5: ZP a = rd(ea); setnz(a); break;
      case 0xb5: ZPX a = rd(ea); setnz(a); break;
      case 0xad: ABS a = rd(ea); setnz(a); break;
      case 0xbd: ABX a = rd(ea); setnz(a); break;
      case 0xb9: ABY a = rd(ea); setnz(a); break;
      case 0xa1: IZX a = rd(ea); setnz(a); break;
      case 0xb1: IZY a = rd(ea); setnz(a); break;
      /* LDX */
      case 0xa2: IMM x = rd(ea); setnz(x); break;
      case 0xa6: ZP x = rd(ea); setnz(x); break;
      case 0xb6: ZPY x = rd(ea); setnz(x); break;
      case 0xae: ABS x = rd(ea); setnz(x); break;
      case 0xbe: ABY x = rd(ea); setnz(x); break;
      /* LDY */
      case 0xa0: IMM y = rd(ea); setnz(y); break;
      case 0xa4: ZP y = rd(ea); setnz(y); break;
      case 0xb4: ZPX y = rd(ea); setnz(y); break;
      case 0xac: ABS y = rd(ea); setnz(y); break;
      case 0xbc: ABX y = rd(ea); setnz(y); break;
      /* STA */
      case 0x85: ZP wr(ea, a); break;
      case 0x95: ZPX wr(ea, a); break;
      case 0x8d: ABS wr(ea, a); break;
      case 0x9d: ABX wr(ea, a); break;
      case 0x99: ABY wr(ea, a); break;
      case 0x81: IZX wr(ea, a); break;
      case 0x91: IZY wr(ea, a); break;
      /* STX/STY */
      case 0x86: ZP wr(ea, x); break;
      case 0x96: ZPY wr(ea, x); break;
      case 0x8e: ABS wr(ea, x); break;
      case 0x84: ZP wr(ea, y); break;
      case 0x94: ZPX wr(ea, y); break;
      case 0x8c: ABS wr(ea, y); break;
      /* Transfers */
      case 0xaa: x = a; setnz(x); break;
      case 0xa8: y = a; setnz(y); break;
      case 0x8a: a = x; setnz(a); break;
      case 0x98: a = y; setnz(a); break;
      case 0xba: x = sp; setnz(x); break;
      case 0x9a: sp = x; break;
      /* Stack */
      case 0x48: push(a); break;
      case 0x68: a = pop(); setnz(a); break;
      case 0x08: push(p | FB | 0x20); break;
      case 0x28: p = pop(); break;
      /* Logic */
#define ALU(base, OPER) \
      case base + 0x09: IMM OPER; break; \
      case base + 0x05: ZP OPER; break; \
      case base + 0x15: ZPX OPER; break; \
      case base + 0x0d: ABS OPER; break; \
      case base + 0x1d: ABX OPER; break; \
      case base + 0x19: ABY OPER; break; \
      case base + 0x01: IZX OPER; break; \
      case base + 0x11: IZY OPER; break;
      ALU(0x00, a |= rd(ea); setnz(a))
      ALU(0x20, a &= rd(ea); setnz(a))
      ALU(0x40, a ^= rd(ea); setnz(a))
      ALU(0x60, adc(rd(ea)))
      ALU(0xe0, sbc(rd(ea)))
      ALU(0xc0, cmp(a, rd(ea)))
      case 0xe0: IMM cmp(x, rd(ea)); break;
      case 0xe4: ZP cmp(x, rd(ea)); break;
      case 0xec: ABS cmp(x, rd(ea)); break;
      case 0xc0: IMM cmp(y, rd(ea)); break;
      case 0xc4: ZP cmp(y, rd(ea)); break;
      case 0xcc: ABS cmp(y, rd(ea)); break;
      case 0x24: ZP { unsigned char v = rd(ea); p &= ~(FZ | FN | FV); if (!(a & v)) p |= FZ; p |= v & (FN | FV); } break;
      case 0x2c: ABS { unsigned char v = rd(ea); p &= ~(FZ | FN | FV); if (!(a & v)) p |= FZ; p |= v & (FN | FV); } break;
      /* Inc/dec */
      case 0xe6: ZP { unsigned char v = rd(ea) + 1; wr(ea, v); setnz(v); } break;
      case 0xf6: ZPX { unsigned char v = rd(ea) + 1; wr(ea, v); setnz(v); } break;
      case 0xee: ABS { unsigned char v = rd(ea) + 1; wr(ea, v); setnz(v); } break;
      case 0xfe: ABX { unsigned char v = rd(ea) + 1; wr(ea, v); setnz(v); } break;
      case 0xc6: ZP { unsigned char v = rd(ea) - 1; wr(ea, v); setnz(v); } break;
      case 0xd6: ZPX { unsigned char v = rd(ea) - 1; wr(ea, v); setnz(v); } break;
      case 0xce: ABS { unsigned char v = rd(ea) - 1; wr(ea, v); setnz(v); } break;
      case 0xde: ABX { unsigned char v = rd(ea) - 1; wr(ea, v); setnz(v); } break;
      case 0xe8: x++; setnz(x); break;
      case 0xca: x--; setnz(x); break;
      case 0xc8: y++; setnz(y); break;
      case 0x88: y--; setnz(y); break;
      /* Shifts */
#define SHIFTOPS(base, OPER) \
      case base + 0x0a: { unsigned char v = a; OPER; a = v; setnz(a); } break; \
      case base + 0x06: ZP { unsigned char v = rd(ea); OPER; wr(ea, v); setnz(v); } break; \
      case base + 0x16: ZPX { unsigned char v = rd(ea); OPER; wr(ea, v); setnz(v); } break; \
      case base + 0x0e: ABS { unsigned char v = rd(ea); OPER; wr(ea, v); setnz(v); } break; \
      case base + 0x1e: ABX { unsigned char v = rd(ea); OPER; wr(ea, v); setnz(v); } break;
      SHIFTOPS(0x00, { int c = v >> 7; v <<= 1; p = (p & ~FC) | c; })
      SHIFTOPS(0x40, { int c = v & 1; v >>= 1; p = (p & ~FC) | c; })
      SHIFTOPS(0x20, { int c = v >> 7; v = (v << 1) | (p & FC); p = (p & ~FC) | c; })
      SHIFTOPS(0x60, { int c = v & 1; v = (v >> 1) | ((p & FC) << 7); p = (p & ~FC) | c; })
      /* Flags */
      case 0x18: p &= ~FC; break;
      case 0x38: p |= FC; break;
      case 0x58: p &= ~FI; break;
      case 0x78: p |= FI; break;
      case 0xb8: p &= ~FV; break;
      case 0xd8: p &= ~FD; break;
      case 0xf8: p |= FD; break;
      /* Branches */
      case 0x10: branch(!(p & FN)); break;
      case 0x30: branch(p & FN); break;
      case 0x50: branch(!(p & FV)); break;
      case 0x70: branch(p & FV); break;
      case 0x90: branch(!(p & FC)); break;
      case 0xb0: branch(p & FC); break;
      case 0xd0: branch(!(p & FZ)); break;
      case 0xf0: branch(p & FZ); break;
      /* Jumps */
      case 0x4c: pc = rd16(pc); break;
      case 0x6c: { unsigned t = rd16(pc); pc = rd(t) | (rd((t & 0xff00) | ((t + 1) & 0xff)) << 8); } break;
      case 0x20: { unsigned t = rd16(pc); pc += 1; push(pc >> 8); push(pc & 0xff); pc = t; } break;
      case 0x60: { unsigned lo = pop(); unsigned hi = pop(); pc = ((hi << 8) | lo) + 1; } break;
      case 0x40: p = pop(); { unsigned lo = pop(); unsigned hi = pop(); pc = (hi << 8) | lo; } break;
      case 0xea: break;
      default:
        fprintf(stderr, "error: illegal opcode $%02X at $%04X\n", op, pc - 1);
        return 0;
    }
  }
}

static unsigned v5addr(int cfg, int start, int k)
{
  static const int stdstart[7] = {0xd420, 0xd440, 0xd480, 0xd500, 0xd420, 0xd420, 0xd500};
  static const int step1[7] = {0x20, 0x40, 0x80, 0x100, 0x20, 0x20, 0x100};
  static const int step2[7] = {0x20, 0x40, 0x80, 0x100, 0x60, 0xe0, 0x300};
  unsigned a;
  int i;
  if (k == 0) return 0xd400;
  a = start ? start : stdstart[cfg];
  for (i = 2; i <= k; i++)
  {
    a += ((i - 1) & 1) ? step2[cfg] : step1[cfg];
    if ((a > 0xd7e0) && (a < 0xde00)) a = 0xde00;
  }
  return a;
}

static const char *keyscript = "";
static int keypos = 0;
static int keydelay = 0;

static void c64irqhook(void)
{
  int s, r;
  printf("%04d:", irqcount - 1);
  for (s = 0; s < numsids; s++)
  {
    printf(" ");
    for (r = 0; r < 25; r++) printf("%02x", sidregs[s][r]);
  }
  printf("\n");
}

static void dumpscreen(void)
{
  int r, c;
  fprintf(stderr, "border %d background %d d018 %02x\n", mem[0xd020] & 15, mem[0xd021] & 15, mem[0xd018]);
  if (getenv("SCREENDUMP"))
  {
    FILE *f = fopen(getenv("SCREENDUMP"), "wb");
    if (f)
    {
      fwrite(mem + 0x400, 1000, 1, f);
      fwrite(mem + 0xd800, 1000, 1, f);
      fputc(mem[0xd020] & 15, f);
      fputc(mem[0xd021] & 15, f);
      fwrite(mem, 65536, 1, f);
      fclose(f);
    }
  }
  for (r = 0; r < 25; r++)
  {
    char line[41];
    for (c = 0; c < 40; c++)
    {
      unsigned char sc = mem[0x400 + r * 40 + c] & 0x7f;
      char ch = '?';
      if (sc == 0) ch = '@';
      else if (sc < 27) ch = 'A' + sc - 1;
      else if ((sc >= 32) && (sc < 64)) ch = sc;
      else if (sc == 0x40) ch = '-';
      else if (sc == 0x5d) ch = '|';
      else if (sc == 0x20) ch = ' ';
      else ch = '#';
      line[c] = ch;
    }
    line[40] = 0;
    fprintf(stderr, "%s   ", line);
    for (c = 0; c < 40; c++) fprintf(stderr, "%x", mem[0xd800 + r * 40 + c] & 15);
    fprintf(stderr, "\n");
  }
}

/* Runs a runnable PRG: --c64 <irqs> <timerperiod> <keys> <sid1> ... */
static int c64main(int argc, char **argv, unsigned char *buf, int len)
{
  unsigned load = buf[0] | (buf[1] << 8);
  int i;
  c64mode = 1;
  memcpy(mem + load, buf + 2, len - 2);
  irqlimit = atoi(argv[3]);
  irqperiod = (argc > 4) ? strtol(argv[4], 0, 16) : 0x4cc7;
  keyscript = (argc > 5) ? argv[5] : "";
  numsids = 0;
  for (i = 6; i < argc; i++) sidbase[numsids++] = strtol(argv[i], 0, 16);
  if (!numsids) { numsids = 1; sidbase[0] = 0xd400; }
  /* KERNAL stubs */
  mem[0xea31] = 0x68; mem[0xea32] = 0xa8; mem[0xea33] = 0x68; mem[0xea34] = 0xaa; mem[0xea35] = 0x68; mem[0xea36] = 0x40;
  /* GETIN at $FFE4: jmp $FF00 handler */
  mem[0xffe4] = 0x4c; mem[0xffe5] = 0x00; mem[0xffe6] = 0xff;
  mem[0xff00] = 0xad; mem[0xff01] = 0xf0; mem[0xff02] = 0xfe; /* lda $fef0 */
  mem[0xff03] = 0xa2; mem[0xff04] = 0x00; /* ldx #0 */
  mem[0xff05] = 0x8e; mem[0xff06] = 0xf0; mem[0xff07] = 0xfe; /* stx $fef0 */
  mem[0xff08] = 0xc9; mem[0xff09] = 0x00; /* cmp #0 */
  mem[0xff0a] = 0x60;
  irqhook = c64irqhook;
  sp = 0xf6;
  p = 0x24;
  nextirq = 20000;
  /* main loop: feed a key every 300 irqs */
  {
    long maxc = (long)irqlimit * irqperiod + 5000000;
    int lastkeyirq = 0;
    pc = 0x080d;
    while (cycles < maxc)
    {
      /* run in slices so keys can be injected */
      long slice = cycles + 20000;
      int savelimit = irqlimit;
      (void)savelimit;
      if (!run_slice(slice)) return 2;
      if ((irqlimit) && (irqcount >= irqlimit)) break;
      if ((keyscript[keypos]) && (irqcount - lastkeyirq >= 300) && (!mem[0xfef0]))
      {
        char k = keyscript[keypos++];
        mem[0xfef0] = (k == 'R') ? 0x1d : ((k == 'L') ? 0x9d : (unsigned char)k);
        lastkeyirq = irqcount;
        fprintf(stderr, "key '%c' at irq %d\n", k, irqcount);
      }
    }
  }
  dumpscreen();
  fprintf(stderr, "%d irqs, %ld SID writes, %ld cycles\n", irqcount, writes, cycles);
  return 0;
}

int main(int argc, char **argv)
{
  FILE *f;
  static unsigned char buf[70000];
  int len, frames, subtune = 0, i, s, r;
  unsigned load, init, play, dataofs;
  int version;
  int prgmode = 0;
  unsigned prginit = 0;

  if (argc < 3)
  {
    fprintf(stderr, "usage: sidtrace file.sid frames [subtune] [--prg initaddr sid1 sid2 ...]\n");
    return 1;
  }
  f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  len = fread(buf, 1, sizeof buf, f);
  fclose(f);
  frames = atoi(argv[2]);
  if (argc > 3) subtune = atoi(argv[3]);
  if ((argc > 3) && (!strcmp(argv[2], "--c64")))
    return c64main(argc, argv, buf, len);
  if ((argc > 5) && (!strcmp(argv[4], "--prg")))
  {
    prgmode = 1;
    sscanf(argv[5], "%x", &prginit);
    numsids = 0;
    for (i = 6; i < argc; i++) sscanf(argv[i], "%x", &sidbase[numsids++]);
    if (!numsids) { numsids = 1; sidbase[0] = 0xd400; }
  }

  if (prgmode)
  {
    load = buf[0] | (buf[1] << 8);
    memcpy(mem + load, buf + 2, len - 2);
    init = prginit;
    play = prginit + 3;
    version = 0;
  }
  else
  {
    if (memcmp(buf, "PSID", 4) && memcmp(buf, "RSID", 4)) { fprintf(stderr, "not a SID file\n"); return 1; }
    version = buf[5];
    dataofs = (buf[6] << 8) | buf[7];
    load = (buf[8] << 8) | buf[9];
    init = (buf[10] << 8) | buf[11];
    play = (buf[12] << 8) | buf[13];
    if (!load)
    {
      load = buf[dataofs] | (buf[dataofs + 1] << 8);
      dataofs += 2;
    }
    memcpy(mem + load, buf + dataofs, len - dataofs);
    sidbase[0] = 0xd400;
    numsids = 1;
    if (version >= 3)
    {
      if ((version == 3 || version == 4))
      {
        if (buf[0x7a]) { sidbase[1] = buf[0x7a] * 16 + 0xd000; numsids = 2; }
        if ((version == 4) && buf[0x7b]) { sidbase[2] = buf[0x7b] * 16 + 0xd000; numsids = 3; }
      }
      else if (version >= 5)
      {
        int count = buf[0x7a] & 0x0f;
        int cfg = buf[0x7a] >> 4;
        int st = buf[0x7b] ? buf[0x7b] * 16 + 0xd000 : 0;
        if (st >= 0xd000 && buf[0x7b] >= 0xe0) st = buf[0x7b] * 16 + 0xd000; /* $DE00-$DFE0 */
        numsids = count ? count : 1;
        for (i = 0; i < numsids; i++) sidbase[i] = v5addr(cfg, st, i);
      }
    }
    fprintf(stderr, "PSID v%d load $%04X init $%04X play $%04X songs %d, %d SID(s):", version, load, init, play, (buf[14] << 8) | buf[15], numsids);
    for (i = 0; i < numsids; i++) fprintf(stderr, " $%04X", sidbase[i]);
    fprintf(stderr, "\n");
  }

  sp = 0xff;
  a = subtune; x = 0; y = 0; p = 0x24;
  if (!run(init, 1000000)) return 2;
  for (i = 0; i < frames; i++)
  {
    long c0 = cycles;
    a = 0; x = 0; y = 0;
    if (!run(play, 200000)) return 2;
    printf("%04d:", i);
    for (s = 0; s < numsids; s++)
    {
      printf(" ");
      for (r = 0; r < 25; r++) printf("%02x", sidregs[s][r]);
    }
    printf("\n");
    if (cycles - c0 > maxframe) maxframe = cycles - c0;
  }
  fprintf(stderr, "%ld SID writes, %ld cycles, max %ld instructions per play call (~%ld cycles)\n", writes, cycles, maxframe / 3, maxframe / 3 * 4);
  return 0;
}

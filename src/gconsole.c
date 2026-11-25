/*
 * C64 GrooveGoat "console" output routines
 *
 * Character cell screen like in GoatTracker, but rendered into a 32-bit
 * framebuffer with anti-aliased fonts, colour schemes with rounded buttons
 * and panels, and HiDPI support. The framebuffer is scaled to the window.
 */

#define GCONSOLE_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "goattrk2.h"
#include "gres.h"
#include "gpiano.h"

int gfxinitted = 0;
unsigned *scrbuffer = NULL;
unsigned *prevscrbuffer = NULL;
int key = 0;
int rawkey = 0;
int symkey = 0;
int shiftpressed = 0;
int shiftonlypressed = 0;
int ctrlpressed = 0;
int altpressed = 0;
int metapressed = 0;
unsigned kbdlayout = 0;   // 0 = EN (QWERTY), 1 = DE (QWERTZ)
int cursorflashdelay = 0;
int mouseb = 0;
int prevmouseb = 0;
int mousex = 0;
int mousey = 0;
int mousepx = 0;
int mousepy = 0;
int mousewheel = 0;
int mouseheld = 0;
int theme = THEME_DEFAULT;
unsigned bigwindow = 1;

extern int plat_dstx, plat_dsty, plat_dstw, plat_dsth, plat_fbw, plat_fbh;

static SDL_Texture *texture = NULL;
static Uint32 *fb = NULL;
static int scale = 0;
static int fbw = 0, fbh = 0;
static int fulldirty = 1;
static Uint32 palette[32];
static int cursorbg = 2;
static int cursorfg = 0;

// ---------------------------------------------------------------------------
// Colour schemes (1.4): a modern look with rounded buttons and panels.
// Every scheme has the 32 screen colours (0-15 keep the meaning of the
// original GoatTracker colours, see gconsole.h for 16-31) and the colours of
// the piano keyboard.
// ---------------------------------------------------------------------------

typedef struct
{
  const char *name;
  int light;
  int group;            // the menu: 0 light, 1 high contrast, 2 dark
  Uint32 pal[32];
  PIANOCOLORS piano;
} THEMEDEF;

static const THEMEDEF themes[NUM_THEMES] = {
  {"Paper", 1, 0,
   {0xe6e9ee, 0xdbeafe, 0x2563eb, 0xa3abb8, 0xdc2626, 0xc2410c, 0xc2410c, 0x5f6879, 0x2b3240, 0x1d4ed8, 0x15803d, 0x1d4ed8, 0xdc2626, 0x7e22ce, 0xc2410c, 0x0b1220, 0xffffff, 0xf1f3f6, 0x2b3240, 0xe7eaee, 0x1f2937, 0x2563eb, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xcfd5de, 0xf6f7f9, 0xfde4e1, 0xd8e3fb, 0xdbeafe},
   {0x2b3240, 0xfbfcfe, 0xd6d8dc, 0x1a1f29, 0x4c5058, 0x898d96, 0x5f6879, {0xdc2626, 0xc2410c, 0x2563eb}}},
  {"Graphite", 0, 2,
   {0x121418, 0x24464d, 0x3ddbd9, 0x5f6774, 0xff6b6b, 0xffb454, 0xffb454, 0x8a93a1, 0xcfd5de, 0x8fb8ff, 0x7ee787, 0x8fb8ff, 0xff6b6b, 0xd2a8ff, 0xffb454, 0xffffff, 0x1b1e24, 0x22262e, 0xcfd5de, 0x262a32, 0xdde3ea, 0x3ddbd9, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x2c313a, 0x1f2228, 0x3a242a, 0x214045, 0x24464d},
   {0x0b0c10, 0xe9ecf1, 0xc1c4c8, 0x14161a, 0x48494c, 0x6f7175, 0x919293, {0xff6b6b, 0xffb454, 0x3ddbd9}}},
  {"Lavender", 1, 0,
   {0xecebf6, 0xe4dcfd, 0x7c3aed, 0xa9a4c7, 0xe11d48, 0xdb2777, 0xdb2777, 0x6a6491, 0x2e2a4a, 0x6d28d9, 0x059669, 0x6d28d9, 0xe11d48, 0x0e7490, 0xdb2777, 0x17132e, 0xffffff, 0xf3f1fb, 0x2e2a4a, 0xeae8f6, 0x2e2a4a, 0x7c3aed, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xd6d2ec, 0xf7f6fc, 0xfde4ec, 0xe7dcfc, 0xe4dcfd},
   {0x2e2a4a, 0xffffff, 0xd9d9de, 0x1d1934, 0x4f4c61, 0x8c8a9b, 0x6a6491, {0xe11d48, 0xdb2777, 0x7c3aed}}},
  {"Sand", 1, 0,
   {0xefe8dc, 0xf5e1c0, 0xb45309, 0xb0a38e, 0xc2410c, 0x0f766e, 0x0f766e, 0x7a6c58, 0x3d3428, 0x0f766e, 0x4d7c0f, 0x0f766e, 0xc2410c, 0x7c2d12, 0x0f766e, 0x1f1a12, 0xfbf7f0, 0xf3ece0, 0x3d3428, 0xece4d8, 0x3d3428, 0xb45309, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xddd2c0, 0xf7f1e7, 0xf9dcd0, 0xeed9c6, 0xf5e1c0},
   {0x3d3428, 0xfffdf8, 0xdcd9d3, 0x2a2218, 0x59534b, 0x948e86, 0x7a6c58, {0xc2410c, 0x0f766e, 0xb45309}}},
  {"Frost", 0, 2,
   {0x242933, 0x4c566a, 0x88c0d0, 0x69758b, 0xbf616a, 0xebcb8b, 0xebcb8b, 0x9aa5b8, 0xd8dee9, 0x81a1c1, 0xa3be8c, 0x81a1c1, 0xbf616a, 0xb48ead, 0xebcb8b, 0xeceff4, 0x2e3440, 0x3b4252, 0xd8dee9, 0x3b4252, 0xe5e9f0, 0x88c0d0, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x434c5e, 0x323846, 0x4a3a44, 0x3e4d5a, 0x4c566a},
   {0x1b1f27, 0xeceff4, 0xc6cacf, 0x242933, 0x545860, 0x797d83, 0x989a9e, {0xbf616a, 0xebcb8b, 0x88c0d0}}},
  {"Ocean", 0, 2,
   {0x0b1d26, 0x1f5a66, 0x2ec4b6, 0x4f7a8a, 0xff6b6b, 0xffbf69, 0xffbf69, 0x86aebb, 0xcde7ef, 0x7fd1ff, 0x7ae582, 0x7fd1ff, 0xff6b6b, 0xb8a1ff, 0xffbf69, 0xffffff, 0x102a36, 0x163746, 0xcde7ef, 0x173a4a, 0xe3f4f8, 0x2ec4b6, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x1f4657, 0x0f2632, 0x3a2a34, 0x15464d, 0x1f5a66},
   {0x061218, 0xe8f6f9, 0xbfcdd0, 0x0b1d26, 0x414f56, 0x6c797d, 0x8f9497, {0xff6b6b, 0xffbf69, 0x2ec4b6}}},
  {"Forest", 0, 2,
   {0x141b16, 0x34503a, 0x8bc34a, 0x61715f, 0xe76f51, 0xe9c46a, 0xe9c46a, 0x94a690, 0xd6e2d4, 0xa3d9a5, 0x8bc34a, 0xa3d9a5, 0xe76f51, 0xcdb4db, 0xe9c46a, 0xf4faf2, 0x1b241e, 0x232e26, 0xd6e2d4, 0x25302a, 0xe6efe3, 0x8bc34a, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x2f3c33, 0x18201a, 0x40302a, 0x2f4126, 0x34503a},
   {0x0c110d, 0xeef4ec, 0xc5cbc4, 0x141b16, 0x484d49, 0x727771, 0x929492, {0xe76f51, 0xe9c46a, 0x8bc34a}}},
  {"Neon", 0, 2,
   {0x0b0a18, 0x3a2f80, 0xff4fd8, 0x5a5590, 0xff5c7a, 0x29e7ff, 0x29e7ff, 0x8f88c9, 0xd8d4ff, 0x29e7ff, 0x3dffa2, 0x29e7ff, 0xff5c7a, 0xffe156, 0x29e7ff, 0xffffff, 0x14112b, 0x1d1840, 0xd8d4ff, 0x211c48, 0xf1edff, 0xff4fd8, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x2a2458, 0x18143a, 0x4a1846, 0x3e1c4a, 0x3a2f80},
   {0x05040d, 0xe8e4ff, 0xbfbcd3, 0x0b0a18, 0x41404b, 0x6b697a, 0x8e8e92, {0xff5c7a, 0x29e7ff, 0xff4fd8}}},
  {"Sunset", 0, 2,
   {0x1a1416, 0x5a3a33, 0xff8a5b, 0x7d6a66, 0xef476f, 0xffd166, 0xffd166, 0xb39d95, 0xf1dfd6, 0xffb38a, 0x06d6a0, 0xffb38a, 0xef476f, 0xc9a0ff, 0xffd166, 0xfff7f0, 0x241b1e, 0x2e2327, 0xf1dfd6, 0x30252a, 0xf7e9e1, 0xff8a5b, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x3d2e33, 0x211a1c, 0x4a2630, 0x4b2f29, 0x5a3a33},
   {0x100c0d, 0xfbeee6, 0xd1c5bf, 0x1a1416, 0x4c4849, 0x7a726f, 0x939292, {0xef476f, 0xffd166, 0xff8a5b}}},
  {"Amber", 0, 2,
   {0x0f0b05, 0x4a3410, 0xffb000, 0x7a5a26, 0xff5a36, 0xff7a00, 0xff7a00, 0xb8893f, 0xffcf80, 0xffc04d, 0xffd000, 0xffc04d, 0xff5a36, 0xff9a3c, 0xff7a00, 0xffe2a8, 0x17110a, 0x21180d, 0xffcf80, 0x251a0c, 0xffd893, 0xffb000, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x33250f, 0x140e07, 0x3d1f0a, 0x412e08, 0x4a3410},
   {0x080603, 0xffe9bf, 0xd3c09d, 0x17110a, 0x4a4540, 0x776c58, 0x908f8e, {0xff5a36, 0xff7a00, 0xffb000}}},
  {"C64 Modern", 0, 2,
   {0x1d1a3d, 0x4b44a8, 0xa8f0a0, 0x6d66b8, 0xff8f80, 0xf2f07a, 0xf2f07a, 0x9a93e0, 0xc9c4ff, 0x78d8e4, 0xa8f0a0, 0x78d8e4, 0xff8f80, 0xd37be0, 0xf2f07a, 0xffffff, 0x272358, 0x312c6b, 0xc9c4ff, 0x353078, 0xf4f2ff, 0xa8f0a0, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x3d3780, 0x2b275f, 0x5a2f6b, 0x3e4865, 0x4b44a8},
   {0x13102c, 0xebe9ff, 0xc4c2d9, 0x1d1a3d, 0x4f4c68, 0x74728b, 0x9593a0, {0xff8f80, 0xf2f07a, 0xa8f0a0}}},
  {"Mono", 0, 1,
   {0x000000, 0x3a3a3a, 0xffffff, 0x666666, 0xff4d4d, 0xffd400, 0xffd400, 0xa0a0a0, 0xe6e6e6, 0xbdbdbd, 0x4dff88, 0xbdbdbd, 0xff4d4d, 0x9e9e9e, 0xffd400, 0xffffff, 0x0d0d0d, 0x1a1a1a, 0xe6e6e6, 0x1f1f1f, 0xffffff, 0xffffff, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x333333, 0x0a0a0a, 0x3a1414, 0x393939, 0x3a3a3a},
   {0x000000, 0xf2f2f2, 0xc6c6c6, 0x000000, 0x383838, 0x6d6d6d, 0x8c8c8c, {0xff4d4d, 0xffd400, 0xffffff}}},
  {"Sky", 1, 0,
   {0xe3eef8, 0xd6ecfa, 0x0284c7, 0x9fb3c8, 0xdc2626, 0xd97706, 0xd97706, 0x50698a, 0x1e3a5f, 0x0369a1, 0x16a34a, 0x0369a1, 0xdc2626, 0x7c3aed, 0xd97706, 0x0c1a2e, 0xffffff, 0xeef5fc, 0x1e3a5f, 0xe2ecf4, 0x1e3a5f, 0x0284c7, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xc5d8ea, 0xf4f8fc, 0xfde2e2, 0xd1e9f5, 0xd6ecfa},
   {0x1e3a5f, 0xffffff, 0xd7dce2, 0x13263f, 0x475669, 0x8393a7, 0x50698a, {0xdc2626, 0xd97706, 0x0284c7}}},
  {"Mint", 1, 0,
   {0xe2f1ea, 0xd1f2e3, 0x059669, 0x9dbcae, 0xdc2626, 0xc2410c, 0xc2410c, 0x4f7466, 0x1f3d33, 0x047857, 0x15803d, 0x047857, 0xdc2626, 0x7c3aed, 0xc2410c, 0x0d2219, 0xffffff, 0xeef8f3, 0x1f3d33, 0xe0efe8, 0x1f3d33, 0x059669, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xc2dfd1, 0xf4faf7, 0xfde4e1, 0xd2ece4, 0xd1f2e3},
   {0x1f3d33, 0xffffff, 0xd7dcda, 0x13261f, 0x475650, 0x84948f, 0x4f7466, {0xdc2626, 0xc2410c, 0x059669}}},
  {"Rose", 1, 0,
   {0xf6e6ea, 0xfce0ec, 0xdb2777, 0xc4a0aa, 0xdc2626, 0x7c3aed, 0x7c3aed, 0x85525f, 0x4a1f2c, 0xbe185d, 0x059669, 0xbe185d, 0xdc2626, 0x0e7490, 0x7c3aed, 0x2a0f18, 0xffffff, 0xfcf1f4, 0x4a1f2c, 0xf5e5ea, 0x4a1f2c, 0xdb2777, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xebcbd4, 0xfdf6f8, 0xfde0d8, 0xf9d8e7, 0xfce0ec},
   {0x4a1f2c, 0xffffff, 0xded7d9, 0x2e121b, 0x5c464d, 0x9b848b, 0x85525f, {0xdc2626, 0x7c3aed, 0xdb2777}}},
  {"Peach", 1, 0,
   {0xf8e9df, 0xfde1cd, 0xea580c, 0xc6a48e, 0xdc2626, 0x0f766e, 0x0f766e, 0x87604a, 0x4a2a1a, 0xc2410c, 0x15803d, 0xc2410c, 0xdc2626, 0x7c3aed, 0x0f766e, 0x2a160b, 0xfffaf6, 0xfcf1ea, 0x4a2a1a, 0xf6e6dc, 0x4a2a1a, 0xea580c, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xecd2c1, 0xfdf5ef, 0xfbd9d3, 0xfbddcc, 0xfde1cd},
   {0x4a2a1a, 0xfffdfb, 0xded7d3, 0x2e1a10, 0x5c4c45, 0x9b897f, 0x87604a, {0xdc2626, 0x0f766e, 0xea580c}}},
  {"Lemon", 1, 0,
   {0xf5f2d6, 0xfbf0b8, 0xa16207, 0xb5ae78, 0xdc2626, 0x1d4ed8, 0x1d4ed8, 0x736c38, 0x3a3510, 0x854d0e, 0x15803d, 0x854d0e, 0xdc2626, 0x7e22ce, 0x1d4ed8, 0x1f1c05, 0xfffef5, 0xfaf8e6, 0x3a3510, 0xf2eed2, 0x3a3510, 0xa16207, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xe4dfae, 0xfdfcf0, 0xfbdcd3, 0xeee2ca, 0xfbf0b8},
   {0x3a3510, 0xfffff8, 0xdcdbce, 0x24210a, 0x545240, 0x939078, 0x736c38, {0xdc2626, 0x1d4ed8, 0xa16207}}},
  {"Latte", 1, 0,
   {0xe6e9ef, 0xe0d4fb, 0x8839ef, 0x9ca0b0, 0xd20f39, 0xfe640b, 0xfe640b, 0x6c6f85, 0x4c4f69, 0x1e66f5, 0x40a02b, 0x1e66f5, 0xd20f39, 0x179299, 0xfe640b, 0x2c2e40, 0xeff1f5, 0xe9ecf2, 0x4c4f69, 0xdee0e8, 0x4c4f69, 0x8839ef, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xccd0da, 0xf5f6f9, 0xf9dade, 0xdcd0f4, 0xe0d4fb},
   {0x4c4f69, 0xffffff, 0xdfdfe4, 0x2c2e40, 0x5a5c6a, 0x9d9eac, 0x6c6f85, {0xd20f39, 0xfe640b, 0x8839ef}}},
  {"Solar Light", 1, 0,
   {0xeee8d5, 0xdde9ec, 0x268bd2, 0x93a1a1, 0xdc322f, 0xcb4b16, 0xcb4b16, 0x657b83, 0x586e75, 0x268bd2, 0x859900, 0x268bd2, 0xdc322f, 0x6c71c4, 0xcb4b16, 0x073642, 0xfdf6e3, 0xf5efdc, 0x586e75, 0xede6d2, 0x586e75, 0x268bd2, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0xddd6c1, 0xf7f1de, 0xf6dcd2, 0xd6e3e0, 0xdde9ec},
   {0x073642, 0xfffcf2, 0xd2d8d2, 0x002b36, 0x385a62, 0x778f91, 0x657b83, {0xdc322f, 0xcb4b16, 0x268bd2}}},
  {"HC Light", 1, 1,
   {0xd4d4d4, 0xffe14d, 0x0038b8, 0x595959, 0xc00000, 0xa33a00, 0xa33a00, 0x2e2e2e, 0x000000, 0x0033aa, 0x006400, 0x0033aa, 0xc00000, 0x6a00a8, 0xa33a00, 0x000000, 0xffffff, 0xececec, 0x000000, 0xb6b6b6, 0x000000, 0x0038b8, 0xe2231a, 0xf7941d, 0xf2c500, 0x39b54a, 0x00aeef, 0x6e6e6e, 0xf0f0f0, 0xffd0d0, 0xd1dbf2, 0xffe14d},
   {0x000000, 0xffffff, 0xd1d1d1, 0x000000, 0x383838, 0x737373, 0x2e2e2e, {0xc00000, 0xa33a00, 0x0038b8}}},
  {"HC Dark", 0, 1,
   {0x000000, 0x004c6b, 0x00e5ff, 0xb0b0b0, 0xff4848, 0xffd400, 0xffd400, 0xd6d6d6, 0xffffff, 0x00e5ff, 0x00ff66, 0x00e5ff, 0xff4848, 0xff7bff, 0xffd400, 0xffffff, 0x141414, 0x202020, 0xffffff, 0x2c2c2c, 0xffffff, 0x00e5ff, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x8a8a8a, 0x1c1c1c, 0x5a0000, 0x103a3e, 0x004c6b},
   {0x3a3a3a, 0xffffff, 0xdcdcdc, 0x000000, 0x383838, 0x939393, 0xa6a6a6, {0xff4848, 0xffd400, 0x00e5ff}}},
  {"HC Yellow", 0, 1,
   {0x000000, 0x00456b, 0x00ffff, 0xb8b800, 0xff5050, 0xffffff, 0xffffff, 0xe6e600, 0xffff00, 0x00ffff, 0x00ff00, 0x00ffff, 0xff5050, 0xff80ff, 0xffffff, 0xffffff, 0x0e0e00, 0x1c1c00, 0xffff00, 0x2a2a00, 0xffff00, 0x00ffff, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x8c8c00, 0x161600, 0x550000, 0x0b392e, 0x00456b},
   {0x505000, 0xffffcc, 0xe0e0a7, 0x000000, 0x383838, 0x9f9f5c, 0xb0b08c, {0xff5050, 0xffffff, 0x00ffff}}},
  {"Midnight", 0, 2,
   {0x0d1321, 0x233b6b, 0x5b9dff, 0x5d6f99, 0xff6b81, 0xffb86b, 0xffb86b, 0x8fa0c8, 0xd6e2ff, 0x7fb4ff, 0x7ee8a2, 0x7fb4ff, 0xff6b81, 0xc792ea, 0xffb86b, 0xffffff, 0x141c2e, 0x1b2540, 0xd6e2ff, 0x1e2a48, 0xe3ecff, 0x5b9dff, 0xe2231a, 0xf7941d, 0xffde17, 0x39b54a, 0x00aeef, 0x27355a, 0x18213a, 0x3d2236, 0x213354, 0x233b6b},
   {0x070b14, 0xe8eeff, 0xc0c5d5, 0x0d1321, 0x424752, 0x6c717e, 0x8f9195, {0xff6b81, 0xffb86b, 0x5b9dff}}},
};

const char *theme_name(int t)
{
  if ((t < 0) || (t >= NUM_THEMES)) t = 0;
  return themes[t].name;
}

int theme_group(int t)
{
  if ((t < 0) || (t >= NUM_THEMES)) t = 0;
  return themes[t].group;
}

int theme_islight(void)
{
  return themes[theme].light;
}

const PIANOCOLORS *theme_piano(void)
{
  return &themes[theme].piano;
}

// a colour of a scheme as 0xRRGGBB (the swatches of the scheme menu)
unsigned theme_color(int t, int idx)
{
  if ((t < 0) || (t >= NUM_THEMES)) t = 0;
  return themes[t].pal[idx & 31];
}

void settheme(int t)
{
  int c;

  if ((t < 0) || (t >= NUM_THEMES)) t = THEME_DEFAULT;
  theme = t;
  for (c = 0; c < 32; c++) palette[c] = 0xff000000 | themes[t].pal[c];
  if (getenv("GG_PALETTE"))
  {
    // a test palette: 32 hex colours (concepts for new looks)
    FILE *f = fopen(getenv("GG_PALETTE"), "rt");
    unsigned v;
    if (f)
    {
      for (c = 0; (c < 32) && (fscanf(f, "%x", &v) == 1); c++) palette[c] = 0xff000000 | v;
      fclose(f);
    }
  }
  cursorbg = 2;
  cursorfg = 16;
  fulldirty = 1;
}

// ---------------------------------------------------------------------------
// Image tiles (glyph codes >= 256)
// ---------------------------------------------------------------------------

#define MAX_TILEIMAGES 16
#define FIRSTTILE 256

typedef struct
{
  char name[32];
  int cw, ch;          // size in cells
  int firstglyph;
  Uint32 *pixels;      // RGBA (A in top byte) at current scale
  int pw, ph;
} TILEIMAGE;

static TILEIMAGE tileimages[MAX_TILEIMAGES];
static int numtileimages = 0;
static int nexttile = FIRSTTILE;

// Render an image resource scaled (area average) into the tile pixel buffer
static void buildtileimage(TILEIMAGE *t)
{
  const RESIMAGE *src = NULL;
  const RESIMAGE *i;
  int w = t->cw * CELLW * scale;
  int h = t->ch * CELLH * scale;
  int side = w < h ? w : h;
  int ox, oy, dw, dh;
  int x, y;
  char base[32];

  // Images are named like "logo" and stored in several sizes
  strcpy(base, t->name);
  for (i = res_images; i->name; i++)
  {
    if (!strncmp(i->name, base, strlen(base)))
    {
      if ((!src) || ((src->w < side) && (i->w > src->w)) || ((i->w >= side) && (i->w < src->w)))
        src = i;
    }
  }
  free(t->pixels);
  t->pixels = (Uint32 *)calloc(w * h, sizeof(Uint32));
  t->pw = w;
  t->ph = h;
  if ((!src) || (!t->pixels)) return;
  // fit into the cells with the aspect ratio of the picture
  dw = w;
  dh = (int)((double)w * src->h / src->w + 0.5);
  if (dh > h)
  {
    dh = h;
    dw = (int)((double)h * src->w / src->h + 0.5);
  }
  ox = (w - dw) / 2;
  oy = (h - dh) / 2;
  (void)side;

  for (y = 0; y < dh; y++)
  {
    for (x = 0; x < dw; x++)
    {
      // box filter from source
      int sx0 = x * src->w / dw;
      int sx1 = (x + 1) * src->w / dw;
      int sy0 = y * src->h / dh;
      int sy1 = (y + 1) * src->h / dh;
      int sx, sy, n = 0;
      unsigned r = 0, g = 0, b = 0, a = 0;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sy1 <= sy0) sy1 = sy0 + 1;
      for (sy = sy0; sy < sy1; sy++)
      {
        for (sx = sx0; sx < sx1; sx++)
        {
          const unsigned char *p = src->rgba + (sy * src->w + sx) * 4;
          r += p[0] * p[3];
          g += p[1] * p[3];
          b += p[2] * p[3];
          a += p[3];
          n++;
        }
      }
      if (a)
      {
        r /= a;
        g /= a;
        b /= a;
      }
      a /= n;
      t->pixels[(y + oy) * w + (x + ox)] = (a << 24) | (r << 16) | (g << 8) | b;
    }
  }
}

static TILEIMAGE *gettileimage(const char *name, int cw, int ch)
{
  int c;
  for (c = 0; c < numtileimages; c++)
  {
    if ((!strcmp(tileimages[c].name, name)) && (tileimages[c].cw == cw) && (tileimages[c].ch == ch))
      return &tileimages[c];
  }
  if (numtileimages >= MAX_TILEIMAGES) return NULL;
  {
    TILEIMAGE *t = &tileimages[numtileimages++];
    memset(t, 0, sizeof(TILEIMAGE));
    strncpy(t->name, name, sizeof t->name - 1);
    t->cw = cw;
    t->ch = ch;
    t->firstglyph = nexttile;
    nexttile += cw * ch;
    if (scale) buildtileimage(t);
    return t;
  }
}

static TILEIMAGE *findtile(int glyph, int *tx, int *ty)
{
  int c;
  for (c = 0; c < numtileimages; c++)
  {
    TILEIMAGE *t = &tileimages[c];
    if ((glyph >= t->firstglyph) && (glyph < t->firstglyph + t->cw * t->ch))
    {
      int idx = glyph - t->firstglyph;
      *tx = idx % t->cw;
      *ty = idx / t->cw;
      return t;
    }
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// Canvases: small pixel graphics (alpha masks at 16x32 pixels per cell) that
// are drawn with the colours of the cells they cover, e.g. the ADSR graph
// ---------------------------------------------------------------------------

#define CANVAS_BASE 0xe000

static inline void setcharcolor(unsigned *dptr, int ch, int color);
#define CANVAS_CELLS 512

static unsigned char canvasbuf[MAX_CANVAS][CANVAS_MAXCH * 32 * CANVAS_MAXCW * 16];
static unsigned char canvasprev[MAX_CANVAS][CANVAS_MAXCH * 32 * CANVAS_MAXCW * 16];
static int canvascw[MAX_CANVAS];
static int canvasch[MAX_CANVAS];

unsigned char *canvas_begin(int id, int cw, int ch)
{
  if ((id < 0) || (id >= MAX_CANVAS)) return NULL;
  if (cw > CANVAS_MAXCW) cw = CANVAS_MAXCW;
  if (ch > CANVAS_MAXCH) ch = CANVAS_MAXCH;
  canvascw[id] = cw;
  canvasch[id] = ch;
  memset(canvasbuf[id], 0, sizeof canvasbuf[id]);
  return canvasbuf[id];
}

void canvas_draw(int id, int x, int y, int color)
{
  int cx, cy;
  int changed;
  if ((id < 0) || (id >= MAX_CANVAS) || (!scrbuffer)) return;
  changed = memcmp(canvasbuf[id], canvasprev[id], sizeof canvasbuf[id]) != 0;
  if (changed) memcpy(canvasprev[id], canvasbuf[id], sizeof canvasbuf[id]);
  for (cy = 0; cy < canvasch[id]; cy++)
  {
    for (cx = 0; cx < canvascw[id]; cx++)
    {
      int idx;
      if ((x + cx < 0) || (x + cx >= MAX_COLUMNS) || (y + cy < 0) || (y + cy >= MAX_ROWS)) continue;
      idx = (x + cx) + (y + cy) * MAX_COLUMNS;
      setcharcolor(scrbuffer + idx, CANVAS_BASE + id * CANVAS_CELLS + cy * canvascw[id] + cx, color);
      if ((changed) && (prevscrbuffer)) prevscrbuffer[idx] = 0xffffffff;
    }
  }
}

// ---------------------------------------------------------------------------
// Full colour canvas
// ---------------------------------------------------------------------------

#define RGBCANVAS_BASE 0xd000

static unsigned rgbbuf[RGBCANVAS_MAXCH * 32 * RGBCANVAS_STRIDE];
static unsigned rgbprev[RGBCANVAS_MAXCH * 32 * RGBCANVAS_STRIDE];
static int rgbcw = 1, rgbch = 1;

unsigned *rgbcanvas_begin(int cw, int ch)
{
  if (cw > RGBCANVAS_MAXCW) cw = RGBCANVAS_MAXCW;
  if (ch > RGBCANVAS_MAXCH) ch = RGBCANVAS_MAXCH;
  if (cw < 1) cw = 1;
  if (ch < 1) ch = 1;
  rgbcw = cw;
  rgbch = ch;
  return rgbbuf;
}

void rgbcanvas_draw(int x, int y)
{
  int cx, cy;
  int changed;
  if (!scrbuffer) return;
  changed = memcmp(rgbbuf, rgbprev, rgbch * 32 * RGBCANVAS_STRIDE * sizeof(unsigned)) != 0;
  if (changed) memcpy(rgbprev, rgbbuf, rgbch * 32 * RGBCANVAS_STRIDE * sizeof(unsigned));
  for (cy = 0; cy < rgbch; cy++)
  {
    for (cx = 0; cx < rgbcw; cx++)
    {
      int idx;
      if ((x + cx < 0) || (x + cx >= MAX_COLUMNS) || (y + cy < 0) || (y + cy >= MAX_ROWS)) continue;
      idx = (x + cx) + (y + cy) * MAX_COLUMNS;
      scrbuffer[idx] = RGBCANVAS_BASE + cy * rgbcw + cx;
      if ((changed) && (prevscrbuffer)) prevscrbuffer[idx] = 0xffffffff;
    }
  }
}

// ---------------------------------------------------------------------------
// Effect canvases: full colour pixels (16x32 per cell) for animations that
// change every frame (the fireworks of the version info); two of them
// ---------------------------------------------------------------------------

#define FXCANVAS_BASE 0xd400
#define FXCANVAS_CELLS 1536

static unsigned *fxbuf[2];
static int fxcw[2], fxch[2];

unsigned *fxcanvas_begin(int id, int cw, int ch)
{
  if ((id < 0) || (id > 1)) return NULL;
  if (cw < 1) cw = 1;
  if (ch < 1) ch = 1;
  while (cw * ch > FXCANVAS_CELLS) ch--;
  if ((!fxbuf[id]) || (fxcw[id] != cw) || (fxch[id] != ch))
  {
    free(fxbuf[id]);
    fxbuf[id] = (unsigned *)calloc((size_t)cw * 16 * ch * 32, sizeof(unsigned));
    fxcw[id] = cw;
    fxch[id] = ch;
  }
  return fxbuf[id];
}

void fxcanvas_end(void)
{
  int k;
  for (k = 0; k < 2; k++)
  {
    free(fxbuf[k]);
    fxbuf[k] = NULL;
    fxcw[k] = fxch[k] = 0;
  }
}

void fxcanvas_draw(int id, int x, int y)
{
  int cx, cy;
  if ((id < 0) || (id > 1) || (!fxbuf[id]) || (!scrbuffer)) return;
  for (cy = 0; cy < fxch[id]; cy++)
    for (cx = 0; cx < fxcw[id]; cx++)
    {
      int idx;
      if ((x + cx < 0) || (x + cx >= MAX_COLUMNS) || (y + cy < 0) || (y + cy >= MAX_ROWS)) continue;
      idx = (x + cx) + (y + cy) * MAX_COLUMNS;
      scrbuffer[idx] = FXCANVAS_BASE + id * FXCANVAS_CELLS + cy * fxcw[id] + cx;
      if (prevscrbuffer) prevscrbuffer[idx] = 0xffffffff;     // changes every frame
    }
}

void drawimage(int x, int y, const char *name, int cellsw, int cellsh)
{
  TILEIMAGE *t = gettileimage(name, cellsw, cellsh);
  int cx, cy;
  if ((!t) || (!scrbuffer)) return;
  for (cy = 0; cy < cellsh; cy++)
  {
    for (cx = 0; cx < cellsw; cx++)
    {
      unsigned *dptr;
      if ((x + cx < 0) || (x + cx >= MAX_COLUMNS) || (y + cy < 0) || (y + cy >= MAX_ROWS)) continue;
      dptr = scrbuffer + (x + cx) + (y + cy) * MAX_COLUMNS;
      *dptr = (*dptr & 0xffff0000) | (t->firstglyph + cy * cellsw + cx);
    }
  }
}

// ---------------------------------------------------------------------------
// Low level cell access
// ---------------------------------------------------------------------------

static inline unsigned decodecolor(int color)
{
  int fg, bg;
  if (color & 0x8000)
  {
    fg = color & 31;
    bg = (color >> 5) & 31;
  }
  else
  {
    fg = color & 15;
    bg = (color >> 4) & 15;
  }
  return (fg << 16) | (bg << 21);
}

static inline void setcharcolor(unsigned *dptr, int ch, int color)
{
  *dptr = (ch & 0xffff) | decodecolor(color);
}

unsigned getcell(int x, int y)
{
  if ((!scrbuffer) || (x < 0) || (y < 0) || (x >= MAX_COLUMNS) || (y >= MAX_ROWS)) return 0;
  return scrbuffer[x + y * MAX_COLUMNS];
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

static void seticon(void)
{
  const RESIMAGE *img = res_getimage("logo64");
  SDL_Surface *s;
  if (!img) return;
  s = SDL_CreateRGBSurfaceFrom((void *)img->rgba, img->w, img->h, 32, img->w * 4,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
  if (s)
  {
    plat_seticon(s);
    SDL_FreeSurface(s);
  }
}

static int setupscale(void)
{
  int newscale = plat_getscale();
  int c;

  if ((newscale == scale) && (texture) && (fb)) return 1;
  scale = newscale;
  fbw = MAX_COLUMNS * CELLW * scale;
  fbh = MAX_ROWS * CELLH * scale;
  plat_fbw = fbw;
  plat_fbh = fbh;
  if (texture) SDL_DestroyTexture(texture);
  free(fb);
  fb = (Uint32 *)malloc(fbw * fbh * sizeof(Uint32));
  texture = SDL_CreateTexture(plat_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fbw, fbh);
  if ((!fb) || (!texture)) return 0;
  for (c = 0; c < numtileimages; c++) buildtileimage(&tileimages[c]);
  fulldirty = 1;
  return 1;
}

int initscreen(void)
{
  res_init();                  // the pictures that are stored with a palette
  if (!plat_init("C64 GrooveGoat", MAX_COLUMNS * CELLW, MAX_ROWS * CELLH))
    return 0;
  seticon();

  scrbuffer = (unsigned *)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  prevscrbuffer = (unsigned *)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  if ((!scrbuffer) || (!prevscrbuffer)) return 0;

  settheme(theme);
  if (!setupscale()) return 0;

  gfxinitted = 1;
  clearscreen();
  atexit(closescreen);
  return 1;
}

void closescreen(void)
{
  int c;
  if (!gfxinitted) return;
  gfxinitted = 0;
  free(scrbuffer);
  free(prevscrbuffer);
  scrbuffer = prevscrbuffer = NULL;
  free(fb);
  fb = NULL;
  for (c = 0; c < numtileimages; c++)
  {
    free(tileimages[c].pixels);
    tileimages[c].pixels = NULL;
  }
  if (texture) SDL_DestroyTexture(texture);
  texture = NULL;
  plat_shutdown();
}

void forceredraw(void)
{
  fulldirty = 1;
}

// ---------------------------------------------------------------------------
// Text output (same API as GoatTracker)
// ---------------------------------------------------------------------------

void clearscreen(void)
{
  int c;
  unsigned *dptr = scrbuffer;
  if (!gfxinitted) return;
  for (c = 0; c < MAX_ROWS * MAX_COLUMNS; c++)
  {
    setcharcolor(dptr, 0x20, 0x7);
    dptr++;
  }
}

// Clip area of the open dialog window: text never runs over its frame
static int clipon = 0, clipx0, clipx1, clipy0, clipy1;

void setclip(int x0, int y0, int x1, int y1)
{
  clipon = 1;
  clipx0 = x0; clipy0 = y0; clipx1 = x1; clipy1 = y1;
}

void clearclip(void)
{
  clipon = 0;
}

static int clipped(int x, int y)
{
  return (clipon) && (y >= clipy0) && (y <= clipy1) && ((x < clipx0) || (x > clipx1));
}

// ---------------------------------------------------------------------------
// Tooltips for the buttons of the dialogs: every text drawn in button colours
// is remembered for this frame; when the mouse rests on it for 2 seconds, its
// explanation (gtips.c) is shown. The main screen has its own (gdisplay.c).
// ---------------------------------------------------------------------------

#define MAXTIPREG 384
typedef struct
{
  short x, y, w;
  unsigned col;
  char raw[64];
} TIPREG;
static TIPREG tipreg[MAXTIPREG];
static int ntipreg = 0;
char tip_context[64] = "";
int tip_mainscreen = 0;

// a dialog frame: what was drawn before it (the main screen) has no tooltips
void tip_newframe(void)
{
  ntipreg = 0;
}

static void tip_register(int x, int y, int color, const char *text)
{
  TIPREG *t;
  int n = (int)strlen(text);
  if ((ntipreg >= MAXTIPREG) || (n < 1)) return;
  t = &tipreg[ntipreg++];
  t->x = x;
  t->y = y;
  t->w = n;
  t->col = decodecolor(color);
  snprintf(t->raw, sizeof t->raw, "%s", text);
}

static void tip_draw(void)
{
  static char cur[128] = "";
  static Uint32 t0 = 0;
  static int clicked = 0;
  char key[128], label[64];
  const char *tip;
  int k, hit = -1;
  if (tip_mainscreen)
  {
    tip_mainscreen = 0;
    ntipreg = 0;
    cur[0] = 0;
    return;
  }
  for (k = 0; k < ntipreg; k++)
  {
    TIPREG *t = &tipreg[k];
    if ((mousey == t->y) && (mousex >= t->x) && (mousex < t->x + t->w)) hit = k;
  }
  if (hit >= 0)
  {
    // the button must still be there (not covered by a later dialog)
    TIPREG *t = &tipreg[hit];
    unsigned cell = getcell(mousex, mousey);
    int i = mousex - t->x;
    if (((cell & ~0xffffu) != t->col) || ((cell & 0xff) != (unsigned char)t->raw[i < 63 ? i : 63])) hit = -1;
  }
  if (getenv("GG_TIPDUMP"))
    for (k = 0; k < ntipreg; k++) tip_dump(tip_context, tipreg[k].raw);
  ntipreg = 0;
  if (hit < 0)
  {
    cur[0] = 0;
    return;
  }
  // the label: printable ASCII, spaces collapsed
  tip_normalize(tipreg[hit].raw, label, sizeof label);
  snprintf(key, sizeof key, "%d/%d/%s", tipreg[hit].x, tipreg[hit].y, label);
  if (strcmp(key, cur))
  {
    snprintf(cur, sizeof cur, "%s", key);
    t0 = SDL_GetTicks();
    clicked = 0;
    return;
  }
  if (mouseb) clicked = 1;
  if ((clicked) || (SDL_GetTicks() - t0 < TOOLTIPDELAY)) return;
  tip = tip_lookup(tip_context, label);
  if (!tip) return;
  {
    // one or two lines under (or above) the pointer
    char l1[100], l2[100];
    int len = (int)strlen(tip), cut = len, x, y, w;
    l2[0] = 0;
    if (len > 96)
    {
      for (cut = 96; (cut > 40) && (tip[cut] != ' '); cut--) ;
      snprintf(l2, sizeof l2, " %.96s ", tip + cut + 1);
    }
    snprintf(l1, sizeof l1, " %.*s ", cut, tip);
    w = (int)strlen(l1) > (int)strlen(l2) ? (int)strlen(l1) : (int)strlen(l2);
    x = mousex - 1;
    if (x + w > MAX_COLUMNS) x = MAX_COLUMNS - w;
    if (x < 0) x = 0;
    y = mousey + 1;
    if (y + (l2[0] ? 2 : 1) > MAX_ROWS) y = mousey - (l2[0] ? 2 : 1);
    printblankc(x, y, MKCOL(C_PANEL, C_TOOLTIP), w);
    printtext(x, y, MKCOL(C_PANEL, C_TOOLTIP), l1);
    if (l2[0])
    {
      printblankc(x, y + 1, MKCOL(C_PANEL, C_TOOLTIP), w);
      printtext(x, y + 1, MKCOL(C_PANEL, C_TOOLTIP), l2);
    }
  }
}

void printtext(int x, int y, int color, const char *text)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  if ((color & 0x8000) && ((((color >> 5) & 0x1f) == C_BUTTON) || (((color >> 5) & 0x1f) == C_BUTTONHI)))
    tip_register(x, y, color, text);
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (*text)
  {
    if ((x >= 0) && (x < MAX_COLUMNS) && (!clipped(x, y))) setcharcolor(dptr, (unsigned char)*text, color);
    dptr++;
    x++;
    text++;
  }
}

void printtextc(int y, int color, const char *text)
{
  int x = (MAX_COLUMNS - (int)strlen(text)) / 2;
  printtext(x, y, color, text);
}

void printtextcp(int cp, int y, int color, const char *text)
{
  int x = cp - ((int)strlen(text) / 2);
  printtext(x, y, color, text);
}

void printblank(int x, int y, int length)
{
  printblankc(x, y, 0x7, length);
}

void printblankc(int x, int y, int color, int length)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS) && (!clipped(x, y))) setcharcolor(dptr, 0x20, color);
    dptr++;
    x++;
  }
}

void printglyph(int x, int y, int color, int glyph)
{
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS) || (x < 0) || (x >= MAX_COLUMNS)) return;
  setcharcolor(scrbuffer + x + y * MAX_COLUMNS, glyph, color);
}

void fillarea(int x, int y, int w, int h, int color, int glyph)
{
  int cx, cy;
  for (cy = y; cy < y + h; cy++)
    for (cx = x; cx < x + w; cx++)
      printglyph(cx, cy, color, glyph);
}

void drawbox(int x, int y, int color, int sx, int sy)
{
  int c;
  if (!gfxinitted) return;
  if ((!sx) || (!sy)) return;
  for (c = 1; c < sx - 1; c++)
  {
    printglyph(x + c, y, color, GL_HLINE);
    printglyph(x + c, y + sy - 1, color, GL_HLINE);
  }
  for (c = 1; c < sy - 1; c++)
  {
    printglyph(x, y + c, color, GL_VLINE);
    printglyph(x + sx - 1, y + c, color, GL_VLINE);
  }
  printglyph(x, y, color, GL_TL);
  printglyph(x + sx - 1, y, color, GL_TR);
  printglyph(x, y + sy - 1, color, GL_BL);
  printglyph(x + sx - 1, y + sy - 1, color, GL_BR);
}

// Panel: a filled area with a header bar (title) on its first row
void drawpanel(int x, int y, int w, int h, const char *title, int active)
{
  fillarea(x, y, w, h, MKCOL(8, C_PANEL), ' ');
  if (title)
  {
    printblankc(x, y, MKCOL(C_HEADERTEXT, active ? C_SELECT : C_HEADER), w);
    printtext(x + 1, y, MKCOL(C_HEADERTEXT, active ? C_SELECT : C_HEADER), title);
  }
}

void drawrainbow(int x, int y, int length)
{
  int c;
  for (c = 0; c < length; c++)
    printglyph(x + c, y, MKCOL(C_RB1 + (c * 5 / (length > 0 ? length : 1)), 0), GL_FULL);
}

void printbg(int x, int y, int color, int length)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS))
    {
      if (color == 1)
        *dptr = (*dptr & 0xffff) | decodecolor(MKCOL(15, C_SELECT));
      else
        *dptr = (*dptr & 0xffff) | decodecolor(MKCOL(cursorfg, cursorbg));
    }
    dptr++;
    x++;
  }
}

void printcursor(int x, int y, int length)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS))
      *dptr = (*dptr & 0xffff) | decodecolor(MKCOL(cursorfg, cursorbg));
    dptr++;
    x++;
  }
}

void printbgx(int x, int y, int bg, int length)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS))
      *dptr = (*dptr & ~(31u << 21)) | ((unsigned)(bg & 31) << 21);
    dptr++;
    x++;
  }
}

void printfgx(int x, int y, int fg, int length)
{
  unsigned *dptr;
  if (!gfxinitted) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS))
      *dptr = (*dptr & ~(31u << 16)) | ((unsigned)(fg & 31) << 16);
    dptr++;
    x++;
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static inline Uint32 blend(Uint32 bg, Uint32 fg, unsigned a)
{
  unsigned ia = 255 - a;
  unsigned r = (((bg >> 16) & 0xff) * ia + ((fg >> 16) & 0xff) * a) / 255;
  unsigned g = (((bg >> 8) & 0xff) * ia + ((fg >> 8) & 0xff) * a) / 255;
  unsigned b = ((bg & 0xff) * ia + (fg & 0xff) * a) / 255;
  return 0xff000000 | (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// The modern look: buttons, panels and highlighted rows get rounded corners,
// and the screen behind a dialog is dimmed
// ---------------------------------------------------------------------------

static int dimx0 = 0, dimy0 = 0, dimx1 = -1, dimy1 = -1;     // the dialog that is not dimmed
static int lastdim[5] = {0, 0, -1, -1, 0};
static int dimamount = 110, nextdimamount = 110;

void setdimamount(int a)
{
  nextdimamount = a;
}

void setdimrect(int x, int y, int w, int h)
{
  dimx0 = x;
  dimy0 = y;
  dimx1 = x + w - 1;
  dimy1 = y + h - 1;
}

void cleardimrect(void)
{
  nextdimamount = 110;
  dimx0 = dimy0 = 0;
  dimx1 = dimy1 = -1;
}

static int isdimmed(int x, int y)
{
  if (dimx1 < 0) return 0;
  if ((x >= 0) && (y >= 0) && (x < MAX_COLUMNS) && (y < MAX_ROWS) && (((scrbuffer[x + y * MAX_COLUMNS] >> 21) & 31) == C_TOOLTIP)) return 0;
  return (x < dimx0) || (x > dimx1) || (y < dimy0) || (y > dimy1);
}

// background colours whose areas get rounded corners
static int roundbg(int bg)
{
  switch (bg)
  {
    case 1: case 2: case C_PANEL: case C_HEADER: case C_BUTTON: case C_BUTTONHI: case C_TOOLTIP:
    case C_BEATROW: case C_PLAYROW: case C_EDITROW: case C_SELECT:
    return 1;
  }
  return 0;
}

static inline int cellbg(int x, int y)
{
  if ((x < 0) || (y < 0) || (x >= MAX_COLUMNS) || (y >= MAX_ROWS)) return -1;
  {
    unsigned c = scrbuffer[x + y * MAX_COLUMNS];
    if ((c & 0xffff) >= 256) return -2;          // pictures and canvases
    return (c >> 21) & 31;
  }
}

static void roundcorners(int x, int y, int bg, Uint32 *cellfb)
{
  int cw = CELLW * scale, ch = CELLH * scale;
  int r = (bg == C_PANEL || bg == C_HEADER) ? 8 * scale : 4 * scale;
  int corner;
  if (r > cw) r = cw;
  for (corner = 0; corner < 4; corner++)
  {
    int dx = (corner & 1) ? 1 : -1, dy = (corner & 2) ? 1 : -1;
    int hb = cellbg(x + dx, y), vb = cellbg(x, y + dy), db = cellbg(x + dx, y + dy);
    int outer, px, py, me = isdimmed(x, y);
    Uint32 oc;
    // a dialog on the dimmed screen and a tooltip: their corners are always round
    int edge = (dimx1 >= 0) && (!me) && (isdimmed(x + dx, y)) && (isdimmed(x, y + dy)) && (isdimmed(x + dx, y + dy));
    if (bg == C_TOOLTIP) edge = (hb != bg) && (vb != bg) && (db != bg);
    if (!edge)
    {
      if ((bg == C_TOOLTIP) || (hb == bg) || (vb == bg) || (db == bg)) continue;
      // panels are cards on the background: only their outer corners are round
      if ((bg == C_PANEL) || (bg == C_HEADER))
        if ((hb > 0) || (vb > 0) || (db > 0)) continue;
      if ((hb == -2) || (vb == -2)) continue;
    }
    if ((hb < 0) && (vb < 0)) continue;
    outer = hb >= 0 ? hb : vb;
    oc = palette[outer];
    // at the edge of a dialog the outside is dimmed (the cell itself is dimmed later)
    if ((!me) && (isdimmed(hb >= 0 ? x + dx : x, hb >= 0 ? y : y + dy)))
      oc = blend(oc, 0xff000000, dimamount);
    for (py = 0; py < r; py++)
      for (px = 0; px < r; px++)
      {
        // distance of the pixel centre from the centre of the corner circle
        float fx = r - (px + 0.5f), fy = r - (py + 0.5f);
        float d = sqrtf(fx * fx + fy * fy) - r;
        int a, sx, sy;
        Uint32 *p;
        if (d <= -0.5f) continue;
        a = d >= 0.5f ? 255 : (int)((d + 0.5f) * 255);
        sx = (dx < 0) ? px : cw - 1 - px;
        sy = (dy < 0) ? py : ch - 1 - py;
        p = cellfb + sy * fbw + sx;
        *p = blend(*p, oc, a);
      }
  }
}

static void rendercell_(int x, int y, unsigned cell);

static void rendercell(int x, int y, unsigned cell)
{
  int glyph = cell & 0xffff;
  int bg = (cell >> 21) & 31, fg = (cell >> 16) & 31;
  int dim = isdimmed(x, y);
  // text on the accent colour (active buttons) uses the panel colour
  if ((bg == C_BUTTONHI) && ((fg == C_BUTTONTEXT) || (fg == C_HEADERTEXT) || (fg == 15) || (fg == 8)))
    cell = (cell & ~(31u << 16)) | ((unsigned)C_PANEL << 16);
  rendercell_(x, y, cell);
  if ((glyph < 256) && (roundbg(bg)))
    roundcorners(x, y, bg, fb + (y * CELLH * scale) * fbw + x * CELLW * scale);
  if ((dim) && (bg != C_TOOLTIP))     // tooltips stay bright
  {
    int cw = CELLW * scale, ch = CELLH * scale, px, py;
    Uint32 *d = fb + (y * ch) * fbw + x * cw;
    for (py = 0; py < ch; py++, d += fbw)
      for (px = 0; px < cw; px++) d[px] = blend(d[px], 0xff000000, dimamount);
  }
  (void)fg;
}

static void rendercell_(int x, int y, unsigned cell)
{
  int glyph = cell & 0xffff;
  Uint32 fgc = palette[(cell >> 16) & 31];
  Uint32 bgc = palette[(cell >> 21) & 31];
  int cw = CELLW * scale;
  int ch = CELLH * scale;
  Uint32 *dst = fb + (y * ch) * fbw + x * cw;
  int px, py;

  if ((glyph >= FXCANVAS_BASE) && (glyph < FXCANVAS_BASE + 2 * FXCANVAS_CELLS))
  {
    int id = (glyph - FXCANVAS_BASE) / FXCANVAS_CELLS, idx = (glyph - FXCANVAS_BASE) % FXCANVAS_CELLS;
    int ccw = fxcw[id] ? fxcw[id] : 1, stride = ccw * 16;
    int tx = idx % ccw, ty = idx / ccw;
    const unsigned *a;
    if (!fxbuf[id]) return;
    a = fxbuf[id] + (ty * 32) * stride + tx * 16;
    for (py = 0; py < ch; py++)
    {
      for (px = 0; px < cw; px++)
      {
        if (scale == 1)
        {
          const unsigned *q = a + (py * 2) * stride + px * 2;
          unsigned r = ((q[0] >> 16) & 0xff) + ((q[1] >> 16) & 0xff) + ((q[stride] >> 16) & 0xff) + ((q[stride + 1] >> 16) & 0xff);
          unsigned g = ((q[0] >> 8) & 0xff) + ((q[1] >> 8) & 0xff) + ((q[stride] >> 8) & 0xff) + ((q[stride + 1] >> 8) & 0xff);
          unsigned b = (q[0] & 0xff) + (q[1] & 0xff) + (q[stride] & 0xff) + (q[stride + 1] & 0xff);
          dst[px] = 0xff000000 | (((r + 2) >> 2) << 16) | (((g + 2) >> 2) << 8) | ((b + 2) >> 2);
        }
        else dst[px] = 0xff000000 | a[py * stride + px];
      }
      dst += fbw;
    }
    return;
  }
  if ((glyph >= RGBCANVAS_BASE) && (glyph < CANVAS_BASE))
  {
    int idx = glyph - RGBCANVAS_BASE;
    int tx = idx % rgbcw, ty = idx / rgbcw;
    const unsigned *a = rgbbuf + (ty * 32) * RGBCANVAS_STRIDE + tx * 16;
    for (py = 0; py < ch; py++)
    {
      for (px = 0; px < cw; px++)
      {
        if (scale == 1)
        {
          const unsigned *q = a + (py * 2) * RGBCANVAS_STRIDE + px * 2;
          unsigned r = ((q[0] >> 16) & 0xff) + ((q[1] >> 16) & 0xff) + ((q[RGBCANVAS_STRIDE] >> 16) & 0xff) + ((q[RGBCANVAS_STRIDE + 1] >> 16) & 0xff);
          unsigned g = ((q[0] >> 8) & 0xff) + ((q[1] >> 8) & 0xff) + ((q[RGBCANVAS_STRIDE] >> 8) & 0xff) + ((q[RGBCANVAS_STRIDE + 1] >> 8) & 0xff);
          unsigned b = (q[0] & 0xff) + (q[1] & 0xff) + (q[RGBCANVAS_STRIDE] & 0xff) + (q[RGBCANVAS_STRIDE + 1] & 0xff);
          dst[px] = 0xff000000 | (((r + 2) >> 2) << 16) | (((g + 2) >> 2) << 8) | ((b + 2) >> 2);
        }
        else dst[px] = 0xff000000 | a[py * RGBCANVAS_STRIDE + px];
      }
      dst += fbw;
    }
    return;
  }
  if (glyph >= CANVAS_BASE)
  {
    int id = (glyph - CANVAS_BASE) / CANVAS_CELLS;
    int idx = (glyph - CANVAS_BASE) % CANVAS_CELLS;
    int ccw = canvascw[id] ? canvascw[id] : 1;
    int tx = idx % ccw, ty = idx / ccw;
    const int stride = CANVAS_MAXCW * 16;
    const unsigned char *a = canvasbuf[id] + (ty * 32) * stride + tx * 16;
    for (py = 0; py < ch; py++)
    {
      for (px = 0; px < cw; px++)
      {
        unsigned v;
        if (scale == 1)
        {
          const unsigned char *q = a + (py * 2) * stride + px * 2;
          v = (q[0] + q[1] + q[stride] + q[stride + 1] + 2) >> 2;
        }
        else v = a[py * stride + px];
        if (!v) dst[px] = bgc;
        else if (v == 255) dst[px] = fgc;
        else dst[px] = blend(bgc, fgc, v);
      }
      dst += fbw;
    }
    return;
  }
  if (glyph < 256)
  {
    const unsigned char *a = (scale == 1 ? res_font1 : res_font2) + glyph * cw * ch;
    for (py = 0; py < ch; py++)
    {
      for (px = 0; px < cw; px++)
      {
        unsigned v = *a++;
        if (!v) dst[px] = bgc;
        else if (v == 255) dst[px] = fgc;
        else dst[px] = blend(bgc, fgc, v);
      }
      dst += fbw;
    }
  }
  else
  {
    int tx = 0, ty = 0;
    TILEIMAGE *t = findtile(glyph, &tx, &ty);
    if ((!t) || (!t->pixels))
    {
      for (py = 0; py < ch; py++, dst += fbw)
        for (px = 0; px < cw; px++) dst[px] = bgc;
      return;
    }
    for (py = 0; py < ch; py++)
    {
      const Uint32 *s = t->pixels + (ty * ch + py) * t->pw + tx * cw;
      for (px = 0; px < cw; px++)
      {
        Uint32 v = s[px];
        unsigned a = v >> 24;
        dst[px] = a ? blend(bgc, v, a) : bgc;
      }
      dst += fbw;
    }
  }
}

char autoshotname[1024];
int autoshotframes = 0;

void fliptoscreen(void)
{
  int x, y;
  int firstrow = MAX_ROWS, lastrow = -1;
  static int presented = 0;

  if (!gfxinitted) return;
  tip_draw();

  if (win_resized)
  {
    win_resized = 0;
    setupscale();
    fulldirty = 1;
  }
  if (win_redraw)
  {
    win_redraw = 0;
    presented = 0;
  }

  // the dimmed screen behind a dialog
  dimamount = nextdimamount;
  if ((lastdim[0] != dimx0) || (lastdim[1] != dimy0) || (lastdim[2] != dimx1) || (lastdim[3] != dimy1) || (lastdim[4] != dimamount))
  {
    lastdim[4] = dimamount;
    lastdim[0] = dimx0;
    lastdim[1] = dimy0;
    lastdim[2] = dimx1;
    lastdim[3] = dimy1;
    fulldirty = 1;
  }
  // a cell is drawn again when it or a neighbour changed (rounded corners)
  {
    static unsigned char changed[MAX_ROWS][MAX_COLUMNS];
    for (y = 0; y < MAX_ROWS; y++)
    {
      unsigned *sptr = scrbuffer + y * MAX_COLUMNS;
      unsigned *cmpptr = prevscrbuffer + y * MAX_COLUMNS;
      for (x = 0; x < MAX_COLUMNS; x++)
      {
        changed[y][x] = (fulldirty) || (sptr[x] != cmpptr[x]);
        cmpptr[x] = sptr[x];
      }
    }
    for (y = 0; y < MAX_ROWS; y++)
    {
      unsigned *sptr = scrbuffer + y * MAX_COLUMNS;
      for (x = 0; x < MAX_COLUMNS; x++)
      {
        int dirty = changed[y][x];
        if (!dirty)
        {
          int y0 = y > 0 ? y - 1 : 0, y1 = y < MAX_ROWS - 1 ? y + 1 : y;
          int x0 = x > 0 ? x - 1 : 0, x1 = x < MAX_COLUMNS - 1 ? x + 1 : x, yy, xx;
          if (!roundbg((sptr[x] >> 21) & 31)) continue;
          for (yy = y0; (yy <= y1) && (!dirty); yy++)
            for (xx = x0; xx <= x1; xx++)
              if (changed[yy][xx]) { dirty = 1; break; }
        }
        if (dirty)
        {
          rendercell(x, y, sptr[x]);
          if (y < firstrow) firstrow = y;
          if (y > lastrow) lastrow = y;
        }
      }
    }
  }
  fulldirty = 0;

  if (lastrow >= firstrow)
  {
    SDL_Rect r;
    r.x = 0;
    r.y = firstrow * CELLH * scale;
    r.w = fbw;
    r.h = (lastrow - firstrow + 1) * CELLH * scale;
    SDL_UpdateTexture(texture, &r, fb + r.y * fbw, fbw * sizeof(Uint32));
    presented = 0;
  }

  if (!presented)
  {
    int outw = 1, outh = 1;
    SDL_Rect dst;
    double sx, sy, s;
    SDL_GetRendererOutputSize(plat_renderer, &outw, &outh);
    sx = (double)outw / fbw;
    sy = (double)outh / fbh;
    s = sx < sy ? sx : sy;
    dst.w = (int)(fbw * s);
    dst.h = (int)(fbh * s);
    dst.x = (outw - dst.w) / 2;
    dst.y = (outh - dst.h) / 2;
    plat_dstx = dst.x;
    plat_dsty = dst.y;
    plat_dstw = dst.w;
    plat_dsth = dst.h;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, ((dst.w == fbw) && (dst.h == fbh)) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
    SDL_SetRenderDrawColor(plat_renderer, (palette[0] >> 16) & 0xff, (palette[0] >> 8) & 0xff, palette[0] & 0xff, 255);
    SDL_RenderClear(plat_renderer);
    SDL_RenderCopy(plat_renderer, texture, NULL, &dst);
    SDL_RenderPresent(plat_renderer);
    presented = 1;
  }

  // Automatic screenshot of whatever is shown (build tests)
  if ((autoshotname[0]) && (--autoshotframes <= 0))
  {
    char name[1024];
    strcpy(name, autoshotname);
    autoshotname[0] = 0;
    savescreenshot(name);
    exit(0);
  }
}

// GG_RECORD=FILE.mp4: every frame (50 per second, in real time) into a
// video through ffmpeg - for the presentation video (with -W the sound of the
// program goes to sidaudio.raw at the same time)
static FILE *recpipe = NULL;
static unsigned recframes = 0;
unsigned record_startticks = 0;

static void record_close(void)
{
  if (recpipe) pclose(recpipe);
  recpipe = NULL;
}

unsigned record_frame(void) { return recframes; }

void record_frames(int frames)
{
  static int tried = 0;
  if (!tried)
  {
    const char *f = getenv("GG_RECORD");
    char cmd[2048];
    tried = 1;
    if ((!f) || (!fb)) return;
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s %dx%d -r 50 -i - "
             "-c:v libx264 -preset ultrafast -crf 14 -pix_fmt yuv420p \"%s\"", fbw, fbh, f);
    recpipe = popen(cmd, "w");
    record_startticks = SDL_GetTicks();
    if (recpipe) atexit(record_close);
    {
      extern FILE *writehandle;      // -W: where the sound file was at the first frame
      if (writehandle) fprintf(stderr, "RECAUDIO %ld\n", ftell(writehandle));
    }
  }
  if ((!recpipe) || (!fb)) return;
  {
    // the frame with a mouse pointer drawn in (the window has none of its own)
    static const char *arrow[] =
    {
      "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X",
      "X.......X", "X........X", "X.........X", "X......XXXXX", "X...X..X",
      "X..XX..X", "X.X  X..X", "XX   X..X", "X     X..X", "      X..X", "       XX", NULL
    };
    static Uint32 *frame = NULL;
    int px = win_mousex, py = win_mousey, r, c, sc = scale ? scale : 1;
    if (!frame) frame = (Uint32 *)malloc((size_t)fbw * fbh * 4);
    if (!frame) return;
    memcpy(frame, fb, (size_t)fbw * fbh * 4);
    for (r = 0; arrow[r]; r++)
      for (c = 0; arrow[r][c]; c++)
      {
        int dx, dy;
        Uint32 col;
        if (arrow[r][c] == ' ') continue;
        col = (arrow[r][c] == 'X') ? 0xff000000 : 0xffffffff;
        for (dy = 0; dy < sc; dy++)
          for (dx = 0; dx < sc; dx++)
          {
            int x = px + c * sc + dx, y = py + r * sc + dy;
            if ((x >= 0) && (y >= 0) && (x < fbw) && (y < fbh)) frame[y * fbw + x] = col;
          }
      }
    // as many frames as the wall clock says (50 per second), so that the
    // video keeps in step with the sound even when the program was busy
    frames = (int)((SDL_GetTicks() - record_startticks) / 20) - (int)recframes;
    while (frames-- > 0)
    {
      fwrite(frame, 4, fbw * fbh, recpipe);
      recframes++;
    }
  }
}

int savescreenshot(const char *filename)
{
  SDL_Surface *s;
  int result;
  if (!fb) return 0;
  fulldirty = 1;
  fliptoscreen();
  s = SDL_CreateRGBSurfaceFrom(fb, fbw, fbh, 32, fbw * 4, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
  if (!s) return 0;
  result = SDL_SaveBMP(s, filename);
  SDL_FreeSurface(s);
  return result == 0;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void getkey(void)
{
  int c;
  autoscript_tick();
  midi_service();
  win_asciikey = 0;
  win_symkey = 0;
  {
    int frames = win_getspeed(50);
    cursorflashdelay += frames;
    record_frames(frames);
  }

  prevmouseb = mouseb;
  mouseb = win_mousebuttons | win_mousepressed;     // a tap within one frame counts as well
  win_mousepressed = 0;
  mousex = win_mousex / (CELLW * (scale ? scale : 1));
  mousey = win_mousey / (CELLH * (scale ? scale : 1));
  mousepx = win_mousex * 16 / (CELLW * (scale ? scale : 1));
  mousepy = win_mousey * 32 / (CELLH * (scale ? scale : 1));
  if (mousex >= MAX_COLUMNS) mousex = MAX_COLUMNS - 1;
  if (mousey >= MAX_ROWS) mousey = MAX_ROWS - 1;
  mousewheel = win_mousewheel;
  win_mousewheel = 0;

  if (mouseb) mouseheld++;
  else mouseheld = 0;

  key = win_asciikey;
  symkey = win_symkey;
  rawkey = 0;
  for (c = 0; c < MAX_KEYS; c++)
  {
    if (win_keytable[c])
    {
      if ((c != KEY_LEFTSHIFT) && (c != KEY_RIGHTSHIFT) &&
          (c != KEY_CTRL) && (c != KEY_RIGHTCTRL) &&
          (c != KEY_ALT) && (c != KEY_RIGHTALT) &&
          (c != KEY_LEFTMETA) && (c != KEY_RIGHTMETA))
      {
        rawkey = c;
        win_keytable[c] = 0;
        break;
      }
      win_keytable[c] = 0;
    }
  }

  shiftpressed = 0;
  ctrlpressed = 0;
  altpressed = 0;
  metapressed = 0;
  shiftonlypressed = 0;

  // the modifiers as the system has them now: a key-up that never arrived
  // (screenshot keys, switching apps on a Mac) must not leave CMD / CTRL held,
  // which turned LEFT / RIGHT into start / end jumps and clicks into right clicks
  {
    static int scripted = -1;      // test scripts push key events: SDL does not track those
    SDL_Keymod m;
    int sh, ct, al, me;
    if (scripted < 0) scripted = getenv("GG_SCRIPT") ? 1 : 0;
    m = scripted ? KMOD_NONE : SDL_GetModState();
    sh = (m & KMOD_SHIFT) || ((scripted) && ((win_keystate[KEY_LEFTSHIFT]) || (win_keystate[KEY_RIGHTSHIFT])));
    ct = (m & KMOD_CTRL) || ((scripted) && ((win_keystate[KEY_CTRL]) || (win_keystate[KEY_RIGHTCTRL])));
    al = (m & KMOD_ALT) || ((scripted) && ((win_keystate[KEY_ALT]) || (win_keystate[KEY_RIGHTALT])));
    me = (m & KMOD_GUI) || ((scripted) && ((win_keystate[KEY_LEFTMETA]) || (win_keystate[KEY_RIGHTMETA])));
    shiftpressed = sh || ct;
    shiftonlypressed = sh && !ct && !me;
    ctrlpressed = ct;
    altpressed = al;
    metapressed = me;
  }

  // Keyboard layout DE (QWERTZ): commands with SHIFT / CTRL / CMD use the
  // key labels, so the Y and Z keys are swapped for them (CTRL+Z = undo on
  // the key labelled Z). Notes are always entered by key position.
  if ((kbdlayout == 1) && ((shiftpressed) || (metapressed)))
  {
    if (rawkey == KEY_Z) rawkey = KEY_Y;
    else if (rawkey == KEY_Y) rawkey = KEY_Z;
  }

  // While CTRL / CMD / ALT is held, no text characters are wanted
  if ((ctrlpressed) || (metapressed))
  {
    if ((key >= 32) && (key < 256)) key = 0;
  }

  if (rawkey == KEY_KPENTER)
  {
    key = KEY_ENTER;
    rawkey = KEY_ENTER;
  }

  if ((rawkey >= KEY_KP0) && (rawkey <= KEY_KP9) && (!key))
    key = '0' + rawkey - KEY_KP0;

  // Macbook keyboard: insert using ALT+BACKSPACE or FN+SHIFT+BACKSPACE
  if (((rawkey == KEY_BACKSPACE) && (altpressed)) ||
      ((rawkey == KEY_DEL) && (shiftonlypressed)))
  {
    rawkey = KEY_INS;
    key = 0;
  }
}

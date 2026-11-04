// C64 GrooveGoat - beat editor: genre catalog
//
// Grid: one line per drum role, one character per 16th note.
//   K kick  S snare  C clap  H closed hi-hat  O open hi-hat
//   T high tom  M mid tom  L low tom  R rimshot  Y crash cymbal
//   .  rest   x  hit   X  accent   g  ghost (soft)   r  roll (two 32nds)
// 16 characters = one bar of 4/4, 32 characters = two bars.
//
// Only one drum sound plays at a time on a SID voice: when roles meet on
// the same step, the strongest wins (crash > snare > clap > kick > toms >
// rimshot > open hat > closed hat).

#include "gbeat.h"
#include <stddef.h>

const char *kitnames[KIT_NUM] = {"Chip", "808", "909", "Rock"};
const char *beatrolenames[BR_NUM] = {"Kick", "Snare", "Clap", "HiHat", "OHat", "TomHi", "TomMd", "TomLo", "Rim", "Crash"};

// common fills (one bar)
#define FILL_TOMS   "K:x............... S:....x.........XX T:........xx...... M:..........xx.... L:............xx.."
#define FILL_SNARE  "K:x.......x....... S:....x...x.xxXXXX"
#define FILL_ROLL   "K:x.......x....... S:....x.......rrrr"
#define FILL_BREAK  "K:x.....x......... S:....X..x.xx.XxXx"
#define FILL_CLAP   "K:x...x...x....... C:....x.......xxxx"
#define FILL_ELEC   "K:x.....x...x..... C:....X...x.x.XxXx"
#define FILL_METAL  "K:xxxxxxxx........ S:........xxxx.... T:............xx.. L:..............xx"
#define FILL_JUNGLE "K:x.x....x........ S:....x..x.x.xrrXX"
#define FILL_REGGAE "K:........x....... R:....x.......x... T:..........x..... L:............x.x."
#define FILL_LATIN  "K:x..x..x......... R:x..x..x......... T:........x.x..... L:............x.x."

const BEATGENRE beatcatalog[] = {
  {"Chiptune / Synthwave", {
    {"C64 Classic", KIT_CHIP, FILL_SNARE, {
      {"Standard 4/4", "K:x.......x.x..... S:....x.......x... H:..x...x...x...x.", 50},
      {"Driving 16ths", "K:x...x...x...x... S:....x.......x... H:xxxxxxxxxxxxxxxx", 50},
      {"Half-Time", "K:x.....x......... S:........x....... H:x.x.x.x.x.x.x.x.", 50},
      {"Syncopated", "K:x..x..x...x..... S:....x.......x..x H:..x...x...x...x.", 50},
      {"Breakbeat", "K:x.x.......x..... S:....x..g.g..x..g H:x.x.x.x.x.x.x.x.", 50},
      {"Offbeat", "K:x...x...x...x... S:....x.......x... O:..x...x...x...x.", 50}}},
    {"Synthwave", KIT_909, FILL_TOMS, {
      {"Standard 4/4", "K:x...x...x...x... S:....X.......X... H:..x...x...x...x.", 50},
      {"Half-Time", "K:x.......x.x..... S:........X....... H:x.x.x.x.x.x.x.x.", 50},
      {"Gated snare", "K:x.....x.x....... S:....X.......X... O:..x...x...x...x.", 50},
      {"Syncopated", "K:x..x..x...x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Offbeat", "K:x...x...x...x... C:....x.......x... O:..x...x...x...x.", 50}}},
    {"Outrun", KIT_909, FILL_TOMS, {
      {"Standard 4/4", "K:x...x...x...x... S:....x.......x... H:x.x.x.x.x.x.x.x.", 50},
      {"Driving 16ths", "K:x...x...x...x... S:....X.......X... H:xxxxxxxxxxxxxxxx", 50},
      {"Half-Time", "K:x.......x....... S:........X....... H:x.x.x.x.x.x.x.x.", 50},
      {"Syncopated", "K:x..x..x...x..x.. S:....X.......X... H:..x...x...x...x.", 50}}}}},

  {"Techno", {
    {"Minimal", KIT_909, FILL_CLAP, {
      {"Four on the floor", "K:x...x...x...x... H:..x...x...x...x.", 50},
      {"Rolling", "K:x...x...x...x... R:...x......x..... H:..xx..xx..xx..xx", 50},
      {"Offbeat", "K:x...x...x...x... C:....x.......x... O:..x...x...x...x.", 50},
      {"Breakdown", "R:....x.......x... H:x.x.x.x.x.x.x.x.", 50}}},
    {"Hard Techno", KIT_909, FILL_ROLL, {
      {"Pounding", "K:X...x...X...x... O:..x...x...x...x.", 50},
      {"Rumble", "K:x..xx..xx..xx..x C:....x.......x...", 50},
      {"16th hats", "K:x...x...x...x... C:....x.......x... H:xxxxxxxxxxxxxxxx", 50},
      {"Half-Time", "K:x.......x....... C:........X....... H:..x...x...x...x.", 50}}},
    {"Industrial", KIT_909, FILL_SNARE, {
      {"Stomp", "K:X..xX..xX..xX..x S:....X.......X...", 50},
      {"Metal hits", "K:x...x...x...x... S:..x...x...x...x.", 50},
      {"Broken", "K:x..x..x...x..... S:....X..x....X..x", 50},
      {"Half-Time", "K:x.......x....... S:........X....... H:x.x.x.x.x.x.x.x.", 50}}}}},

  {"EBM / Cyberpunk", {
    {"EBM", KIT_909, FILL_SNARE, {
      {"Marching", "K:x...x...x...x... S:....x.......x... H:..x...x...x...x.", 50},
      {"8th kicks", "K:x.x.x.x.x.x.x.x. S:....X.......X...", 50},
      {"Syncopated", "K:x..x..x.x..x..x. S:....X.......X...", 50},
      {"Half-Time", "K:x.......x.....x. S:........X....... H:x.x.x.x.x.x.x.x.", 50}}},
    {"Dark Electro", KIT_909, FILL_ROLL, {
      {"Standard", "K:x...x...x...x... S:....X.......X... H:xxxxxxxxxxxxxxxx", 50},
      {"Gallop", "K:x..xx..xx..xx..x S:....X.......X...", 50},
      {"Breakbeat", "K:x.....x...x..... S:....X.......X..x H:x.x.x.x.x.x.x.x.", 50},
      {"Half-Time", "K:x.........x..... S:........X....... H:x.x.x.x.x.x.x.x.", 50}}},
    {"Cyberpunk", KIT_808, FILL_ELEC, {
      {"Standard", "K:x.....x...x..... S:....X.......X... H:x.xxx.xxx.xxx.xx", 50},
      {"Trap hats", "K:x.........x..x.. S:........X....... H:x.x.r.x.x.rrx.x.", 50},
      {"Glitch", "K:x..x...x..x..... S:....X..g....X.g. H:..x.x...x...xx..", 50},
      {"Driving", "K:x...x...x...x... C:....X.......X... H:xxxxxxxxxxxxxxxx", 50}}}}},

  {"Electro", {
    {"Electro Funk", KIT_808, FILL_ELEC, {
      {"Classic", "K:x.....x...x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Syncopated", "K:x..x..x...x..x.. C:....x.......x... H:..x...x...x...x.", 50},
      {"Clap-heavy", "K:x.....x.x....... C:....X...x...X..x H:x.x.x.x.x.x.x.x.", 50},
      {"Offbeat", "K:x...x...x...x... C:....x.......x... O:..x...x...x...x.", 50}}},
    {"808 Electro", KIT_808, FILL_ELEC, {
      {"Classic", "K:x.....x...x...x. S:....X.......X... H:xxxxxxxxxxxxxxxx", 50},
      {"Broken", "K:x..x..x..x..x... C:....x.......x... H:x.x.x.x.x.x.x.x.", 50},
      {"Half-Time", "K:x.........x..... C:........X....... H:x.x.x.x.x.x.x.x.", 50},
      {"Robot", "K:x.x...x.x...x... S:....X.......X... R:..x...x...x...x.", 50}}},
    {"Miami Bass", KIT_808, FILL_CLAP, {
      {"Standard", "K:x.....x.x.....x. C:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Booty", "K:x..x..x.x..x..x. C:....X.......X... H:xxxxxxxxxxxxxxxx", 50},
      {"Hat rolls", "K:x.....x.x....... C:....X.......X... H:x.x.r.x.x.x.r.rr", 50},
      {"Half-Time", "K:x.......x.x..... C:........X....... H:x.x.x.x.x.x.x.x.", 50}}}}},

  {"Hip-Hop", {
    {"Boom Bap", KIT_808, FILL_BREAK, {
      {"Standard", "K:x.......x.x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 62},
      {"Syncopated", "K:x..x......x..x.. S:....X.......X... H:x.x.x.x.x.x.x.x.", 62},
      {"Ghost notes", "K:x.......x.x..... S:....X..g.g..X..g H:x.x.x.x.x.x.x.x.", 62},
      {"Half-Time", "K:x.....x......... S:........X....... H:x.x.x.x.x.x.x.x.", 62}}},
    {"Trap", KIT_808, FILL_ROLL, {
      {"Standard", "K:x.......x..x.... S:........X....... H:x.x.x.x.x.x.x.x.", 50},
      {"Hat rolls", "K:x.......x..x.... S:........X....... H:x.x.rrx.x.x.rrrr", 50},
      {"Triplet hats", "K:x.....x...x..... S:........X....... H:x..x..x..x..x.x.", 50},
      {"Open hats", "K:x.........x.x... S:........X....... O:..x...x...x...x.", 50}}},
    {"Lo-Fi", KIT_808, FILL_BREAK, {
      {"Lazy", "K:x......x..x..... S:....x.......x... H:x.x.x.x.x.x.x.x.", 66},
      {"Rimshot", "K:x.......x.x..... R:....x.......x... H:x.x.x.x.x.x.x.x.", 66},
      {"Ghost notes", "K:x......x..x..... S:....x..g....x.g. H:x.x.x.x.x.x.x.x.", 66},
      {"Sparse", "K:x.........x..... S:....x.......x...", 66}}}}},

  {"Funk / Disco", {
    {"Funk", KIT_ROCK, FILL_SNARE, {
      {"Standard", "K:x.........x..x.. S:....X.......X... H:xxxxxxxxxxxxxxxx", 50},
      {"Ghost notes", "K:x.........x..x.. S:....X..g.g..X..g H:x.x.x.x.x.x.x.x.", 50},
      {"Syncopated", "K:x.x.......x..x.. S:....X..g.g.gX..g H:x.x.x.x.x.x.x.x.", 50},
      {"Linear", "K:x..x.....x.x.... S:....X..x..x..X.. H:.x...x.x.x...x.x", 50},
      {"Shuffle", "K:x.........x..x.. S:....X..g....X... H:x.x.x.x.x.x.x.x.", 62}}},
    {"Disco", KIT_909, FILL_SNARE, {
      {"Standard", "K:x...x...x...x... S:....x.......x... O:..x...x...x...x.", 50},
      {"16th hats", "K:x...x...x...x... S:....x.......x... H:xxxxxxxxxxxxxxxx", 50},
      {"Clap", "K:x...x...x...x... C:....x.......x... O:..x...x...x...x.", 50},
      {"Half-Time", "K:x.......x....... S:........x....... O:..x...x...x...x.", 50}}},
    {"Breakbeat", KIT_ROCK, FILL_BREAK, {
      {"Amen", "K:x.x.......xx.... S:....x..x.x..x..x H:x.x.x.x.x.x.x.x. ", 50},
      {"Big beat", "K:x.....x.x....... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Skippy", "K:x..x......x..... S:....x..x.x..x..g H:x.x.x.x.x.x.x.x.", 50},
      {"Two bars", "K:x.x.......x.....x.x.......xx.... S:....x..x.x..x..x....x..x.x....x. H:x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 50}}}}},

  {"Drum & Bass", {
    {"Jungle", KIT_ROCK, FILL_JUNGLE, {
      {"Amen", "K:x.x.......xx.... S:....x..x.x..x..x H:x.x.x.x.x.x.x.x.", 50},
      {"Two-step", "K:x.........x..... S:....x.......x... H:x.x.x.x.x.x.x.x.", 50},
      {"Rolling", "K:x.....x.x....... S:....x.......x... H:x.xxx.xxx.xxx.xx", 50},
      {"Choppy", "K:x.x.......x..... S:....x..x.x..x..x H:x.x.x.x.x.x.x.x.", 50}}},
    {"Liquid", KIT_ROCK, FILL_SNARE, {
      {"Two-step", "K:x.........x..... S:....X.......X... H:x.xxx.xxx.xxx.xx", 50},
      {"Ride", "K:x.........x..... S:....X.......X... O:x.x.x.x.x.x.x.x.", 50},
      {"Syncopated", "K:x.....x...x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Ghost notes", "K:x.........x..... S:....X..g.g..X..g H:x.x.x.x.x.x.x.x.", 50}}},
    {"Neurofunk", KIT_909, FILL_ROLL, {
      {"Standard", "K:X.........x..... S:....X.......X...", 50},
      {"Stepper", "K:x..x......x..... S:....X.......X... H:..x...x...x...x.", 50},
      {"Broken", "K:x.....x..x...... S:....X.......X..x H:x.x.x.x.x.x.x.x.", 50},
      {"Half-Time", "K:x.......x.....x. S:........X....... H:x.x.x.x.x.x.x.x.", 50}}}}},

  {"Rock", {
    {"Rock", KIT_ROCK, FILL_TOMS, {
      {"Standard", "K:x.......x.x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Half-Time", "K:x.......x....... S:........X....... H:x.x.x.x.x.x.x.x.", 50},
      {"Double-Time", "K:x...x...x...x... S:..x...x...x...x.", 50},
      {"Syncopated", "K:x.....x...x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Open hats", "K:x.......x.x..... S:....X.......X... O:x.x.x.x.x.x.x.x.", 50}}},
    {"Punk", KIT_ROCK, FILL_SNARE, {
      {"Standard", "K:x...x...x...x... S:....X.......X... H:x.x.x.x.x.x.x.x.", 50},
      {"Skank beat", "K:x...x...x...x... S:..x...x...x...x.", 50},
      {"Driving 16ths", "K:x.x.x.x.x.x.x.x. S:....X.......X...", 50},
      {"Half-Time", "K:x.......x.x..... S:........X....... H:x.x.x.x.x.x.x.x.", 50}}},
    {"Hard Rock", KIT_ROCK, FILL_TOMS, {
      {"Standard", "K:x.....x.x....... S:....X.......X... O:x.x.x.x.x.x.x.x.", 50},
      {"Shuffle", "K:x.......x.x..... S:....X.......X... H:x.x.x.x.x.x.x.x.", 66},
      {"Half-Time", "K:x.......x....... S:........X....... O:x.x.x.x.x.x.x.x.", 50},
      {"Double kick", "K:xx..xx..xx..xx.. S:....X.......X...", 50}}}}},

  {"Metal", {
    {"Doublebass", KIT_ROCK, FILL_METAL, {
      {"16th doublebass", "K:xxxxxxxxxxxxxxxx S:....X.......X...", 50},
      {"8th doublebass", "K:x.x.x.x.x.x.x.x. S:....X.......X...", 50},
      {"Gallop", "K:x.xxx.xxx.xxx.xx S:....X.......X...", 50},
      {"Breakdown", "K:xxxxxxxx......xx S:........X....... Y:x...............", 50}}},
    {"Thrash", KIT_ROCK, FILL_METAL, {
      {"Skank beat", "K:x...x...x...x... S:..X...X...X...X.", 50},
      {"Standard", "K:x.x...x.x.x...x. S:....X.......X...", 50},
      {"Gallop", "K:xx.xxx.xxx.xxx.x S:....X.......X...", 50},
      {"Double-Time", "K:x.x.x.x.x.x.x.x. S:..X...X...X...X.", 50}}},
    {"Blast Beat", KIT_ROCK, FILL_METAL, {
      {"Traditional", "K:x.x.x.x.x.x.x.x. S:.x.x.x.x.x.x.x.x", 50},
      {"Hammer", "K:.x.x.x.x.x.x.x.x S:x.x.x.x.x.x.x.x.", 50},
      {"Bomb", "K:xxxxxxxxxxxxxxxx S:..x...x...x...x.", 50},
      {"Gravity", "K:x.x.x.x.x.x.x.x. S:....X.......X... Y:x...............", 50}}},
    {"Djent", KIT_ROCK, FILL_METAL, {
      {"Chug", "K:x..x..x...x..x.. S:........X.......", 50},
      {"Polyrhythm", "K:x..x..x..x..x..x S:........X.......", 50},
      {"Groove", "K:xx..x..xx.x..x.. S:....X.......X...", 50},
      {"Breakdown", "K:x..x..x.x..x.... S:........X....... Y:x...............", 50}}}}},

  {"House", {
    {"Deep House", KIT_909, FILL_CLAP, {
      {"Four on the floor", "K:x...x...x...x... C:....x.......x... O:..x...x...x...x.", 50},
      {"Shuffle", "K:x...x...x...x... C:....x.......x... H:x.xxx.xxx.xxx.xx", 62},
      {"Rimshot", "K:x...x...x...x... R:...x..x....x.... O:..x...x...x...x.", 50},
      {"Breakdown", "C:....x.......x... O:..x...x...x...x.", 50}}},
    {"Acid House", KIT_909, FILL_CLAP, {
      {"Standard", "K:x...x...x...x... C:....x.......x... H:xxxxxxxxxxxxxxxx", 50},
      {"Offbeat", "K:x...x...x...x... S:....x.......x... O:..x...x...x...x.", 50},
      {"Syncopated", "K:x...x...x..xx... C:....x.......x... O:..x...x...x...x.", 50},
      {"Half-Time", "K:x.......x....... C:........x....... H:x.x.x.x.x.x.x.x.", 50}}},
    {"UK Garage", KIT_909, FILL_CLAP, {
      {"2-Step", "K:x.........x..x.. S:....x.......x... H:..x...x...x...x.", 62},
      {"Shuffle", "K:x......x..x..... S:....x.......x... H:x.xxx.xxx.xxx.xx", 62},
      {"4x4 Garage", "K:x...x...x...x... S:....x.......x... H:..xx..xx..xx..xx", 62},
      {"Broken", "K:x..x......x..x.. C:....x.......x..x H:..x...x...x...x.", 62}}}}},

  {"Reggae / Dub", {
    {"Roots", KIT_ROCK, FILL_REGGAE, {
      {"One drop", "K:........x....... R:........x....... H:x.x.x.x.x.x.x.x.", 62},
      {"Rockers", "K:x.......x....... R:....x.......x... H:x.x.x.x.x.x.x.x.", 62},
      {"Steppers", "K:x...x...x...x... R:........x....... H:..x...x...x...x.", 62},
      {"Half-Time", "K:x............... R:........x....... H:x...x...x...x...", 62}}},
    {"Dub", KIT_ROCK, FILL_REGGAE, {
      {"One drop", "K:........x....... S:........x....... O:..x...x...x...x.", 50},
      {"Steppers", "K:x...x...x...x... S:........x....... O:..x...x...x...x.", 50},
      {"Rimshot", "K:........x....... R:....x..x....x... H:x.x.x.x.x.x.x.x.", 62},
      {"Sparse", "K:........x....... S:........x.......", 50}}},
    {"Dancehall", KIT_808, FILL_REGGAE, {
      {"Dembow", "K:x...x...x...x... S:...x..x....x..x.", 50},
      {"Riddim", "K:x..x..x...x..... S:....x.......x... H:x.x.x.x.x.x.x.x.", 50},
      {"Clap", "K:x...x...x...x... C:...x..x....x..x. H:..x...x...x...x.", 50},
      {"Half-Time", "K:x.......x....... S:...x..x......... H:x.x.x.x.x.x.x.x.", 50}}}}},

  {"Latin", {
    {"Bossa Nova", KIT_ROCK, FILL_LATIN, {
      {"Standard", "K:x..xx..xx..xx..x R:x..x..x...x..x.. H:x.x.x.x.x.x.x.x.", 50},
      {"Clave 3-2", "K:x.......x.......x.......x....... R:x..x..x.............x.x......... H:x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 50},
      {"Brushes", "K:x..x....x..x.... S:..g...g...g...g. H:x.x.x.x.x.x.x.x.", 50},
      {"Sparse", "K:x.......x....... R:x..x..x...x..x..", 50}}},
    {"Samba", KIT_ROCK, FILL_LATIN, {
      {"Standard", "K:x..xx..xx..xx..x S:..x..x..x.x..x.. H:xxxxxxxxxxxxxxxx", 50},
      {"Batucada", "K:x..xx..xx..xx..x S:x.xgx.xgx.xgx.xg", 50},
      {"Partido alto", "K:x..x..x.x..x..x. R:..x..x..x..x.x..", 50},
      {"Half-Time", "K:x.......x....... S:..x..x.....x..x. H:x.x.x.x.x.x.x.x.", 50}}},
    {"Reggaeton", KIT_808, FILL_CLAP, {
      {"Dembow", "K:x...x...x...x... S:...x..x....x..x.", 50},
      {"Perreo", "K:x...x...x...x... C:...x..x....x..x. H:..x...x...x...x.", 50},
      {"Moombahton", "K:x...x...x...x... S:...x..x....x..x. O:..x...x...x...x.", 50},
      {"Half-Time", "K:x.......x....... S:...x..x......... H:x.x.x.x.x.x.x.x.", 50}}}}},

  {NULL, {{NULL, 0, NULL, {{NULL, NULL, 0}}}}}
};

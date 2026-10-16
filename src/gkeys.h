//
// C64 GrooveGoat - key codes
//
// The editor code of GoatTracker compares "raw" key codes against KEY_xxx
// constants. These keep the numbering of SDL 1.2 so that all existing logic
// works unchanged. Under SDL2 the codes are derived from the physical key
// position (scancode, US layout), so note entry works the same on every
// keyboard layout (QWERTY, QWERTZ, AZERTY ...).
//

#ifndef GKEYS_H
#define GKEYS_H

#define KEY_BACKSPACE    8
#define KEY_TAB          9
#define KEY_ENTER        13
#define KEY_PAUSE        19
#define KEY_ESC          27
#define KEY_SPACE        32
#define KEY_APOST1       39
#define KEY_COMMA        44
#define KEY_MINUS        45
#define KEY_PERIOD       46
#define KEY_COLON        46
#define KEY_SLASH        47
#define KEY_0            48
#define KEY_1            49
#define KEY_2            50
#define KEY_3            51
#define KEY_4            52
#define KEY_5            53
#define KEY_6            54
#define KEY_7            55
#define KEY_8            56
#define KEY_9            57
#define KEY_SEMICOLON    59
#define KEY_EQUAL        61
#define KEY_BRACKETL     91
#define KEY_BACKSLASH    92
#define KEY_BRACKETR     93
#define KEY_APOST2       96
#define KEY_A            97
#define KEY_B            98
#define KEY_C            99
#define KEY_D            100
#define KEY_E            101
#define KEY_F            102
#define KEY_G            103
#define KEY_H            104
#define KEY_I            105
#define KEY_J            106
#define KEY_K            107
#define KEY_L            108
#define KEY_M            109
#define KEY_N            110
#define KEY_O            111
#define KEY_P            112
#define KEY_Q            113
#define KEY_R            114
#define KEY_S            115
#define KEY_T            116
#define KEY_U            117
#define KEY_V            118
#define KEY_W            119
#define KEY_X            120
#define KEY_Y            121
#define KEY_Z            122
#define KEY_DEL          127
#define KEY_KP0          256
#define KEY_KP1          257
#define KEY_KP2          258
#define KEY_KP3          259
#define KEY_KP4          260
#define KEY_KP5          261
#define KEY_KP6          262
#define KEY_KP7          263
#define KEY_KP8          264
#define KEY_KP9          265
#define KEY_KPPERIOD     266
#define KEY_KPDIVIDE     267
#define KEY_KPMULTIPLY   268
#define KEY_KPMINUS      269
#define KEY_KPPLUS       270
#define KEY_KPENTER      271
#define KEY_KPEQUALS     272
#define KEY_UP           273
#define KEY_DOWN         274
#define KEY_RIGHT        275
#define KEY_LEFT         276
#define KEY_INS          277
#define KEY_HOME         278
#define KEY_END          279
#define KEY_PGUP         280
#define KEY_PGDN         281
#define KEY_F1           282
#define KEY_F2           283
#define KEY_F3           284
#define KEY_F4           285
#define KEY_F5           286
#define KEY_F6           287
#define KEY_F7           288
#define KEY_F8           289
#define KEY_F9           290
#define KEY_F10          291
#define KEY_F11          292
#define KEY_F12          293
#define KEY_NUMLOCK      300
#define KEY_CAPSLOCK     301
#define KEY_SCROLLLOCK   302
#define KEY_RIGHTSHIFT   303
#define KEY_LEFTSHIFT    304
#define KEY_RIGHTCTRL    305
#define KEY_CTRL         306
#define KEY_LEFTCTRL     306
#define KEY_RIGHTALT     307
#define KEY_ALT          308
#define KEY_RIGHTMETA    309
#define KEY_LEFTMETA     310
#define KEY_ISO          314
#define KEY_MENU         319

#define KEY_KPUP         KEY_KP8
#define KEY_KPDOWN       KEY_KP2
#define KEY_KPLEFT       KEY_KP4
#define KEY_KPRIGHT      KEY_KP6

#endif

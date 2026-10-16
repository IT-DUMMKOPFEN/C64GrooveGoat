//
// C64 GrooveGoat screen layout (character cell coordinates)
//

#ifndef GLAYOUT_H
#define GLAYOUT_H

// Toolbar & settings bar
#define TOOLBAR_Y 0
#define SETTINGS_Y 1

// Pattern editor
#define PATT_X 0
#define PATT_Y 2                      // panel header row
#define PATT_CHNHDR_Y 3               // channel header row
#define PATT_ROWS_Y 4                 // first pattern row
#define PATT_W 94
#define VISIBLECHANNELS 6
#define CHN_W 15                      // width of one channel column
#define CHN_X(i) (PATT_X + 2 + (i) * CHN_W)

// Right panel
#define RP_X 95
#define RP_W 45

#define ORD_Y 2                       // orderlist header
#define ORD_ROWS_Y 3
#define VISIBLEORDERCHNS 6
#define ORD_LABEL_W 5
#define ORD_X (RP_X + ORD_LABEL_W)

#define INS_Y 10                      // instrument header
#define INS_ROWS_Y 11
#define INS_COL1_X (RP_X + 1)
#define INS_COL2_X (RP_X + 23)
#define INS_VAL_OFS 16                // offset of the hex value inside a column

#define TBL_Y 17                      // table header
#define TBL_ROWS_Y 18
#define TBL_X(t) (RP_X + 1 + (t) * 11)

#define NAMES_Y 42                    // song info header
#define NAMES_ROWS_Y 43
#define NAMES_X (RP_X + 1)
#define NAMES_VAL_X (RP_X + 11)

// Bottom status area
#define STATUS_Y 47
#define POSITIONS_Y 48
#define MESSAGE_Y 49

#endif

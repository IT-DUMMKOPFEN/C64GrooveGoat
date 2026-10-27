/*
 * C64 GrooveGoat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//
// USBSID-Pico output
//

#ifndef GUSBSID_H
#define GUSBSID_H

#ifdef __cplusplus
extern "C" {
#endif

#define USBSID_MAXSIDS 8

// Board serial buffer size, terminating zero included
#define USBSID_SERIALSIZE 33

// SID types of USBSID_SIDINFO.type
#define USBSID_TYPE_UNKNOWN 0
#define USBSID_TYPE_NONE 1
#define USBSID_TYPE_8580 2
#define USBSID_TYPE_6581 3
#define USBSID_TYPE_FMOPL 4

// Highest SID number on a board a map token may name
#define USBSID_MAXSLOT 99

// Kinds returned by usbsid_parsetoken()
#define USBSID_TOKEN_AUTO 0
#define USBSID_TOKEN_OFF 1
#define USBSID_TOKEN_CHIP 2
#define USBSID_TOKEN_MALFORMED 3

typedef void (*USBSID_FRAMEFUNC)(void);

// One logical SID of an open board
typedef struct
{
  char serial[USBSID_SERIALSIZE]; // Board serial, empty when unknown
  int board;                      // Board number, 1 based, in open order
  int slot;                       // SID number on the board, 1 based, as numbered by the board
  int type;                       // USBSID_TYPE_*
  int pcb;                        // PCB version, e.g. 13 for v1.3, -1 unknown
} USBSID_SIDINFO;

/**
 * @brief: Open USBSID-Pico boards.
 *         With serials set, open those boards in that order, then every board the SID map
 *         (usbsid_setmap) names that serials lacks, in map order. Skip boards that are not
 *         attached or fail to open.
 * @param serials: comma separated board serials in SID order, NULL or empty opens every board
 * @return: 1 on success, 0 if no board with a SID could be opened
 */
int usbsid_open(const char *serials);

/**
 * @brief: List every logical SID of the open boards in logical order, FMopl slots included.
 *         No engine lock, main thread only.
 * @param info: output, may be NULL
 * @param max: number of entries info holds
 * @return: entries written, the total SID count when info is NULL, 0 when closed
 */
int usbsid_sidinfo(USBSID_SIDINFO *info, int max);

/**
 * @brief: Parse one SID map token. Pure function: no device access, no locks, any thread.
 *         Spaces, tabs and line ends are removed first. Forms: "" or "-" auto, "0" off,
 *         "SERIAL:N" chip with N all digits, 1 to USBSID_MAXSLOT, and SERIAL at most
 *         USBSID_SERIALSIZE - 1 bytes. Anything else is malformed.
 * @param tok: token text, not zero terminated beyond len
 * @param len: number of bytes of tok to read, stops early at a zero byte
 * @param serial: output for the chip kind, may be NULL, cleared otherwise
 * @param ssize: size of serial, USBSID_SERIALSIZE always suffices
 * @param slot: output for the chip kind, may be NULL, 0 otherwise
 * @return: USBSID_TOKEN_AUTO, USBSID_TOKEN_OFF, USBSID_TOKEN_CHIP or USBSID_TOKEN_MALFORMED
 */
int usbsid_parsetoken(const char *tok, int len, char *serial, int ssize, int *slot);

/**
 * @brief: Set the SID map: which chip plays each song SID.
 *         Token n is song SID n: "-" or empty = auto, "0" = off, "SERIAL:N" = SID N of board
 *         SERIAL. Missing trailing tokens are auto, tokens past USBSID_MAXSIDS are ignored.
 *         Store a copy, take effect at the following open or at the start of the following
 *         frame of a running engine: chips leaving the song are silenced, chips joining it get
 *         every register written again. Callable while closed, open or running.
 *         No engine lock, callable from any thread.
 * @param map: comma separated tokens, NULL or empty = every song SID auto
 */
void usbsid_setmap(const char *map);

/**
 * @brief: Resolve a SID map against a SID list, without device access or locks.
 *         Pass 1: explicit tokens in song SID order take their chip when it is listed, free
 *         and not FMopl; otherwise the token turns auto and sets its missing bit.
 *         Pass 2: auto song SIDs in song SID order take the first free non FMopl chip.
 *         Off song SIDs and song SIDs left without a free chip get -1. A chip plays one song
 *         SID at most. Malformed tokens are auto without a missing bit.
 * @param map: SID map, format of usbsid_setmap()
 * @param sids: SID list, e.g. from usbsid_sidinfo(), may be NULL when count is 0
 * @param count: number of entries in sids
 * @param target: output, target[n] = index into sids for song SID n, or -1
 * @param numtargets: number of song SIDs to resolve, tokens past it are ignored
 * @return: missing mask, bit n set when the explicit chip of song SID n was not available
 */
unsigned usbsid_resolvemap(const char *map, const USBSID_SIDINFO *sids, int count, int *target, int numtargets);

/**
 * @brief: Stop the frame thread, silence every SID and close the boards.
 */
void usbsid_close(void);

/**
 * @brief: Return 1 if boards are open.
 */
int usbsid_isopen(void);

/**
 * @brief: Return the number of SIDs on the open boards.
 */
int usbsid_totalsids(void);

/**
 * @brief: Toggle mono / stereo on every open board (v1.3+ PCB, older boards ignore it).
 *         Uses the board command channel, no engine lock, callable from the UI thread.
 *         Does not reboot the board.
 */
void usbsid_togglestereo(void);

/**
 * @brief: Set mono / stereo on every open board (v1.3+ PCB, older boards ignore it).
 *         Uses the board command channel, no engine lock, callable from the UI thread.
 *         Does not reboot the board and does not save the setting.
 * @param stereo: 0 = mono, nonzero = stereo
 */
void usbsid_setstereo(int stereo);

/**
 * @brief: Set the board clock and the frame rate, not callable from framefunc.
 * @param ntsc: 0 = PAL clock, 1 = NTSC clock
 * @param framerate: player calls per second
 */
void usbsid_settiming(unsigned ntsc, unsigned framerate);

/**
 * @brief: Set the number of SIDs the song plays on, chips leaving the song are silenced.
 *         Applied by the frame thread at frame start, callable from framefunc.
 * @param count: 1 to USBSID_MAXSIDS
 */
void usbsid_setsidcount(unsigned count);

/**
 * @brief: Start the frame thread, it calls framefunc once per frame paced on the wall clock.
 * @param framefunc: runs the player and calls usbsid_write() for the frame
 * @return: 1 on success
 */
int usbsid_start(USBSID_FRAMEFUNC framefunc);

/**
 * @brief: Stop the frame thread and silence every SID.
 */
void usbsid_stop(void);

/**
 * @brief: Queue a register write, call from framefunc only.
 *         Drop writes to song SIDs without a chip (off, or no free chip in the SID map).
 * @param sid: song SID, 0 based
 * @param reg: SID register 0x00 to 0x18
 * @param value: register value
 * @param cycle: cycle offset from the start of the frame
 */
void usbsid_write(unsigned sid, unsigned char reg, unsigned char value, unsigned cycle);

/**
 * @brief: Restart the write timeline at the following frame, keep queued writes.
 */
void usbsid_resync(void);

/**
 * @brief: Return 1 if an open board is unplugged or no longer accepts writes.
 *         Close and open again to recover.
 */
int usbsid_lost(void);

/**
 * @brief: Write the serials of the open boards, comma separated, in SID order.
 * @param buffer: output, empty string when no board is open
 * @param size: buffer size in bytes
 * @return: number of open boards
 */
int usbsid_openserials(char *buffer, int size);

/**
 * @brief: List the attached boards without opening them.
 * @param buffer: output, comma separated serials in USB bus and port order, may be NULL
 * @param size: buffer size in bytes
 * @return: number of attached boards
 */
int usbsid_attached(char *buffer, int size);

/**
 * @brief: Return 1 if every board in serials is attached, without opening any.
 * @param serials: comma separated board serials, NULL or empty means any board
 */
int usbsid_allattached(const char *serials);

#ifdef __cplusplus
}
#endif

#endif

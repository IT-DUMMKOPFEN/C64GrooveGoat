#ifndef GSOUND_H
#define GSOUND_H

#include "gusbsid.h"

#ifdef GSOUND_C

#ifdef __WIN32__
#include <winioctl.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "cwsid.h"
#endif

#endif

#define MINMIXRATE 11025
#define MAXMIXRATE 48000
#define MINBUF 20
#define MAXBUF 2000
#define DEFAULTBUF 100
#define DEFAULTMIXRATE 48000

#define PALFRAMERATE 50
#define PALCLOCKRATE 985248
#define NTSCFRAMERATE 60
#define NTSCCLOCKRATE 1022727

#define MIXBUFFERSIZE 65536

int sound_init(unsigned b, unsigned mr, unsigned writer, unsigned hardsid, unsigned m, unsigned ntsc, unsigned multiplier, unsigned catweasel, unsigned interpolate, unsigned customclockrate);
void sound_uninit(void);
void sound_suspend(void);
void sound_flush(void);

// USBSID-Pico output, stubs when built without USE_USBSID
enum { USBSID_EVENT_NONE, USBSID_EVENT_LOST, USBSID_EVENT_RECONNECTED };
int sound_usbsidpoll(void);
int sound_usbsidlost(void);
int sound_usbsidactive(void);
int sound_usbsidbuilt(void);
int sound_usbsidtotalsids(void);
int sound_usbsidboards(char *attached, int asize, char *inuse, int isize);
void sound_usbsidstereo(int stereo);

/**
 * @brief: Probe the attached boards and list every SID of them, for the Alt+U dialog.
 *         Output on (usbsid set): sound_uninit(), usbsid_close(), list the attached boards,
 *         open every attached board, usbsid_sidinfo(), close. The engine stays closed and the
 *         sound uninitialised: the caller restores with do_sound_init() (also after cancel).
 *         Output off: only list the attached boards, nothing is opened, returns 0.
 *         Main thread only, never under snd_lock (the frame thread takes it).
 *         Stub without USE_USBSID: empty attached, returns 0.
 * @param info: output, one entry per logical SID in board then slot order, may be NULL
 * @param max: number of entries info holds
 * @param attached: output, comma separated serials of the attached boards in USB bus and
 *                  port order, may be NULL; set to "" first
 * @param asize: attached buffer size in bytes
 * @return: SIDs of the opened boards (total count when info is NULL), 0 when output is off or
 *          no board could be opened
 */
int sound_usbsidprobe(USBSID_SIDINFO *info, int max, char *attached, int asize);

/**
 * @brief: Apply the dialog result: store output flag, board list and SID map in the globals
 *         usbsid, usbsidboards, usbsidmap, clear the lost state, close the boards and run
 *         do_sound_init(). Sets the status message: missing chip, song SIDs without chip, or
 *         "USBSID-Pico: N SID(s) connected"; "USBSID-Pico not found, using reSID" when no
 *         board opens. on == 0 closes the boards and plays on reSID, without message.
 *         boards and map may be the globals themselves. Does not save the config.
 *         Main thread only, never under snd_lock. Stub without USE_USBSID: returns 0.
 * @param on: 0 = reSID, nonzero = USBSID-Pico output
 * @param boards: comma separated board serials in SID order, NULL or empty = every board
 * @param map: SID map, format of usbsid_setmap(), NULL or empty = all auto
 * @return: 1 when the boards are playing, 0 otherwise
 */
int sound_usbsidapply(unsigned on, const char *boards, const char *map);

/**
 * @brief: Resolve a SID map against a SID list, thin wrapper of usbsid_resolvemap().
 *         Pure: no device access, no lock, any thread. Stub without USE_USBSID: every target
 *         -1, returns 0.
 * @param map: SID map, format of usbsid_setmap()
 * @param sids: SID list, e.g. from sound_usbsidprobe() or usbsid_sidinfo()
 * @param count: entries in sids
 * @param target: output, target[n] = index into sids for song SID n, or -1 (off or no chip)
 * @param numtargets: number of song SIDs to resolve
 * @return: missing mask, bit n set when the explicit chip of song SID n was not available
 */
unsigned sound_usbsidresolve(const char *map, const USBSID_SIDINFO *sids, int count, int *target, int numtargets);

/**
 * @brief: Chip that plays a song SID at this moment: usbsid_sidinfo() of the open boards resolved with
 *         the current usbsidmap and numsids (never the engine targets).
 *         Main thread only, not callable from the frame thread. Stub without USE_USBSID: -1.
 * @param songsid: song SID, 0 based
 * @param chip: output, may be NULL; zeroed when there is no chip
 * @return: index of the chip in the usbsid_sidinfo() list, -1 when the boards are closed
 *          (reSID, lost), songsid is outside 0..numsids-1, it is mapped off or has no free chip
 */
int sound_usbsidtarget(int songsid, USBSID_SIDINFO *chip);

/**
 * @brief: Number of open boards, 0 when closed or lost (usbsid_openserials count).
 *         Main thread only. Stub without USE_USBSID: 0.
 */
int sound_usbsidboardcount(void);

/**
 * @brief: Return 1 if song SID n is mapped off (token "0") in a SID map, parsed with
 *         usbsid_parsetoken(). Pure, any thread. Stub without USE_USBSID: 0.
 * @param map: SID map, format of usbsid_setmap(), may be NULL
 * @param n: song SID, 0 based
 */
int sound_usbsidmapoff(const char *map, int n);

#endif

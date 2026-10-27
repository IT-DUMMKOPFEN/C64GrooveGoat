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
// Keep one absolute cycle timeline per board. Each write waits for its own
// cycle, measured from the previous write on the same board, frame tail
// included. Start the timeline one lead time ahead of the board to absorb
// host scheduling jitter. Pace frames on the wall clock.
//

#include "gusbsid.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Driver headers last, the Windows build pulls in windows.h through them
#include "driver/usbsid/src/USBSID.h"
#include "driver/usbsid/src/USBSID_Manager.h"

namespace
{
  // Cycles a write takes on the board on top of its wait
  const uint64_t BOARD_WRITE_OVERHEAD = 2;

  // Shortest time between two writes on the board
  const uint64_t BOARD_WRITE_MIN = 7;

  // Waits above this are shortened while the board lags
  const uint64_t CATCH_UP_MIN_WAIT = 4096;

  // Largest wait a cycled write carries
  const uint64_t MAX_WAIT_CYCLES = 0xffff;

  // Free ring bytes required before queueing, one write takes 4
  const int RING_LOW_WATER = 1024;

  // Frames run in one go at most, a longer stall restarts the pacing
  const unsigned PACE_MAX_FRAMES = 3;
  const double PACE_RESTART_FRAMES = 8.0;

  // Board timeline head start on the wall clock
  const unsigned LEAD_TIME_MS = 30;

  const long PAL_CLOCK = 985248;
  const long NTSC_CLOCK = 1022727;

  const int SID_TYPE_FMOPL = USBSID_TYPE_FMOPL;

  const unsigned char NUM_REGS = 0x19;

  // Clear gates and volume first, the last flush may leave writes behind
  const unsigned char SILENCE_ORDER[NUM_REGS] =
  {
    0x04, 0x0b, 0x12, 0x18,
    0x00, 0x01, 0x02, 0x03, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0c, 0x0d,
    0x0e, 0x0f, 0x10, 0x11, 0x13, 0x14,
    0x15, 0x16, 0x17
  };

  typedef std::chrono::steady_clock Clock;

  struct Target
  {
    int logical;              // Logical SID in the manager, -1 = no chip
    int board;                // Board index
    unsigned char base;       // Board register block of the SID
    int usable;               // Index in Engine::usable
  };

  const Target NO_TARGET = { -1, -1, 0, -1 };

  enum TokenKind { TOKEN_AUTO, TOKEN_OFF, TOKEN_CHIP };

  struct MapToken
  {
    TokenKind kind = TOKEN_AUTO;
    std::string serial;       // TOKEN_CHIP: board serial
    int slot = 0;             // TOKEN_CHIP: SID number on the board, 1 based
  };

  // SID map, kept across close and open, applied by the frame thread
  std::mutex mapmutex;
  std::string mapstring;
  std::atomic<unsigned> mapgen{0};

  struct BoardState
  {
    uint64_t clock = 0;           // Timeline cycle at which the board is free again
    bool synced = false;          // Timeline is running
    bool stalled = false;         // Ring stopped draining
    bool catchup = false;         // Board lags behind the timeline
    int logical = -1;             // Any SID on the board, routes the write batch
    size_t lastbatch = 0;         // Bytes queued by the previous frame
    std::vector<uint8_t> pending; // Writes of the current frame: reg, value, wait hi, wait lo
  };

  struct Engine
  {
    USBSID_Manager manager;
    std::mutex mutex;             // Held by the frame thread for each run of frames
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<unsigned> wantsidcount{1};  // Applied by the frame thread
    USBSID_FRAMEFUNC framefunc = nullptr;

    std::vector<Target> usable;   // Every SID that takes SID writes, in board order
    std::vector<int> shadow;      // Last value sent per usable SID and register, -1 unknown
    std::vector<Target> targets;  // Song SID n plays on targets[n]
    std::vector<BoardState> boards;
    std::vector<USBSID_SIDINFO> sids;  // Every logical SID, FMopl included
    std::vector<int> usableof;    // Logical SID -> index in usable, -1 for FMopl

    std::string map;              // SID map in use
    unsigned mapgen = 0;          // Generation of map
    std::vector<int> printedtargets;  // Logical SID per song SID, last logged
    unsigned printedmissing = 0;

    long clockrate = PAL_CLOCK;
    unsigned framerate = 50;
    unsigned sidcount = 1;
    double framecycles = PAL_CLOCK / 50.0;
    double framefraction = 0.0;
    uint64_t framebase = 0;       // Timeline cycle at the start of the current frame
    uint64_t leadcycles = 1;

    bool pacerunning = false;
    Clock::time_point pacestart;
    double pacedcycles = 0.0;
    double idleseconds = 0.0;     // Wall clock time until a frame is due
    bool warnedsids = false;
  };

  Engine *engine = nullptr;

  std::vector<std::string> splitserials(const char *list)
  {
    std::vector<std::string> serials;
    std::string current;

    if (!list) return serials;

    for (const char *p = list; ; p++)
    {
      if (*p == ',' || *p == 0)
      {
        if (!current.empty()) serials.push_back(current);
        current.clear();
        if (*p == 0) break;
      }
      else if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        current += *p;
    }
    return serials;
  }

  // Parse one map token, whitespace removed; malformed tokens are auto
  MapToken parsetoken(const std::string &text)
  {
    MapToken token;
    char serial[USBSID_SERIALSIZE];
    int slot = 0;

    switch (usbsid_parsetoken(text.c_str(), (int)text.size(), serial, sizeof serial, &slot))
    {
      case USBSID_TOKEN_OFF:
        token.kind = TOKEN_OFF;
        break;
      case USBSID_TOKEN_CHIP:
        token.kind = TOKEN_CHIP;
        token.serial = serial;
        token.slot = slot;
        break;
      default:
        break;
    }
    return token;
  }

  // Split a SID map into at most USBSID_MAXSIDS tokens
  std::vector<MapToken> parsemap(const char *map)
  {
    std::vector<MapToken> tokens;
    std::string current;

    if (!map || !*map) return tokens;

    for (const char *p = map; ; p++)
    {
      if (*p == ',' || *p == 0)
      {
        if (tokens.size() < USBSID_MAXSIDS) tokens.push_back(parsetoken(current));
        current.clear();
        if (*p == 0) break;
      }
      else if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        current += *p;
    }
    return tokens;
  }

  // Mark every register of a board unknown, forcing out its following writes
  void forget(Engine &e, int board)
  {
    for (size_t i = 0; i < e.usable.size(); i++)
    {
      if (e.usable[i].board != board) continue;
      for (unsigned char reg = 0; reg < NUM_REGS; reg++)
        e.shadow[i * NUM_REGS + reg] = -1;
    }
  }

  // Mark every register of one usable SID unknown
  void forgetsid(Engine &e, int usable)
  {
    for (unsigned char reg = 0; reg < NUM_REGS; reg++)
      e.shadow[usable * NUM_REGS + reg] = -1;
  }

  bool plays(const std::vector<Target> &targets, int usable)
  {
    for (const Target &t : targets)
    {
      if (t.logical >= 0 && t.usable == usable) return true;
    }
    return false;
  }

  void unsync(Engine &e)
  {
    for (BoardState &state : e.boards)
      state.synced = false;
    e.pacerunning = false;
  }

  void applyclock(Engine &e)
  {
    // Force: PAL/NTSC can change while the boards stay open
    e.manager.SetClockRateAll(e.clockrate, true, true);
    e.framecycles = (double)e.clockrate / (double)e.framerate;
    e.leadcycles = ((uint64_t)LEAD_TIME_MS * (uint64_t)e.clockrate) / 1000;
    if (!e.leadcycles) e.leadcycles = 1;
    unsync(e);
  }

  void silence(Engine &e, size_t first)
  {
    for (size_t i = first; i < e.usable.size(); i++)
    {
      const Target &t = e.usable[i];

      if (e.manager.RingFreeBytes(t.logical) < NUM_REGS * 4 + 16)
      {
        forget(e, t.board);
        continue;
      }
      for (unsigned char reg : SILENCE_ORDER)
      {
        e.manager.WriteRingCycled(t.logical, (uint8_t)(t.base + reg), 0, 0);
        e.shadow[i * NUM_REGS + reg] = 0;
      }
    }
    for (BoardState &state : e.boards)
    {
      state.synced = false;
      state.pending.clear();
    }
  }

  void flushsilence(Engine &e, std::unique_lock<std::mutex> &lock)
  {
    // Repeat the flush, one flush sends a single packet
    for (int i = 0; i < 4; i++)
    {
      e.manager.FlushAll();
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      lock.lock();
    }
  }

  void buildtargets(Engine &e)
  {
    const std::vector<MapToken> tokens = parsemap(e.map.c_str());
    int chip[USBSID_MAXSIDS];
    const unsigned missing = usbsid_resolvemap(e.map.c_str(), e.sids.data(), (int)e.sids.size(), chip, (int)e.sidcount);
    unsigned wanted = 0;
    unsigned placed = 0;
    std::vector<int> logicals;

    e.targets.clear();
    for (unsigned n = 0; n < e.sidcount; n++)
    {
      const bool off = n < tokens.size() && tokens[n].kind == TOKEN_OFF;
      Target t = NO_TARGET;

      if (chip[n] >= 0 && e.usableof[chip[n]] >= 0)
        t = e.usable[e.usableof[chip[n]]];
      e.targets.push_back(t);
      logicals.push_back(t.logical);
      if (!off) wanted++;
      if (t.logical >= 0) placed++;
    }

    // Log the routing once per change
    if (logicals != e.printedtargets || missing != e.printedmissing)
    {
      for (unsigned n = 0; n < e.sidcount; n++)
      {
        if (missing & (1u << n))
          fprintf(stderr, "USBSID-Pico: chip %s:%d for song SID %u not available, using auto\n",
            tokens[n].serial.c_str(), tokens[n].slot, n + 1);
      }
      for (unsigned n = 0; n < e.sidcount; n++)
      {
        const Target &t = e.targets[n];
        if (t.logical >= 0)
          fprintf(stderr, "USBSID-Pico: song SID %u on board %d SID %d\n", n + 1, t.board + 1, t.base / 0x20 + 1);
        else if (n < tokens.size() && tokens[n].kind == TOKEN_OFF)
          fprintf(stderr, "USBSID-Pico: song SID %u off\n", n + 1);
      }
      e.printedtargets = logicals;
      e.printedmissing = missing;
    }

    if (placed < wanted && !e.warnedsids)
    {
      fprintf(stderr, "USBSID-Pico: song uses %u SIDs, %u available, the rest stays silent\n",
        wanted, placed);
      e.warnedsids = true;
    }
  }

  void queuewrite(Engine &e, const Target &t, unsigned char reg, unsigned char value, uint64_t now, unsigned cycle)
  {
    BoardState &state = e.boards[t.board];
    uint64_t wait;

    if (!state.synced || now > state.clock + MAX_WAIT_CYCLES)
    {
      // Start a new timeline on an idle board, the lead time absorbs feed jitter
      wait = e.leadcycles + cycle;
      state.clock = now;
      state.synced = true;
      state.catchup = false;
    }
    else
    {
      // Aim the write at its cycle, minus the board's own write overhead
      const uint64_t landat = state.clock + BOARD_WRITE_OVERHEAD;
      wait = now > landat ? now - landat : 0;

      const uint64_t busy = wait + BOARD_WRITE_OVERHEAD;
      state.clock += busy > BOARD_WRITE_MIN ? busy : BOARD_WRITE_MIN;

      // Shorten a long wait to let a lagging board catch up, the timeline keeps its place
      if (state.catchup && wait > CATCH_UP_MIN_WAIT)
        wait -= wait >> 6;
    }

    if (wait > MAX_WAIT_CYCLES) wait = MAX_WAIT_CYCLES;

    state.pending.push_back((uint8_t)(t.base + reg));
    state.pending.push_back(value);
    state.pending.push_back((uint8_t)(wait >> 8));
    state.pending.push_back((uint8_t)(wait & 0xff));
  }

  // Rebuild the targets for a new SID count or SID map, call at frame start
  void retarget(Engine &e, unsigned count, bool mapchanged)
  {
    const std::vector<Target> old = e.targets;

    e.sidcount = count;
    e.warnedsids = false;
    buildtargets(e);

    // Silence chips leaving the song, they keep their last notes otherwise
    for (const Target &t : old)
    {
      if (t.logical < 0 || plays(e.targets, t.usable)) continue;
      for (unsigned char reg : SILENCE_ORDER)
      {
        queuewrite(e, t, reg, 0, e.framebase, 0);
        e.shadow[t.usable * NUM_REGS + reg] = 0;
      }
    }

    // Write every register of a chip joining through the map
    if (mapchanged)
    {
      for (const Target &t : e.targets)
      {
        if (t.logical >= 0 && !plays(old, t.usable))
          forgetsid(e, t.usable);
      }
    }
  }

  unsigned framesdue(Engine &e)
  {
    const Clock::time_point now = Clock::now();

    if (!e.pacerunning)
    {
      e.pacerunning = true;
      e.pacestart = now;
      e.pacedcycles = 0.0;
    }

    const double elapsed = std::chrono::duration<double>(now - e.pacestart).count() * (double)e.clockrate;

    // Restart pacing and the board timelines when too far behind to catch up
    if (elapsed - e.pacedcycles > PACE_RESTART_FRAMES * e.framecycles)
    {
      unsync(e);
      e.pacerunning = true;
      e.pacestart = now;
      e.pacedcycles = e.framecycles;
      return 1;
    }

    unsigned frames = 0;
    while (e.pacedcycles <= elapsed && frames < PACE_MAX_FRAMES)
    {
      e.pacedcycles += e.framecycles;
      frames++;
    }
    e.idleseconds = (e.pacedcycles - elapsed) / (double)e.clockrate;
    return frames;
  }

  void runframe(Engine &e)
  {
    const unsigned sidcount = e.wantsidcount;
    if (mapgen != e.mapgen)
    {
      {
        std::lock_guard<std::mutex> lock(mapmutex);
        e.map = mapstring;
        e.mapgen = mapgen;
      }
      retarget(e, sidcount, true);
    }
    else if (sidcount != e.sidcount)
      retarget(e, sidcount, false);

    // Flag a board lagging behind: more than two frames waiting in the driver ring
    for (BoardState &state : e.boards)
    {
      if (state.logical < 0) continue;
      const size_t used = (size_t)(USBSID_NS::default_ring_size - 1 - e.manager.RingFreeBytes(state.logical));
      state.catchup = state.lastbatch > 0 && used > 2 * state.lastbatch;
    }

    e.framefraction += e.framecycles;
    const uint64_t cycles = (uint64_t)e.framefraction;
    e.framefraction -= (double)cycles;

    if (e.framefunc) e.framefunc();

    e.framebase += cycles;

    // Send one batch per board: one lock, one driver thread wakeup
    for (size_t b = 0; b < e.boards.size(); b++)
    {
      BoardState &state = e.boards[b];

      if (state.pending.empty()) continue;

      // Drop writes instead of overwriting unsent data, the driver ring has no overflow guard
      if (e.manager.RingFreeBytes(state.logical) < (int)state.pending.size() + RING_LOW_WATER)
      {
        if (!state.stalled)
          fprintf(stderr, "USBSID-Pico: board %d stopped accepting writes, dropping writes\n", (int)b + 1);
        state.stalled = true;
        state.synced = false;
        forget(e, (int)b);
      }
      else
      {
        state.stalled = false;
        state.lastbatch = state.pending.size();
        e.manager.WriteRingCycledN(state.logical, state.pending.data(), (int)(state.pending.size() / 4));
      }
      state.pending.clear();
    }

    e.manager.FlushAll();
  }

  void framethread(Engine *e)
  {
    while (e->running)
    {
      unsigned due;
      double idle;
      {
        std::lock_guard<std::mutex> lock(e->mutex);
        due = framesdue(*e);
        for (unsigned i = 0; i < due; i++)
          runframe(*e);
        idle = e->idleseconds;
      }
      if (!due)
      {
        // Wake early, a late wake costs lead time
        long us = (long)(idle * 1e6) - 500;
        if (us < 200) us = 200;
        if (us > 5000) us = 5000;
        std::this_thread::sleep_for(std::chrono::microseconds(us));
      }
    }
  }
}

extern "C" {

int usbsid_parsetoken(const char *tok, int len, char *serial, int ssize, int *slot)
{
  char text[USBSID_SERIALSIZE + 8];
  int n = 0, colon = -1, value = 0, i;

  if (serial && ssize > 0) serial[0] = 0;
  if (slot) *slot = 0;
  if (!tok || len < 0) return USBSID_TOKEN_MALFORMED;

  // Strip spaces, tabs and line ends; a token this long cannot be valid
  for (i = 0; i < len && tok[i]; i++)
  {
    if (tok[i] == ' ' || tok[i] == '\t' || tok[i] == '\r' || tok[i] == '\n') continue;
    if (n >= (int)sizeof text - 1) return USBSID_TOKEN_MALFORMED;
    text[n++] = tok[i];
  }
  text[n] = 0;

  if (n == 0 || !strcmp(text, "-")) return USBSID_TOKEN_AUTO;
  if (!strcmp(text, "0")) return USBSID_TOKEN_OFF;

  for (i = 0; i < n; i++)
    if (text[i] == ':') colon = i;
  if (colon <= 0 || colon + 1 >= n || colon >= USBSID_SERIALSIZE) return USBSID_TOKEN_MALFORMED;
  for (i = colon + 1; i < n; i++)
  {
    if (text[i] < '0' || text[i] > '9') return USBSID_TOKEN_MALFORMED;
    value = value * 10 + (text[i] - '0');
    if (value > USBSID_MAXSLOT) return USBSID_TOKEN_MALFORMED;
  }
  if (value < 1) return USBSID_TOKEN_MALFORMED;
  if (serial)
  {
    if (ssize <= colon) return USBSID_TOKEN_MALFORMED;
    memcpy(serial, text, colon);
    serial[colon] = 0;
  }
  if (slot) *slot = value;
  return USBSID_TOKEN_CHIP;
}

int usbsid_open(const char *serials)
{
  if (engine) return 1;

  Engine *e = new Engine();
  {
    std::lock_guard<std::mutex> lock(mapmutex);
    e->map = mapstring;
    e->mapgen = mapgen;
  }

  // Boards named in the map follow the requested boards
  std::vector<std::string> wanted = splitserials(serials);
  if (!wanted.empty())
  {
    for (const MapToken &token : parsemap(e->map.c_str()))
    {
      if (token.kind != TOKEN_CHIP) continue;
      bool listed = false;
      for (const std::string &serial : wanted)
        listed |= serial == token.serial;
      if (!listed) wanted.push_back(token.serial);
    }
  }

  if (!e->manager.OpenAll(wanted, true, true))
  {
    fprintf(stderr, "USBSID-Pico: no board could be opened\n");
    delete e;
    return 0;
  }

  const auto &boards = e->manager.Boards();
  const auto &map = e->manager.LogicalMap();

  for (size_t i = 0; i < map.size(); i++)
  {
    const auto &board = boards[map[i].board_index];
    USBSID_SIDINFO info = {};

    snprintf(info.serial, sizeof info.serial, "%s", board.serial.c_str());
    info.board = map[i].board_index + 1;
    info.slot = map[i].local_slot + 1;
    info.type = map[i].sid_type;
    info.pcb = board.pcbversion;
    e->sids.push_back(info);

    if (map[i].sid_type == SID_TYPE_FMOPL)
    {
      e->usableof.push_back(-1);
      continue;
    }
    e->usableof.push_back((int)e->usable.size());
    e->usable.push_back({ (int)i, map[i].board_index, (unsigned char)(map[i].local_slot * 0x20), (int)e->usable.size() });
  }
  e->shadow.assign(e->usable.size() * NUM_REGS, -1);

  if (e->usable.empty())
  {
    fprintf(stderr, "USBSID-Pico: opened boards have no SID configured\n");
    e->manager.CloseAll();
    delete e;
    return 0;
  }

  e->boards.assign(boards.size(), BoardState());
  for (const Target &t : e->usable)
  {
    if (e->boards[t.board].logical < 0)
      e->boards[t.board].logical = t.logical;
  }

  for (const auto &board : boards)
    fprintf(stderr, "USBSID-Pico: board %d [%s] with %d SID(s)\n", board.index + 1, board.serial.c_str(), board.numsids);
  for (size_t i = 0; i < e->usable.size(); i++)
    fprintf(stderr, "USBSID-Pico: SID %d on board %d SID %d\n", (int)i + 1, e->usable[i].board + 1, e->usable[i].base / 0x20 + 1);

  e->manager.ResetAllRegistersAll();
  applyclock(*e);
  buildtargets(*e);

  engine = e;
  return 1;
}

void usbsid_close(void)
{
  if (!engine) return;

  usbsid_stop();

  Engine *e = engine;
  engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(e->mutex);
    e->manager.ResetAllRegistersAll();
    e->manager.CloseAll();
  }
  delete e;
}

int usbsid_sidinfo(USBSID_SIDINFO *info, int max)
{
  if (!engine) return 0;

  // Fixed between open and close, no engine lock needed
  const int total = (int)engine->sids.size();
  if (!info) return total;

  int n = 0;
  for (; n < total && n < max; n++)
    info[n] = engine->sids[n];
  return n;
}

void usbsid_setmap(const char *map)
{
  std::lock_guard<std::mutex> lock(mapmutex);
  const std::string text = map ? map : "";

  if (text == mapstring) return;
  mapstring = text;
  mapgen++;
}

unsigned usbsid_resolvemap(const char *map, const USBSID_SIDINFO *sids, int count, int *target, int numtargets)
{
  if (!target || numtargets <= 0) return 0;
  if (!sids || count < 0) count = 0;

  const std::vector<MapToken> tokens = parsemap(map);
  std::vector<char> taken((size_t)count, 0);
  unsigned missing = 0;

  for (int n = 0; n < numtargets; n++)
    target[n] = -1;

  // Pass 1: explicit chips in song SID order, a missing or taken chip turns auto
  for (int n = 0; n < numtargets && n < (int)tokens.size(); n++)
  {
    const MapToken &token = tokens[n];
    if (token.kind != TOKEN_CHIP) continue;

    for (int i = 0; i < count; i++)
    {
      if (taken[i] || sids[i].type == SID_TYPE_FMOPL || sids[i].slot != token.slot) continue;
      if (strncmp(sids[i].serial, token.serial.c_str(), USBSID_SERIALSIZE) != 0) continue;
      target[n] = i;
      taken[i] = 1;
      break;
    }
    if (target[n] < 0 && n < 32) missing |= 1u << n;
  }

  // Pass 2: auto song SIDs take the first free chip in logical order
  for (int n = 0; n < numtargets; n++)
  {
    if (target[n] >= 0) continue;
    if (n < (int)tokens.size() && tokens[n].kind == TOKEN_OFF) continue;

    for (int i = 0; i < count; i++)
    {
      if (taken[i] || sids[i].type == SID_TYPE_FMOPL) continue;
      target[n] = i;
      taken[i] = 1;
      break;
    }
  }
  return missing;
}

int usbsid_isopen(void)
{
  return engine != nullptr;
}

int usbsid_totalsids(void)
{
  return engine ? (int)engine->usable.size() : 0;
}

void usbsid_togglestereo(void)
{
  if (!engine) return;

  // Command channel write, the engine lock is held by the frame thread across frames
  for (int i = 0; i < engine->manager.BoardCount(); i++)
  {
    USBSID_NS::USBSID_Class *board = engine->manager.Board(i);
    if (board) board->USBSID_ToggleStereo();
  }
}

void usbsid_setstereo(int stereo)
{
  if (!engine) return;

  // Command channel write, the engine lock is held by the frame thread across frames
  for (int i = 0; i < engine->manager.BoardCount(); i++)
  {
    USBSID_NS::USBSID_Class *board = engine->manager.Board(i);
    if (board) board->USBSID_SetStereo(stereo ? 1 : 0);
  }
}

void usbsid_settiming(unsigned ntsc, unsigned framerate)
{
  if (!engine) return;

  Engine &e = *engine;
  std::lock_guard<std::mutex> lock(e.mutex);
  const long clockrate = ntsc ? NTSC_CLOCK : PAL_CLOCK;

  if (!framerate) framerate = 50;
  if (clockrate == e.clockrate && framerate == e.framerate) return;

  e.clockrate = clockrate;
  e.framerate = framerate;
  applyclock(e);
}

void usbsid_setsidcount(unsigned count)
{
  if (!engine) return;

  if (count < 1) count = 1;
  if (count > USBSID_MAXSIDS) count = USBSID_MAXSIDS;
  engine->wantsidcount = count;
}

int usbsid_start(USBSID_FRAMEFUNC framefunc)
{
  if (!engine) return 0;
  if (engine->running) return 1;

  Engine &e = *engine;
  {
    std::lock_guard<std::mutex> lock(e.mutex);
    e.framefunc = framefunc;
    unsync(e);
  }
  e.running = true;
#if defined(__cpp_exceptions)
  try
  {
    e.thread = std::thread(framethread, &e);
  }
  catch (...)
  {
    e.running = false;
    return 0;
  }
#else
  e.thread = std::thread(framethread, &e);
#endif
  return 1;
}

void usbsid_stop(void)
{
  if (!engine) return;

  Engine &e = *engine;
  if (e.running)
  {
    e.running = false;
    if (e.thread.joinable()) e.thread.join();
  }

  std::unique_lock<std::mutex> lock(e.mutex);
  e.framefunc = nullptr;
  silence(e, 0);
  flushsilence(e, lock);
}

void usbsid_write(unsigned sid, unsigned char reg, unsigned char value, unsigned cycle)
{
  if (!engine || sid >= engine->targets.size() || reg >= NUM_REGS) return;

  Engine &e = *engine;
  const Target &t = e.targets[sid];
  if (t.logical < 0) return;

  int &last = e.shadow[t.usable * NUM_REGS + reg];

  // Skip unchanged values, rewriting a SID register has no effect and costs board time
  if (last == value) return;
  last = value;
  queuewrite(e, t, reg, value, e.framebase + cycle, cycle);
}

void usbsid_resync(void)
{
  if (!engine) return;

  std::lock_guard<std::mutex> lock(engine->mutex);
  unsync(*engine);
}

int usbsid_lost(void)
{
  // Lost flags are atomic, no engine lock needed
  return engine && engine->manager.AnyBoardLost() ? 1 : 0;
}

int usbsid_openserials(char *buffer, int size)
{
  std::string list;
  int count = 0;

  if (engine)
  {
    for (const auto &board : engine->manager.Boards())
    {
      if (!list.empty()) list += ',';
      list += board.serial;
      count++;
    }
  }
  if (buffer && size > 0)
    snprintf(buffer, (size_t)size, "%s", list.c_str());
  return count;
}

int usbsid_attached(char *buffer, int size)
{
  const std::vector<USBSID_NS::USBSID_DeviceInfo> found = USBSID_Manager::Enumerate();
  std::string list;

  for (const auto &device : found)
  {
    if (!list.empty()) list += ',';
    list += device.serial;
  }
  if (buffer && size > 0)
    snprintf(buffer, (size_t)size, "%s", list.c_str());
  return (int)found.size();
}

int usbsid_allattached(const char *serials)
{
  const std::vector<USBSID_NS::USBSID_DeviceInfo> found = USBSID_Manager::Enumerate();
  const std::vector<std::string> wanted = splitserials(serials);

  if (wanted.empty()) return found.empty() ? 0 : 1;

  for (const std::string &serial : wanted)
  {
    bool present = false;
    for (const auto &device : found)
    {
      if (device.serial == serial)
      {
        present = true;
        break;
      }
    }
    if (!present) return 0;
  }
  return 1;
}

}

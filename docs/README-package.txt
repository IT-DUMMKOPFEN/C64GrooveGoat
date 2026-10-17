C64 GrooveGoat 0.8b
===================

A multi-SID music editor for the Commodore 64 by IT-Dummkopfen,
based on GoatTracker 2 by Lasse Oorni (Cadaver) and its "Silver Fork".

- Songs with 1, 2, 4 or 8 SID chips (3 to 24 voices), all played in the editor
- SID addresses $D400 $D420 $D440 $D460 $D480 $D4A0 $D4C0 $D4E0 (changeable)
- All GoatTracker 2 functions: patterns, orderlists, instruments, wave/pulse/
  filter/speed tables, multispeed, 6581/8580, PAL/NTSC, undo
- More than 5000 ready-made instruments for the 6581 and the 8580 with preview
- Loads and saves GoatTracker 2 (.sng), GoatTracker Stereo (.sng) and the
  C64 GrooveGoat format (.ggt, for 4 and 8 SIDs); instruments (.ins)
- Export: SID files (PSID v2 / v3 / v5 multi-SID) with selectable SID
  addresses, runnable C64 programs (.prg) with a player screen, raw PRG/BIN
- Night theme in the original C64 blue and a day theme
- Built-in manual (Help -> Manual) and keyboard reference (F12)


Starting
--------

Windows
  Unpack the ZIP file and start GrooveGoat.exe. Keep the "instruments"
  folder next to GrooveGoat.exe. Windows 10 or newer (64-bit).
  If Windows SmartScreen warns about an unknown publisher, click
  "More info" and "Run anyway".

macOS (Apple Silicon, macOS 11 or newer)
  Unpack the ZIP file and move "C64 GrooveGoat.app" to the Applications
  folder. The app is not notarized by Apple, so the first start is blocked:
  - macOS 15 and newer: open the app once, then go to System Settings ->
    Privacy & Security, scroll down and click "Open Anyway".
  - macOS 11-14: right-click (or CTRL-click) the app, choose "Open" and
    confirm with "Open".
  Alternatively run this command in the Terminal once:
    xattr -cr "/Applications/C64 GrooveGoat.app"

Linux (x86-64)
  Unpack the archive and start ./groovegoat. The SDL 2 library is needed:
    Debian/Ubuntu:  sudo apt install libsdl2-2.0-0
    Fedora:         sudo dnf install SDL2
    Arch:           sudo pacman -S sdl2
  ./install.sh adds a menu entry and an icon for the current user.
  Needs glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Fedora 35 or newer).


First steps
-----------

1. Open examples/solar_voyage_2sid.sng (Open button or F10) and press F1
   to hear two SIDs (6 voices) playing together. F4 stops.
2. Click "New" and choose the number of SIDs for your own song.
3. Click "Library" in the instrument panel to pick instruments; SPACE plays a
   preview, ENTER loads.
4. Enter notes in the pattern editor with the computer keyboard like on a
   piano (Z S X D C V ... and Q 2 W 3 E R ...). F1 plays, F4 stops.
5. "Export SID" writes a SID file, "Export PRG" a program for a real C64 or
   an emulator (LOAD"NAME",8,1 and RUN).

Click "Help" for the keyboard reference and "Manual" for the full manual.


Files in this package
---------------------

  instruments/   instrument library ("SIDGen instruments for GoatTracker 2"),
                 6581/ for the old and 8580/ for the new SID chip
  examples/      example songs: "Solar Voyage" by IT-Dummkopfen (2 SIDs, also as
                 .sid and runnable .prg) and the GoatTracker 2 example songs
  Manual.txt     the manual (also built into the program)
  CREDITS.txt    credits and licenses of the used software
  COPYING.txt    GNU General Public License


License
-------

C64 GrooveGoat is free software under the GNU General Public License v2 or
later. The C64 playroutines and the PRG player do not fall under the GPL:
the music you create and export is yours to use for any purpose.

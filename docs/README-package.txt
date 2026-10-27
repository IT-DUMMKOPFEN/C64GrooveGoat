C64 GrooveGoat 0.9b
===================

A multi-SID music editor for the Commodore 64 by IT-Dummkopfen,
based on GoatTracker 2 by Lasse Oorni (Cadaver) and its "Silver Fork".

- Songs with 1, 2, 4 or 8 SID chips (3 to 24 voices), all played in the editor
- SID addresses $D400 $D420 $D440 $D460 $D480 $D4A0 $D4C0 $D4E0 (changeable)
- All GoatTracker 2 functions: patterns, orderlists, instruments, wave/pulse/
  filter/speed tables, multispeed, 6581/8580, PAL/NTSC
- Undo (CTRL+Z) and redo (CTRL+Y), keyboard layout switch EN/DE for QWERTZ
- More than 5000 ready-made instruments for the 6581 and the 8580 with search,
  favourites and preview
- Random instrument generator: bass, drums, woodwinds, guitars, brass, strings,
  leads, pads, arpeggios and more, with SID filter options
- Graphical ADSR envelope
- Loads and saves GoatTracker 2 (.sng), GoatTracker Stereo (.sng) and the
  C64 GrooveGoat format (.ggt, for 4 and 8 SIDs); instruments (.ins)
- Backup copies: NAME.bak on every save, automatic backup every 2 minutes,
  restore after a crash
- Export: SID files (PSID v2 / v3 / v5 multi-SID) with selectable SID
  addresses, runnable C64 programs (.prg) with a player screen, raw PRG/BIN
- Audio export: WAV (also every voice as its own file) and MP3
- Video export: MP4 (720p) with an oscilloscope for every voice
- USBSID-Pico output: plays on real SID chips (-u1), see below
- Night theme in the original C64 blue and a day theme
- Built-in manual with search (Help -> Manual), the history of C64 SID music
  (Help -> History) and keyboard reference (F12)


Starting
--------

Windows 11 (x64)
  Unpack the ZIP file and start GrooveGoat.exe. Keep the "instruments" and
  "examples" folders next to GrooveGoat.exe.
  If Windows SmartScreen warns about an unknown publisher, click
  "More info" and "Run anyway".

macOS (Apple Silicon / M chips, macOS 11 or newer)
  Unpack the ZIP file and move "C64 GrooveGoat.app" to the Applications
  folder. The app is not notarized by Apple, so the first start is blocked:
  - macOS 15 and newer: open the app once, then go to System Settings ->
    Privacy & Security, scroll down and click "Open Anyway".
  - macOS 11-14: right-click (or CTRL-click) the app, choose "Open" and
    confirm with "Open".
  Alternatively run this command in the Terminal once:
    xattr -cr "/Applications/C64 GrooveGoat.app"
  The instrument library and the example songs are inside the app; the
  file selector has the buttons "Library" and "Examples" to open them.

Linux (x86-64, AppImage)
  Make the AppImage executable and start it:
    chmod +x C64_GrooveGoat-0.9b-x86_64.AppImage
    ./C64_GrooveGoat-0.9b-x86_64.AppImage
  (or in the file manager: Properties -> Permissions -> "Allow executing").
  Nothing else has to be installed. Needs glibc 2.35 or newer (Ubuntu 22.04,
  Debian 12, Fedora 36 or newer) and an X11 or Wayland desktop (via XWayland).
  If the AppImage does not start because FUSE is missing, run it with
    ./C64_GrooveGoat-0.9b-x86_64.AppImage --appimage-extract-and-run
  The instrument library and the example songs are inside the AppImage; the
  file selector has the buttons "Library" and "Examples" to open them.

Linux (x86-64, tar.gz - without AppImage)
  Unpack the archive and start the program in the unpacked folder:
    tar xzf C64-GrooveGoat-0.9b-Linux-x86_64.tar.gz
    cd C64-GrooveGoat-0.9b-Linux-x86_64
    ./groovegoat
  Keep the "instruments" and "examples" folders next to the program.
  Optional: ./install.sh installs it for the current user (no root rights)
  to ~/.local/share/groovegoat, adds "C64 GrooveGoat" to the application
  menu and the command ~/.local/bin/groovegoat. ./uninstall.sh removes it
  again. Same requirements as the AppImage (glibc 2.35 or newer, X11 or
  Wayland); SDL 2 is built into the program, FUSE is not needed.


USBSID-Pico (real SID chips)
----------------------------

Start the program with -u1 to play on every attached USBSID-Pico board, or with
-u1 -sSERIAL1,SERIAL2 for chosen boards. Alt+U (or the USB button on the status
row) sets output, boards, board order and the song SID to chip map; -m sets the map.
Windows: install the WinUSB driver for the interface "USBSID-Pico Data" once
with Zadig (zadig.akeo.ie). Linux: copy the udev rule
69-usbsid-permissions.rules from the USBSID-Pico repository to
/etc/udev/rules.d and replug the board. See the manual chapter "USBSID-Pico".


First steps
-----------

1. Open the example song "Solar Voyage" (Open button or F10, then the
   "Examples" button) and press F1 to hear two SIDs (6 voices) playing
   together. F4 stops.
2. Click "New" and choose the number of SIDs for your own song.
3. Click "Library" in the instrument panel to pick instruments (type to
   search, SPACE plays a preview, ENTER loads), or click the dice button for
   a random instrument.
4. Enter notes in the pattern editor with the computer keyboard like on a
   piano (Z S X D C V ... and Q 2 W 3 E R ...; on German keyboards Y S X D C
   V ...). F1 plays, F4 stops. CTRL+Z undoes, CTRL+Y redoes. With a German
   keyboard click "Kbd: EN" in the status bar to switch to "Kbd: DE".
5. "SID" writes a SID file, "PRG" a program for a real C64 or an emulator
   (LOAD"NAME",8,1 and RUN), "Audio" a WAV or MP3 file and "Video" an MP4
   video with oscilloscopes.

Click "Help" for the keyboard reference and "Manual" for the full manual.


Files in this package
---------------------

  instruments/   instrument library ("SIDGen instruments for GoatTracker 2"),
                 6581/ for the old and 8580/ for the new SID chip
  examples/      example songs: "Solar Voyage" by IT-Dummkopfen (2 SIDs, also as
                 .sid and runnable .prg) and the GoatTracker 2 example songs
  licenses/      license texts of the included MP3/AAC/H.264 encoders and of the
                 USBSID-Pico driver and libusb
  Manual.txt     the manual (also built into the program)
  CREDITS.txt    credits and licenses of the used software
  COPYING.txt    GNU General Public License


License
-------

C64 GrooveGoat is free software under the GNU General Public License v2 or
later (GNU GPL v3 or later in builds with USBSID-Pico support, see CREDITS.txt).
The C64 playroutines and the PRG player do not fall under the GPL:
the music you create and export (SID, PRG, WAV, MP3, MP4) is yours to use for
any purpose. The source code is available at
https://github.com/IT-DUMMKOPFEN/C64GrooveGoat

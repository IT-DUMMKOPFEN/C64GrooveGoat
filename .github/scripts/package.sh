#!/bin/bash
# CI packaging of C64 GrooveGoat. Reads the binary built by the Makefile and
# writes one release package into OUTDIR. Deletes nothing.
#
#   .github/scripts/package.sh linux|win|mac OUTDIR
#
#   linux: build/appimage/groovegoat -> tar.gz and AppImage (APPIMAGETOOL)
#   win:   build/win/GrooveGoat.exe  -> zip (MINGW for llvm-strip)
#   mac:   build/mac/groovegoat      -> zip with an ad-hoc signed .app (SDLFW)
#
# The instrument library is not part of the repository: it is added when
# INSTRUMENTS points at a folder with 6581/ and 8580/, else it is left out.
# These packages are CI packages, the signed release packages come from
# tools/package.sh on the maintainer machine.
set -euo pipefail
cd "$(dirname "$0")/../.."

PLAT=${1:?usage: package.sh linux|win|mac OUTDIR}
OUT=${2:?usage: package.sh linux|win|mac OUTDIR}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
VERSION=$(sed -n 's/^VERSION *= *//p' Makefile | head -1)
NAME="C64-GrooveGoat-$VERSION"
STAGE=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/gg-package.XXXXXX")
LIBUSBDIR=${LIBUSBDIR:-}

# 1 when the built binary has USBSID-Pico support (the Makefile can switch it off)
usbsid_in() {
  if grep -aq 'USBSID-Pico: no board could be opened' "$1"; then echo 1; else echo 0; fi
}

# docs, examples, licenses and the optional instrument library into $1
common_files() {
  local d=$1 usb=$2
  mkdir -p "$d/licenses"
  cp docs/manual.txt "$d/Manual.txt"
  cp docs/credits.txt "$d/CREDITS.txt"
  cp copying "$d/COPYING.txt"
  cp docs/README-package.txt "$d/README.txt"
  cp src/codecs/lame/COPYING "$d/licenses/LAME-COPYING.txt"
  cp src/codecs/faac/COPYING "$d/licenses/faac-COPYING.txt"
  cp src/codecs/minih264/LICENSE "$d/licenses/minih264-LICENSE.txt"
  cp src/codecs/minimp4/LICENSE "$d/licenses/minimp4-LICENSE.txt"
  if [ "$usb" = 1 ]; then
    cp src/driver/usbsid/LICENSE "$d/licenses/USBSID-Pico-driver-LICENSE.txt"
    cp src/driver/usbsid/LICENSE-EXCEPTION "$d/licenses/USBSID-Pico-driver-LICENSE-EXCEPTION.txt"
    for f in "$LIBUSBDIR/share/doc/libusb/COPYING" /usr/share/doc/libusb-1.0-0/copyright; do
      [ -f "$f" ] || continue
      cp "$f" "$d/licenses/libusb-COPYING.txt"
      break
    done
    [ -f "$d/licenses/libusb-COPYING.txt" ] || echo "warning: libusb COPYING not found"
  fi
  mkdir -p "$d/examples"
  cp examples/*.sng examples/*.ins examples/*.prg examples/goatcompo.txt "$d/examples/" 2>/dev/null || true
  if [ -n "${INSTRUMENTS:-}" ] && [ -d "$INSTRUMENTS/6581" ]; then
    mkdir -p "$d/instruments"
    cp -r "$INSTRUMENTS/6581" "$INSTRUMENTS/8580" "$d/instruments/"
    python3 tools/fixlibrary.py "$d/instruments"
  else
    echo "note: no instrument library, package is built without it"
  fi
  chmod -R u+rwX,go+rX-w "$d"
}

icon256() {
  python3 -c "
from PIL import Image
Image.open('res/logo1024.png').resize((256, 256), Image.LANCZOS).save('$1')" 2>/dev/null \
    || cp res/logo1024.png "$1"
}

case "$PLAT" in
linux)
  BIN=build/appimage/groovegoat
  USB=$(usbsid_in "$BIN")
  echo "linux: USBSID-Pico support $USB"

  # tar.gz
  L=$STAGE/$NAME-Linux-x86_64
  mkdir -p "$L"
  strip -o "$L/groovegoat" "$BIN"
  chmod +x "$L/groovegoat"
  common_files "$L" "$USB"
  icon256 "$L/groovegoat.png"
  tar -C "$STAGE" --owner=0 --group=0 -czf "$OUT/$NAME-Linux-x86_64.tar.gz" "$NAME-Linux-x86_64"

  # AppImage
  A=$STAGE/AppDir
  mkdir -p "$A/usr/bin" "$A/usr/share/applications" "$A/usr/share/icons/hicolor/256x256/apps" "$A/usr/share/doc/groovegoat"
  strip -o "$A/usr/bin/groovegoat" "$BIN"
  [ -d "$L/instruments" ] && cp -r "$L/instruments" "$A/usr/bin/"
  cp -r "$L/examples" "$A/usr/bin/"
  cp "$L"/*.txt "$A/usr/share/doc/groovegoat/"
  cp -r "$L/licenses" "$A/usr/share/doc/groovegoat/"
  icon256 "$A/groovegoat.png"
  cp "$A/groovegoat.png" "$A/usr/share/icons/hicolor/256x256/apps/groovegoat.png"
  ln -s groovegoat.png "$A/.DirIcon"
  cat > "$A/groovegoat.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=C64 GrooveGoat
GenericName=SID Music Editor
Comment=Multi-SID music editor for the Commodore 64
Exec=groovegoat %f
Icon=groovegoat
Terminal=false
Categories=AudioVideo;Audio;
Keywords=C64;SID;tracker;chiptune;GoatTracker;music;
DESKTOP
  cp "$A/groovegoat.desktop" "$A/usr/share/applications/"
  cat > "$A/AppRun" <<'APPRUN'
#!/bin/sh
# C64 GrooveGoat AppImage start script
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/groovegoat" "$@"
APPRUN
  chmod +x "$A/AppRun" "$A/usr/bin/groovegoat"
  ARCH=x86_64 VERSION=$VERSION "${APPIMAGETOOL:?APPIMAGETOOL not set}" --appimage-extract-and-run --no-appstream \
    "$A" "$OUT/C64_GrooveGoat-$VERSION-x86_64.AppImage"
  chmod +x "$OUT/C64_GrooveGoat-$VERSION-x86_64.AppImage"
  ;;

win)
  BIN=build/win/GrooveGoat.exe
  USB=$(usbsid_in "$BIN")
  echo "win: USBSID-Pico support $USB"
  W=$STAGE/$NAME-Windows-x64
  mkdir -p "$W"
  "${MINGW:?MINGW not set}/bin/llvm-strip" -o "$W/GrooveGoat.exe" "$BIN"
  common_files "$STAGE/common" "$USB"
  [ -d "$STAGE/common/instruments" ] && cp -r "$STAGE/common/instruments" "$W/"
  cp -r "$STAGE/common/examples" "$W/"
  mkdir -p "$W/licenses"
  for f in Manual.txt CREDITS.txt COPYING.txt README.txt; do sed 's/$/\r/' "$STAGE/common/$f" > "$W/$f"; done
  for f in "$STAGE"/common/licenses/*.txt; do sed 's/$/\r/' "$f" > "$W/licenses/$(basename "$f")"; done
  (cd "$STAGE" && zip -qr9 "$OUT/$NAME-Windows-x64.zip" "$NAME-Windows-x64")
  ;;

mac)
  BIN=build/mac/groovegoat
  USB=$(usbsid_in "$BIN")
  echo "mac: USBSID-Pico support $USB"
  LICENSE_NOTE="GNU GPL v2"
  [ "$USB" = 1 ] && LICENSE_NOTE="GNU GPL v3 or later"
  M=$STAGE/$NAME-macOS-AppleSilicon
  APP="$M/C64 GrooveGoat.app"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
  cp "$BIN" "$APP/Contents/MacOS/GrooveGoat"
  # headers are not needed at run time
  rsync -a --exclude=Headers "${SDLFW:?SDLFW not set}/SDL2.framework" "$APP/Contents/Frameworks/"
  cp res/groovegoat.icns "$APP/Contents/Resources/"
  common_files "$STAGE/common" "$USB"
  [ -d "$STAGE/common/instruments" ] && cp -r "$STAGE/common/instruments" "$APP/Contents/Resources/"
  cp -r "$STAGE/common/examples" "$APP/Contents/Resources/"
  printf 'APPL????' > "$APP/Contents/PkgInfo"
  cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>GrooveGoat</string>
  <key>CFBundleIconFile</key><string>groovegoat</string>
  <key>CFBundleIdentifier</key><string>de.itdummkopfen.groovegoat</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>C64 GrooveGoat</string>
  <key>CFBundleDisplayName</key><string>C64 GrooveGoat</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleSignature</key><string>????</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.music</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>C64 GrooveGoat $VERSION by IT-Dummkopfen - $LICENSE_NOTE</string>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>C64 GrooveGoat Song</string>
      <key>CFBundleTypeExtensions</key><array><string>ggt</string><string>sng</string></array>
      <key>CFBundleTypeRole</key><string>Editor</string>
      <key>CFBundleTypeIconFile</key><string>groovegoat</string>
    </dict>
    <dict>
      <key>CFBundleTypeName</key><string>GoatTracker Instrument</string>
      <key>CFBundleTypeExtensions</key><array><string>ins</string></array>
      <key>CFBundleTypeRole</key><string>Editor</string>
    </dict>
  </array>
</dict>
</plist>
PLIST
  # ad-hoc signature (runs after the quarantine flag is cleared, not notarized)
  codesign --force --deep -s - "$APP"
  cp -r "$STAGE/common/examples" "$STAGE/common/licenses" "$STAGE"/common/*.txt "$M/"
  (cd "$STAGE" && zip -qry9 "$OUT/$NAME-macOS-AppleSilicon.zip" "$NAME-macOS-AppleSilicon")
  ;;

*)
  echo "usage: package.sh linux|win|mac OUTDIR" >&2
  exit 1
  ;;
esac
ls -la "$OUT"

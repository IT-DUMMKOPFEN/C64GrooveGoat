#!/bin/bash
# Builds the release packages of C64 GrooveGoat for Windows 11 (x64), Linux
# (x86-64 AppImage and tar.gz) and macOS (Apple Silicon) into dist/.
#
#   tools/package.sh
#
# Needs the cross toolchains described in the Makefile, the instrument
# library (INSTRUMENTS, default ../ins_zip/Instrumente) and rcodesign for
# signing the macOS app (RCODESIGN), a static SDL 2 for the AppImage
# (SDLSTATIC, see the Makefile) and appimagetool with the type 2 runtime
# (APPIMAGETOOL, APPIMAGERUNTIME).
set -e
cd "$(dirname "$0")/.."

VERSION=0.9b
NAME="C64-GrooveGoat-$VERSION"
TOOLS=${TOOLS:-$(cd .. && pwd)/tools}
INSTRUMENTS=${INSTRUMENTS:-$(cd .. && pwd)/ins_zip/Instrumente}
RCODESIGN=${RCODESIGN:-$TOOLS/rcodesign/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign}
MINGWBIN=$TOOLS/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin
SDLFW=$TOOLS/sdlmac/SDL2/SDL2.framework
APPIMAGETOOL=${APPIMAGETOOL:-$TOOLS/appimage/appimagetool.AppImage}
APPIMAGERUNTIME=${APPIMAGERUNTIME:-$TOOLS/appimage/runtime-x86_64}
DIST=dist

[ -d "$INSTRUMENTS/6581" ] || { echo "instrument library not found: $INSTRUMENTS"; exit 1; }

echo "== Building"
python3 tools/mkres.py > /dev/null
make TARGET=linux -j8 > /dev/null
make TARGET=appimage -j8 > /dev/null
make TARGET=win -j8 > /dev/null
make TARGET=mac -j8 > /dev/null

rm -rf $DIST
mkdir -p $DIST/common/examples

echo "== Demo song and examples"
DEMO=solar_voyage_2sid
cp examples/$DEMO.sng $DIST/common/examples/
./build/linux/groovegoat --export=$DIST/common/examples/$DEMO.sid $DIST/common/examples/$DEMO.sng > /dev/null
./build/linux/groovegoat --format=prg --export=$DIST/common/examples/$DEMO.prg $DIST/common/examples/$DEMO.sng > /dev/null
mkdir -p $DIST/common/examples/goattracker2
for f in examples/*.sng examples/*.ins; do [ "$(basename $f)" = "$DEMO.sng" ] || cp $f $DIST/common/examples/goattracker2/; done 2>/dev/null || true
cp examples/goatcompo.txt $DIST/common/examples/goattracker2/ 2>/dev/null || true

# Instrument library (without macOS metadata)
mkdir -p $DIST/common/instruments
cp -r "$INSTRUMENTS/6581" "$INSTRUMENTS/8580" $DIST/common/instruments/
cp "$INSTRUMENTS/README.txt" $DIST/common/instruments/ 2>/dev/null || true
find $DIST/common/instruments -name "__MACOSX" -prune -exec rm -rf {} \; 2>/dev/null || true
find $DIST/common/instruments -name ".DS_Store" -delete
# instruments that are silent with the GoatTracker playroutine (envelope over
# within the first frame) get their intended decay
python3 tools/fixlibrary.py $DIST/common/instruments

# Documents
cp docs/manual.txt $DIST/common/Manual.txt
cp docs/credits.txt $DIST/common/CREDITS.txt
cp copying $DIST/common/COPYING.txt
cp docs/README-package.txt $DIST/common/README.txt
mkdir -p $DIST/common/licenses
cp src/codecs/lame/COPYING $DIST/common/licenses/LAME-COPYING.txt
cp src/codecs/faac/COPYING $DIST/common/licenses/faac-COPYING.txt
cp src/codecs/minih264/LICENSE $DIST/common/licenses/minih264-LICENSE.txt
cp src/codecs/minimp4/LICENSE $DIST/common/licenses/minimp4-LICENSE.txt

# readable for everybody (files from the repository may be private)
chmod -R u+rwX,go+rX-w $DIST/common

# Text files with CRLF line endings for Windows
crlf() { sed 's/$/\r/' "$1" > "$2"; }

echo "== Windows"
W=$DIST/$NAME-Windows-x64
mkdir -p $W
$MINGWBIN/llvm-strip -o $W/GrooveGoat.exe build/win/GrooveGoat.exe
cp -r $DIST/common/instruments $DIST/common/examples $W/
mkdir -p $W/licenses
for f in Manual.txt CREDITS.txt COPYING.txt README.txt; do crlf $DIST/common/$f $W/$f; done
for f in $DIST/common/licenses/*.txt; do crlf $f $W/licenses/$(basename $f); done
(cd $DIST && zip -qr9 $NAME-Windows-x64.zip $NAME-Windows-x64)

echo "== Linux (AppImage)"
A=$DIST/AppDir
mkdir -p $A/usr/bin $A/usr/share/applications $A/usr/share/icons/hicolor/256x256/apps $A/usr/share/doc/groovegoat
strip -o $A/usr/bin/groovegoat build/appimage/groovegoat
# instrument library and examples next to the program (SDL_GetBasePath)
cp -r $DIST/common/instruments $DIST/common/examples $A/usr/bin/
cp $DIST/common/*.txt $A/usr/share/doc/groovegoat/
cp -r $DIST/common/licenses $A/usr/share/doc/groovegoat/
python3 -c "
from PIL import Image
Image.open('res/logo1024.png').resize((256, 256), Image.LANCZOS).save('$A/groovegoat.png')"
cp $A/groovegoat.png $A/usr/share/icons/hicolor/256x256/apps/groovegoat.png
ln -s groovegoat.png $A/.DirIcon
cat > $A/groovegoat.desktop <<'DESKTOP'
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
cp $A/groovegoat.desktop $A/usr/share/applications/
cat > $A/AppRun <<'APPRUN'
#!/bin/sh
# C64 GrooveGoat AppImage start script
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/groovegoat" "$@"
APPRUN
chmod +x $A/AppRun $A/usr/bin/groovegoat
APPIMAGE_FILE=$DIST/C64_GrooveGoat-$VERSION-x86_64.AppImage
ARCH=x86_64 VERSION=$VERSION "$APPIMAGETOOL" --appimage-extract-and-run --no-appstream \
  --runtime-file "$APPIMAGERUNTIME" $A $APPIMAGE_FILE > $DIST/appimage.log 2>&1 || { cat $DIST/appimage.log; exit 1; }
chmod +x $APPIMAGE_FILE
rm -rf $A

echo "== Linux (tar.gz)"
L=$DIST/$NAME-Linux-x86_64
mkdir -p $L
strip -o $L/groovegoat build/appimage/groovegoat
chmod +x $L/groovegoat
cp -r $DIST/common/instruments $DIST/common/examples $DIST/common/licenses $DIST/common/*.txt $L/
python3 -c "
from PIL import Image
Image.open('res/logo1024.png').resize((256, 256), Image.LANCZOS).save('$L/groovegoat.png')"
cat > $L/install.sh <<'INSTALL'
#!/bin/sh
# C64 GrooveGoat - install for the current user (no root rights needed).
# Copies the program to ~/.local/share/groovegoat, links it as
# ~/.local/bin/groovegoat and adds it to the application menu.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/groovegoat"
BIN="$HOME/.local/bin"
if [ ! -x "$HERE/groovegoat" ]; then echo "groovegoat not found next to install.sh"; exit 1; fi
if [ "$HERE" != "$DEST" ]; then
  if [ -e "$DEST" ] && [ ! -x "$DEST/groovegoat" ]; then
    echo "$DEST exists and is not a C64 GrooveGoat installation - not touching it"; exit 1
  fi
  rm -rf "$DEST"
  mkdir -p "$DEST"
  cp -r "$HERE/." "$DEST/"
fi
mkdir -p "$BIN" "$DATA/applications" "$DATA/icons/hicolor/256x256/apps"
ln -sf "$DEST/groovegoat" "$BIN/groovegoat"
cp "$DEST/groovegoat.png" "$DATA/icons/hicolor/256x256/apps/groovegoat.png"
cat > "$DATA/applications/groovegoat.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=C64 GrooveGoat
GenericName=SID Music Editor
Comment=Multi-SID music editor for the Commodore 64
Exec="$DEST/groovegoat" %f
Icon=groovegoat
Terminal=false
Categories=AudioVideo;Audio;
Keywords=C64;SID;tracker;chiptune;GoatTracker;music;
DESKTOP
command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$DATA/applications" > /dev/null 2>&1 || true
command -v gtk-update-icon-cache > /dev/null 2>&1 && gtk-update-icon-cache -q "$DATA/icons/hicolor" > /dev/null 2>&1 || true
echo "C64 GrooveGoat installed to $DEST"
echo "Start it from the application menu or with: $BIN/groovegoat"
INSTALL
cat > $L/uninstall.sh <<'UNINSTALL'
#!/bin/sh
# C64 GrooveGoat - remove the installation made by install.sh.
# Your songs and the settings in ~/.groovegoat are kept.
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/groovegoat"
if [ -x "$DEST/groovegoat" ]; then rm -rf "$DEST"; fi
[ -L "$HOME/.local/bin/groovegoat" ] && rm -f "$HOME/.local/bin/groovegoat"
rm -f "$DATA/applications/groovegoat.desktop" "$DATA/icons/hicolor/256x256/apps/groovegoat.png"
command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$DATA/applications" > /dev/null 2>&1 || true
echo "C64 GrooveGoat removed (settings in ~/.groovegoat are kept)"
UNINSTALL
chmod +x $L/install.sh $L/uninstall.sh
(cd $DIST && tar --owner=0 --group=0 -czf $NAME-Linux-x86_64.tar.gz $NAME-Linux-x86_64)

echo "== macOS"
M=$DIST/$NAME-macOS-AppleSilicon
APP="$M/C64 GrooveGoat.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
$MINGWBIN/llvm-strip -o "$APP/Contents/MacOS/GrooveGoat" build/mac/groovegoat
cp -a "$SDLFW" "$APP/Contents/Frameworks/"
rm -rf "$APP/Contents/Frameworks/SDL2.framework/Versions/A/Headers" "$APP/Contents/Frameworks/SDL2.framework/Headers"
cp res/groovegoat.icns "$APP/Contents/Resources/"
cp -r $DIST/common/instruments $DIST/common/examples "$APP/Contents/Resources/"
printf 'APPL????' > "$APP/Contents/PkgInfo"
cat > "$APP/Contents/Info.plist" <<EOF
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
  <key>NSHumanReadableCopyright</key><string>C64 GrooveGoat $VERSION by IT-Dummkopfen - GNU GPL v2</string>
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
EOF
"$RCODESIGN" sign "$APP" > $DIST/codesign.log 2>&1 || { cat $DIST/codesign.log; exit 1; }
"$RCODESIGN" verify "$APP/Contents/MacOS/GrooveGoat" >> $DIST/codesign.log 2>&1 || true
cp -r $DIST/common/examples $DIST/common/licenses $DIST/common/*.txt $M/
(cd $DIST && zip -qry9 $NAME-macOS-AppleSilicon.zip $NAME-macOS-AppleSilicon)

echo "== Source"
git archive --format=tar.gz --prefix=$NAME-source/ -o $DIST/$NAME-source.tar.gz HEAD

rm -rf $DIST/common
ls -la $DIST

#!/bin/bash
# Builds the release packages of C64 GrooveGoat for Windows, Linux and macOS
# (Apple Silicon) into dist/.
#
#   tools/package.sh
#
# Needs the cross toolchains described in the Makefile, the instrument
# library (INSTRUMENTS, default ../ins_zip/Instrumente) and rcodesign for
# signing the macOS app (RCODESIGN).
set -e
cd "$(dirname "$0")/.."

VERSION=0.1b
NAME="C64-GrooveGoat-$VERSION"
TOOLS=${TOOLS:-$(cd .. && pwd)/tools}
INSTRUMENTS=${INSTRUMENTS:-$(cd .. && pwd)/ins_zip/Instrumente}
RCODESIGN=${RCODESIGN:-$TOOLS/rcodesign/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign}
MINGWBIN=$TOOLS/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin
SDLFW=$TOOLS/sdlmac/SDL2/SDL2.framework
DIST=dist

[ -d "$INSTRUMENTS/6581" ] || { echo "instrument library not found: $INSTRUMENTS"; exit 1; }

echo "== Building"
python3 tools/mkres.py > /dev/null
make TARGET=linux -j8 > /dev/null
make TARGET=win -j8 > /dev/null
make TARGET=mac -j8 > /dev/null

rm -rf $DIST
mkdir -p $DIST/common/examples

echo "== Demo song and examples"
./build/linux/groovegoat --makedemo=$DIST/common/examples/groovegoat_demo_4sid.ggt > /dev/null
./build/linux/groovegoat --export=$DIST/common/examples/groovegoat_demo_4sid.sid $DIST/common/examples/groovegoat_demo_4sid.ggt > /dev/null
./build/linux/groovegoat --format=prg --export=$DIST/common/examples/groovegoat_demo_4sid.prg $DIST/common/examples/groovegoat_demo_4sid.ggt > /dev/null
mkdir -p $DIST/common/examples/goattracker2
cp examples/*.sng examples/*.ins $DIST/common/examples/goattracker2/ 2>/dev/null || true
cp examples/goatcompo.txt $DIST/common/examples/goattracker2/ 2>/dev/null || true

# Instrument library (without macOS metadata)
mkdir -p $DIST/common/instruments
cp -r "$INSTRUMENTS/6581" "$INSTRUMENTS/8580" $DIST/common/instruments/
cp "$INSTRUMENTS/README.txt" $DIST/common/instruments/ 2>/dev/null || true
find $DIST/common/instruments -name "__MACOSX" -prune -exec rm -rf {} \; 2>/dev/null || true
find $DIST/common/instruments -name ".DS_Store" -delete

# Documents
cp docs/manual.txt $DIST/common/Manual.txt
cp docs/credits.txt $DIST/common/CREDITS.txt
cp copying $DIST/common/COPYING.txt
cp docs/README-package.txt $DIST/common/README.txt

# Text files with CRLF line endings for Windows
crlf() { sed 's/$/\r/' "$1" > "$2"; }

echo "== Windows"
W=$DIST/$NAME-Windows-x64
mkdir -p $W
$MINGWBIN/llvm-strip -o $W/GrooveGoat.exe build/win/GrooveGoat.exe
cp -r $DIST/common/instruments $DIST/common/examples $W/
for f in Manual.txt CREDITS.txt COPYING.txt README.txt; do crlf $DIST/common/$f $W/$f; done
(cd $DIST && zip -qr9 $NAME-Windows-x64.zip $NAME-Windows-x64)

echo "== Linux"
L=$DIST/$NAME-Linux-x86_64
mkdir -p $L
strip -o $L/groovegoat build/linux/groovegoat
cp -r $DIST/common/instruments $DIST/common/examples $DIST/common/*.txt $L/
python3 -c "
from PIL import Image
Image.open('res/logo1024.png').resize((256, 256), Image.LANCZOS).save('$L/groovegoat.png')"
cat > $L/groovegoat.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=C64 GrooveGoat
GenericName=SID Music Editor
Comment=Multi-SID music editor for the Commodore 64
Exec=groovegoat %f
Icon=groovegoat
Terminal=false
Categories=AudioVideo;Audio;Music;
MimeType=application/x-goattracker-song;
EOF
cat > $L/install.sh <<'EOF'
#!/bin/sh
# Adds C64 GrooveGoat to the application menu of the current user.
DIR="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
cp "$DIR/groovegoat.png" "$HOME/.local/share/icons/hicolor/256x256/apps/groovegoat.png"
sed "s|^Exec=groovegoat|Exec=\"$DIR/groovegoat\"|" "$DIR/groovegoat.desktop" > "$HOME/.local/share/applications/groovegoat.desktop"
echo "C64 GrooveGoat was added to the application menu."
EOF
chmod +x $L/install.sh $L/groovegoat
(cd $DIST && tar czf $NAME-Linux-x86_64.tar.gz $NAME-Linux-x86_64)

echo "== macOS"
M=$DIST/$NAME-macOS-AppleSilicon
APP="$M/C64 GrooveGoat.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
$MINGWBIN/llvm-strip -o "$APP/Contents/MacOS/GrooveGoat" build/mac/groovegoat
cp -a "$SDLFW" "$APP/Contents/Frameworks/"
rm -rf "$APP/Contents/Frameworks/SDL2.framework/Versions/A/Headers" "$APP/Contents/Frameworks/SDL2.framework/Headers"
cp res/groovegoat.icns "$APP/Contents/Resources/"
cp -r $DIST/common/instruments "$APP/Contents/Resources/"
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
cp -r $DIST/common/examples $DIST/common/*.txt $M/
(cd $DIST && zip -qry9 $NAME-macOS-AppleSilicon.zip $NAME-macOS-AppleSilicon)

echo "== Source"
git archive --format=tar.gz --prefix=$NAME-source/ -o $DIST/$NAME-source.tar.gz HEAD

rm -rf $DIST/common
ls -la $DIST

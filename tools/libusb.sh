#!/bin/bash
# Builds a static libusb 1.0.29 for the release targets of C64 GrooveGoat.
#
#   tools/libusb.sh appimage|win|mac
#
# Installs into $TOOLS/libusb-TARGET (the Makefile LIBUSBDIR default); the
# sources are unpacked and built in $TOOLS/src/libusb-TARGET-build. Existing
# files are overwritten, nothing is deleted. Needs the toolchains described
# in the Makefile (llvm-mingw for win, clang + SDK for mac).
set -e

TARGET=$1
TOOLS=${TOOLS:-$(cd "$(dirname "$0")/.." && cd .. && pwd)/tools}
VER=1.0.29
URL=https://github.com/libusb/libusb/releases/download/v$VER/libusb-$VER.tar.bz2
MINGW=${MINGW:-$TOOLS/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64}
MACSDK=${MACSDK:-$TOOLS/macsdk}
PREFIX=$TOOLS/libusb-$TARGET
BUILD=$TOOLS/src/libusb-$TARGET-build
TARBALL=$TOOLS/src/libusb-$VER.tar.bz2

case "$TARGET" in
appimage|win|mac) ;;
*) echo "usage: tools/libusb.sh appimage|win|mac" >&2; exit 1 ;;
esac

mkdir -p "$TOOLS/src" "$BUILD"
[ -f "$TARBALL" ] || curl -L -o "$TARBALL" "$URL"
tar -xjf "$TARBALL" -C "$BUILD" --strip-components=1

cd "$BUILD"
COMMON="--enable-static --disable-shared --prefix=$PREFIX"
case "$TARGET" in
appimage)
    CC=gcc ./configure $COMMON --disable-udev
    ;;
win)
    # configure needs the mingw windres/dlltool on PATH, else RC is empty and libtool fails with "unrecognised option: '--preprocessor-arg'"
    export PATH="$MINGW/bin:$PATH"
    ./configure $COMMON --host=x86_64-w64-mingw32 \
        CC="$MINGW/bin/x86_64-w64-mingw32-clang" \
        AR="$MINGW/bin/llvm-ar" RANLIB="$MINGW/bin/llvm-ranlib"
    ;;
mac)
    # same target and linker flags as the Makefile mac LDFLAGS
    MACFLAGS="-target arm64-apple-macos11.0 -isysroot $MACSDK -fuse-ld=lld -Wl,-platform_version,macos,11.0,14.0"
    # llvm-ar / llvm-ranlib from the llvm-mingw toolchain if present, else from PATH
    MACAR=llvm-ar
    MACRANLIB=llvm-ranlib
    [ -x "$MINGW/bin/llvm-ar" ] && MACAR="$MINGW/bin/llvm-ar"
    [ -x "$MINGW/bin/llvm-ranlib" ] && MACRANLIB="$MINGW/bin/llvm-ranlib"
    ./configure $COMMON --host=aarch64-apple-darwin \
        CC="clang $MACFLAGS" \
        AR="$MACAR" RANLIB="$MACRANLIB"
    ;;
esac
make -j"$(nproc 2>/dev/null || echo 4)"
make install
# libusb make install does not ship COPYING, package.sh reads it from here
mkdir -p "$PREFIX/share/doc/libusb"
cp "$BUILD/COPYING" "$PREFIX/share/doc/libusb/COPYING"
echo "libusb $VER ($TARGET) installed in $PREFIX"

# C64 GrooveGoat build
#
#   make TARGET=linux     native Linux build (needs SDL2 headers + libSDL2)
#   make TARGET=win       Windows x64 cross build (llvm-mingw + SDL2 mingw devel)
#   make TARGET=mac       macOS Apple Silicon cross build (clang + lld + SDK + SDL2.framework)
#
# Paths to the toolchains can be overridden on the command line.

TARGET ?= linux
TOOLS ?= $(abspath ../tools)
VERSION = 0.8b

BUILDDIR = build/$(TARGET)
SRCDIR = src

CSRC = goattrk2.c gsong.c gorder.c gpattern.c ginstr.c gtable.c gplay.c gdisplay.c \
       gfile.c greloc.c ghelp.c gsound.c gconsole.c gundo.c gplatform.c gaudio.c \
       gfileio.c gdialog.c gres.c gsonglen.c gautotest.c
CXXSRC = gsid.cpp
RESIDSRC = resid/envelope.cpp resid/extfilt.cpp resid/filter.cpp resid/pot.cpp resid/sid.cpp \
       resid/wave.cpp resid/wave6581__ST.cpp resid/wave6581_P_T.cpp resid/wave6581_PS_.cpp \
       resid/wave6581_PST.cpp resid/wave8580__ST.cpp resid/wave8580_P_T.cpp resid/wave8580_PS_.cpp \
       resid/wave8580_PST.cpp resid/voice.cpp \
       resid-fp/envelopefp.cpp resid-fp/extfiltfp.cpp resid-fp/filterfp.cpp resid-fp/potfp.cpp \
       resid-fp/sidfp.cpp resid-fp/wavefp.cpp resid-fp/voicefp.cpp
ASMSRC = asm/asmtab.c asm/chnkpool.c asm/expr.c asm/lexyy.c asm/log.c asm/membuf.c \
       asm/membufio.c asm/namedbuf.c asm/parse.c asm/pc.c asm/vec.c

OPT = -O2
COMMONFLAGS = $(OPT) -I$(SRCDIR) -I$(SRCDIR)/asm -DGG_VERSION=\"$(VERSION)\" \
       -Wno-unused-result -Wno-format-truncation -Wno-deprecated-declarations
CFLAGS_EXTRA =
CXXFLAGS_EXTRA = -fno-exceptions -fno-rtti

ifeq ($(TARGET),linux)
CC = gcc
CXX = g++
SDLINC ?= $(TOOLS)/SDL2src/include
SDLLIB ?= /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0
COMMONFLAGS += -I$(SDLINC) -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE
LDFLAGS = -Wl,-rpath,'$$ORIGIN/lib' -Wl,--as-needed
LIBS = $(SDLLIB) -lm -ldl -lpthread
EXE = $(BUILDDIR)/groovegoat
endif

ifeq ($(TARGET),win)
MINGW ?= $(TOOLS)/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
SDLMINGW ?= $(TOOLS)/SDL2-2.32.10/x86_64-w64-mingw32
CC = $(MINGW)/bin/x86_64-w64-mingw32-clang
CXX = $(MINGW)/bin/x86_64-w64-mingw32-clang++
WINDRES = $(MINGW)/bin/x86_64-w64-mingw32-windres
COMMONFLAGS += -I$(SDLMINGW)/include/SDL2 -Dmain=SDL_main -D__WIN32__
LDFLAGS = -mwindows -static
LIBS = -L$(SDLMINGW)/lib -lmingw32 -lSDL2main -lSDL2 -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 \
       -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid -lcfgmgr32
EXE = $(BUILDDIR)/GrooveGoat.exe
WINRES = $(BUILDDIR)/groovegoat_res.o
endif

ifeq ($(TARGET),mac)
CC = clang
CXX = clang++
MACSDK ?= $(TOOLS)/macsdk
SDLFW ?= $(TOOLS)/sdlmac/SDL2
MACTARGET = -target arm64-apple-macos11.0 -isysroot $(MACSDK)
COMMONFLAGS += $(MACTARGET) -F$(SDLFW) -I$(SDLFW)/SDL2.framework/Headers -D__MACOSX__
CXXFLAGS_EXTRA += -nostdinc++ -I$(SRCDIR)/macstub
LDFLAGS = $(MACTARGET) -fuse-ld=lld -F$(SDLFW) -Wl,-rpath,@executable_path/../Frameworks \
       -Wl,-platform_version,macos,11.0,14.0 -Wl,-dead_strip
LIBS = -framework SDL2 -lSystem
EXE = $(BUILDDIR)/groovegoat
endif

OBJS = $(addprefix $(BUILDDIR)/,$(CSRC:.c=.o) $(CXXSRC:.cpp=.o) $(RESIDSRC:.cpp=.o) $(ASMSRC:.c=.o))

all: $(EXE)

$(SRCDIR)/gres.c: tools/mkres.py tools/mklogo.py $(SRCDIR)/player.s $(SRCDIR)/altplayer.s $(SRCDIR)/mplayer.s $(SRCDIR)/frontend.s docs/manual.txt docs/credits.txt
	python3 tools/mkres.py

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(COMMONFLAGS) $(CFLAGS_EXTRA) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp $(wildcard $(SRCDIR)/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMONFLAGS) $(CXXFLAGS_EXTRA) -c -o $@ $<

$(BUILDDIR)/groovegoat_res.o: res/groovegoat.rc res/groovegoat.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -I res -o $@ res/groovegoat.rc

ifeq ($(TARGET),mac)
$(EXE): $(OBJS) $(BUILDDIR)/macstub.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
$(BUILDDIR)/macstub.o: $(SRCDIR)/macstub/cxxrt.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMONFLAGS) -c -o $@ $<
else
$(EXE): $(OBJS) $(WINRES)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)
endif

clean:
	rm -rf build

.PHONY: all clean

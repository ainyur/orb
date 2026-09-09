WIN := bin/orb.exe build/wine/%.exe release-wine

ifeq ($(OS),Windows_NT)
CC       := gcc
CFLAGS   := -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra
BACKEND  := -DORB_OS_GDI
LIBS     := -lgdi32 -lole32
TESTLIBS :=
ORB      := bin/orb.exe
LIB      := dll
OBJ      := obj
EXE      := .exe
else
CC       := gcc
CFLAGS   := -std=c23 -D_POSIX_C_SOURCE=200809L -Wall -Wextra
BACKEND  := -DORB_OS_X11
LIBS     := -lX11 -lasound -lm
TESTLIBS := -lm
ORB      := bin/orb
LIB      := so
OBJ      := o
EXE      :=
$(WIN): CC := x86_64-w64-mingw32-gcc
endif

$(WIN): CFLAGS   := -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra
$(WIN): BACKEND  := -DORB_OS_GDI
$(WIN): LIBS     := -lgdi32 -lole32
$(WIN): TESTLIBS :=
$(WIN): LIB      := dll
$(WIN): OBJ      := obj
$(WIN): EXE      := .exe

SRC      := $(wildcard src/*.c src/*/*.c src/*.h src/*/*.h)
TESTS    := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))
GAME     ?= examples/demo
GAMENAME := $(notdir $(abspath $(GAME)))

.PHONY: all run test test-wine run-wine release release-wine fixtures clean

all: $(ORB)

build/scratch build/wine bin:
	mkdir -p $@

bin/orb bin/orb.exe: $(SRC) | bin
	$(CC) $(CFLAGS) -g -O1 $(BACKEND) -o $@ src/main.c $(LIBS)

clean:
	rm -rf build bin

run: $(ORB)
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.$(LIB)
	$(ORB) run $(GAME)

release release-wine: $(ORB)
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.$(LIB)
	$(ORB) seal $(GAME) $(GAME)/build/game.orb
	mkdir -p $(GAME)/bin
	$(CC) $(CFLAGS) -O2 $(BACKEND) -DORB_RELEASE --embed-dir=$(GAME)/build -o $(GAME)/bin/$(GAMENAME)$(EXE) src/main.c $(GAME)/build/*.$(OBJ) $(LIBS)

run-wine: bin/orb.exe
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.dll
	WINEDEBUG=-all wine bin/orb.exe run $(GAME)

build/test_%: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/*) | build/scratch
	$(CC) $(CFLAGS) -g -O1 -DORB_OS_HEADLESS -Isrc -o $@ $< $(TESTLIBS)

build/wine/test_%.exe: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/*) | build/scratch build/wine
	$(CC) $(CFLAGS) -g -O1 -DORB_OS_HEADLESS -Isrc -o $@ $< $(TESTLIBS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done


test-wine: build/wine/test_os.exe
	@echo "== $<"; WINEDEBUG=-all wine $< || exit 1

fixtures: build/wav$(EXE)
	aseprite -b --script tests/fixtures/make.lua
	aseprite -b --script examples/demo/art/make.lua
	mkdir -p examples/demo/sfx examples/demo/music
	build/wav$(EXE) beep tests/fixtures/beep.wav
	build/wav$(EXE) loop tests/fixtures/loop.wav
	build/wav$(EXE) beep examples/demo/sfx/bounce.wav
	build/wav$(EXE) song examples/demo/music/song.wav

build/wav$(EXE): tests/fixtures/wav.c | build/scratch
	$(CC) $(CFLAGS) -O1 -o $@ $<

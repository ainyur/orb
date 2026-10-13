WIN := bin/orb.exe build/wine/%.exe release-wine

# CC, CFLAGS, and LDFLAGS come from the command line or the environment; the flags go after FLAGS.
ifeq ($(OS),Windows_NT)
FLAGS    := -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wformat=2
BACKEND  := -DORB_OS_GDI
LIBS     := -static -lgdi32 -lole32 -lshell32
TESTLIBS := -lshell32
ORB      := bin/orb.exe
LIB      := dll
OBJ      := obj
EXE      := .exe
HARDEN   := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong
HARDEN_LD :=
else
FLAGS    := -std=c23 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wformat=2
BACKEND  := -DORB_OS_X11
LIBS     := -lX11 -lasound
TESTLIBS :=
ORB      := bin/orb
LIB      := so
OBJ      := o
EXE      :=
HARDEN   := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fPIE
HARDEN_LD := -pie -Wl,-z,relro,-z,now
$(WIN): private CC := x86_64-w64-mingw32-gcc
endif

$(WIN): private FLAGS    := -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wformat=2
$(WIN): private BACKEND  := -DORB_OS_GDI
$(WIN): private LIBS     := -static -lgdi32 -lole32 -lshell32
$(WIN): private TESTLIBS := -lshell32
$(WIN): private LIB      := dll
$(WIN): private OBJ      := obj
$(WIN): private EXE      := .exe
$(WIN): private HARDEN   := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong
$(WIN): private HARDEN_LD :=

SRC      := $(wildcard src/*.c src/*/*.c src/*.h src/*/*.h)
TESTS    := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))
TESTS_ASAN := $(patsubst tests/%.c,build/asan/%,$(wildcard tests/test_*.c))
GAME     ?= examples/demo
GAMENAME := $(notdir $(abspath $(GAME)))

.PHONY: all run test test-asan test-wine run-wine release release-wine fixtures clean

all: $(ORB)

build/scratch build/wine build/asan bin:
	mkdir -p $@

bin/orb bin/orb.exe: $(SRC) | bin
	$(CC) $(FLAGS) -g -O1 $(BACKEND) $(CFLAGS) $(LDFLAGS) -o $@ src/main.c $(LIBS)

clean:
	rm -rf build bin

run: $(ORB)
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.$(LIB)
	$(ORB) run $(GAME)

release release-wine: $(ORB)
	$(MAKE) --no-print-directory -s -C $(GAME) release-objects RELEASE_EXT=$(OBJ) RELEASE_CFLAGS='$(HARDEN)'
	$(ORB) seal $(GAME) $(GAME)/build/game.orb
	mkdir -p $(GAME)/bin
	$(CC) $(FLAGS) -O2 $(BACKEND) -DORB_RELEASE $(HARDEN) $(HARDEN_LD) $(CFLAGS) $(LDFLAGS) --embed-dir=$(GAME)/build -o $(GAME)/bin/$(GAMENAME)$(EXE) src/main.c $(GAME)/build/release/*.$(OBJ) $(LIBS)

run-wine: bin/orb.exe
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.dll
	WINEDEBUG=-all wine bin/orb.exe run $(GAME)

build/test_%: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/* tests/fixtures/*/* tests/fixtures/*/*/*) | build/scratch
	$(CC) $(FLAGS) -g -O1 -DORB_OS_HEADLESS -Isrc $(CFLAGS) $(LDFLAGS) -o $@ $< $(TESTLIBS)

build/wine/test_%.exe: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/* tests/fixtures/*/* tests/fixtures/*/*/*) | build/scratch build/wine
	$(CC) $(FLAGS) -g -O1 -DORB_OS_HEADLESS -Isrc $(CFLAGS) $(LDFLAGS) -o $@ $< $(TESTLIBS)

build/asan/test_%: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/* tests/fixtures/*/* tests/fixtures/*/*/*) | build/scratch build/asan
	$(CC) $(FLAGS) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DORB_OS_HEADLESS -Isrc $(CFLAGS) $(LDFLAGS) -o $@ $< $(TESTLIBS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

test-asan: $(TESTS_ASAN)
	@for t in $(TESTS_ASAN); do echo "== $$t"; ./$$t || exit 1; done

# The UTF-8 directory the test makes is checked from this side, since inside one
# process a mangled name round-trips and looks fine.
test-wine: build/wine/test_os.exe
	@rm -rf build/scratch/héllo; echo "== $<"; LC_ALL=C.UTF-8 WINEDEBUG=-all wine $< héllo || exit 1
	@test -f build/scratch/héllo/ü.bin || { echo "wine wrote a mangled path"; exit 1; }

fixtures: build/wav$(EXE)
	mkdir -p tests/fixtures/art tests/fixtures/sfx tests/fixtures/music tests/fixtures/levels examples/demo/sfx examples/demo/music examples/demo/levels examples/demo/fonts tests/fixtures/fonts tests/fixtures/fonts-bad/uneven tests/fixtures/fonts-bad/origin tests/fixtures/fonts-bad/wide tests/fixtures/fonts-bad/few tests/fixtures/fonts-bad/frames tests/fixtures/fonts-bad/blank
	aseprite -b --script tests/fixtures/make.lua
	aseprite -b --script examples/demo/art/make.lua
	build/wav$(EXE) beep tests/fixtures/sfx/beep.wav
	build/wav$(EXE) loop tests/fixtures/music/loop.wav
	build/wav$(EXE) beep examples/demo/sfx/bounce.wav
	build/wav$(EXE) song examples/demo/music/song.wav

build/wav$(EXE): build/%$(EXE): tests/fixtures/%.c | build/scratch
	$(CC) $(FLAGS) -O1 $(CFLAGS) $(LDFLAGS) -o $@ $<

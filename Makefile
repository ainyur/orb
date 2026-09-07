ifeq ($(OS),Windows_NT)
ifeq ($(origin CC),default)
CC       := gcc
endif
CFLAGS   ?= -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -g -O1
WINCC    ?= $(CC)
BACKEND  := -DORB_OS_GDI
LIBS     := -lgdi32
TESTLIBS :=
ORB      := bin/orb.exe
LIB      := dll
else
CC       ?= cc
CFLAGS   ?= -std=c23 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1
WINCC    ?= x86_64-w64-mingw32-gcc
BACKEND  := -DORB_OS_X11
LIBS     := -lX11 -ldl
TESTLIBS := -ldl
ORB      := bin/orb
LIB      := so
endif

WINCFLAGS ?= -std=c23 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -g -O1
SRC       := $(wildcard src/*.c src/*/*.c src/*.h src/*/*.h)
TESTS     := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))
GAME      ?= examples/demo

.PHONY: all run test test-wine run-wine fixtures clean

all: $(ORB)

build/scratch build/wine bin:
	mkdir -p $@

bin/orb: $(SRC) | bin
	$(CC) $(CFLAGS) $(BACKEND) -o $@ src/main.c $(LIBS)

bin/orb.exe: $(SRC) | bin
	$(WINCC) $(WINCFLAGS) -DORB_OS_GDI -o $@ src/main.c -lgdi32

clean:
	rm -rf build bin

run: $(ORB)
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.$(LIB)
	$(ORB) run $(GAME)

run-wine: bin/orb.exe
	$(MAKE) --no-print-directory -s -C $(GAME) build/game.dll
	WINEDEBUG=-all wine bin/orb.exe run $(GAME)

build/test_%: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/*) | build/scratch
	$(CC) $(CFLAGS) -DORB_OS_HEADLESS -Isrc -o $@ $< $(TESTLIBS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

build/wine/test_%.exe: tests/test_%.c tests/test.h $(SRC) $(wildcard tests/fixtures/*) | build/scratch build/wine
	$(WINCC) $(WINCFLAGS) -DORB_OS_HEADLESS -Isrc -o $@ $<


test-wine: build/wine/test_os.exe
	@echo "== $<"; WINEDEBUG=-all wine $< || exit 1

fixtures:
	aseprite -b --script tests/fixtures/make.lua
	aseprite -b --script examples/demo/art/make.lua

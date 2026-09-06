CC      ?= cc
CFLAGS  ?= -std=c23 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1
SRC     := $(wildcard src/*.c src/*/*.c src/*.h src/*/*.h)
TESTS   := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))

.PHONY: all test fixtures clean

all: bin/orb

build/scratch bin:
	mkdir -p $@

bin/orb: $(SRC) | bin
	$(CC) $(CFLAGS) -DORB_OS_X11 -o $@ src/main.c -lX11 -ldl

build/test_%: tests/test_%.c tests/test.h $(SRC) | build/scratch
	$(CC) $(CFLAGS) -DORB_OS_HEADLESS -Isrc -o $@ $< -ldl

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

fixtures:
	aseprite -b --script tests/fixtures/make.lua
	aseprite -b --script examples/hello/art/make.lua

clean:
	rm -rf build bin

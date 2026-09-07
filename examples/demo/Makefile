CC    ?= cc
ifeq ($(OS),Windows_NT)
WINCC ?= gcc
else
WINCC ?= x86_64-w64-mingw32-gcc
endif

build/game.so: demo.c ../../src/orb.h | build
	$(CC) -std=c23 -Wall -Wextra -g -shared -fPIC -I../../src -o $@ demo.c

build/game.dll: demo.c ../../src/orb.h | build
	$(WINCC) -std=c23 -Wall -Wextra -g -shared -I../../src -o $@ demo.c

build:
	mkdir -p build

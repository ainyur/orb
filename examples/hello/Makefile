CC ?= cc

build/game.so: game.c game_assets.h ../../src/orb.h | build
	$(CC) -std=c23 -Wall -Wextra -g -shared -fPIC -I../../src -o $@ game.c

build:
	mkdir -p build

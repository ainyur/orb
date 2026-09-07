ORB    ?= ../..
CC     ?= cc
ifeq ($(OS),Windows_NT)
WINCC  ?= gcc
else
WINCC  ?= x86_64-w64-mingw32-gcc
endif
CFLAGS := -std=c23 -Wall -Wextra -g -MMD -MP -I$(ORB)/src
OBJ    := $(patsubst %.c,build/%.o,$(wildcard *.c))
WINOBJ := $(OBJ:.o=.obj)

build/game.so: $(OBJ)
	$(CC) -shared -o $@ $^

build/game.dll: $(WINOBJ)
	$(WINCC) -shared -o $@ $^

build/%.o: %.c | build
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

build/%.obj: %.c | build
	$(WINCC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

-include $(OBJ:.o=.d)

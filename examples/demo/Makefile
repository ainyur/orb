ORB    ?= ../..
CC     := gcc
CFLAGS := -std=c23 -Wall -Wextra -g -I$(ORB)/src
PIC    := -fPIC
OBJ    := $(patsubst %.c,build/%.o,$(wildcard *.c))

.PHONY: clean release-objects

build/%.obj: PIC :=
ifneq ($(OS),Windows_NT)
build/game.dll build/%.obj: CC := x86_64-w64-mingw32-gcc
endif

RELEASE_EXT ?= o
RELEASE_OBJ := $(patsubst %.c,build/release/%.$(RELEASE_EXT),$(wildcard *.c))

ifneq ($(OS),Windows_NT)
build/release/%.obj: CC := x86_64-w64-mingw32-gcc
endif

build/game.so: $(OBJ)
build/game.dll: $(OBJ:.o=.obj)
build/game.so build/game.dll:
	$(CC) -shared -o $@ $^

build/%.o: %.c | build
	$(CC) $(CFLAGS) $(PIC) -MMD -MP -MF $@.d -c -o $@ $<

build/%.obj: %.c | build
	$(CC) $(CFLAGS) $(PIC) -MMD -MP -MF $@.d -c -o $@ $<

release-objects: $(RELEASE_OBJ)

build/release/%.o: %.c | build/release
	$(CC) $(CFLAGS) -O2 -fPIE -DORB_RELEASE -MMD -MP -MF $@.d -c -o $@ $<

build/release/%.obj: %.c | build/release
	$(CC) $(CFLAGS) -O2 -DORB_RELEASE -MMD -MP -MF $@.d -c -o $@ $<

build/release:
	mkdir -p build/release

build:
	mkdir -p build

clean:
	rm -rf build bin

-include $(wildcard build/*.d)
-include $(wildcard build/release/*.d)

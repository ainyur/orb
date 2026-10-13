ORB    ?= ../..
# CC, CFLAGS, and LDFLAGS come from the command line or the environment; the flags go after FLAGS.
FLAGS  := -std=c23 -Wall -Wextra -g -I$(ORB)/src
PIC    := -fPIC
OBJ    := $(patsubst %.c,build/%.o,$(wildcard *.c))

.PHONY: clean release-objects

build/%.obj: PIC :=
ifneq ($(OS),Windows_NT)
build/game.dll build/%.obj: CC := x86_64-w64-mingw32-gcc
endif

RELEASE_EXT ?= o
RELEASE_CFLAGS ?=
RELEASE_OBJ := $(patsubst %.c,build/release/%.$(RELEASE_EXT),$(wildcard *.c))

ifneq ($(OS),Windows_NT)
build/release/%.obj: CC := x86_64-w64-mingw32-gcc
endif

build/game.so: $(OBJ)
build/game.dll: $(OBJ:.o=.obj)
build/game.so build/game.dll:
	$(CC) -shared $(LDFLAGS) -o $@ $^

build/%.o: %.c | build
	$(CC) $(FLAGS) $(PIC) $(CFLAGS) -MMD -MP -MF $@.d -c -o $@ $<

build/%.obj: %.c | build
	$(CC) $(FLAGS) $(PIC) $(CFLAGS) -MMD -MP -MF $@.d -c -o $@ $<

release-objects: $(RELEASE_OBJ)

build/release/%.o: %.c | build/release
	$(CC) $(FLAGS) -O2 $(RELEASE_CFLAGS) -DORB_RELEASE $(CFLAGS) -MMD -MP -MF $@.d -c -o $@ $<

build/release/%.obj: %.c | build/release
	$(CC) $(FLAGS) -O2 $(RELEASE_CFLAGS) -DORB_RELEASE $(CFLAGS) -MMD -MP -MF $@.d -c -o $@ $<

build/release:
	mkdir -p build/release

build:
	mkdir -p build

clean:
	rm -rf build bin

-include $(wildcard build/*.d)
-include $(wildcard build/release/*.d)

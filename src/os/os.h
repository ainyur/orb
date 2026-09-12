#pragma once

#include "../core/arena.h"
#include "../core/input.h"
#include "../orb.h"

#include <stdio.h>
#include <string.h>

constexpr int ORB_PATH_MAX = 600;
typedef char orb_path[ORB_PATH_MAX];

// What the game's build produces and scry loads: build/game.so or build/game.dll.
#ifdef _WIN32
#define ORB_OS_LIB_SUFFIX ".dll"
#else
#define ORB_OS_LIB_SUFFIX ".so"
#endif

typedef struct orb_os_config {
    const char* title;
    int size_w, size_h;
} orb_os_config;

void orb_path_join(orb_path out, const char* dir, const char* rel);

static inline bool orb_has_suffix(const char* path, const char* suffix) {
    size_t n = strlen(path), m = strlen(suffix);

    return n >= m && strcmp(path + n - m, suffix) == 0;
}

static inline int orb_os_compare_paths(const void* a, const void* b) {
    return strcmp(a, b);
}

// Paths are UTF-8 on every platform. On Windows this replaces main's ANSI argv
// with UTF-8 copies of the wide command line; elsewhere it leaves it alone.
void orb_os_args(int* argc, char*** argv);

bool orb_os_open(const orb_os_config* cfg);
void orb_os_close(void);
void orb_os_present(const uint32_t* rgb);
bool orb_os_pump(orb_input* out);

void* orb_os_dlopen(const char* path);
void orb_os_dlclose(void* lib);
void* orb_os_dlsym(void* lib, const char* name);
bool orb_os_copy_file(const char* from, const char* to);
bool orb_os_make_dir(const char* path);
int orb_os_list_dir(const char* dir, const char* suffix, orb_path* out, int max);
int orb_os_run(const char* command, void (*line)(const char* text));
void orb_os_sleep(uint64_t ns);
uint64_t orb_os_ticks(void);

// What a path is, from one query; false when there is nothing there.
typedef struct orb_os_info {
    uint64_t size, mtime; // bytes; nanoseconds
    bool dir;
} orb_os_info;

bool orb_os_stat(const char* path, orb_os_info* out);
FILE* orb_os_fopen(const char* path, const char* mode); // "rb" or "wb"

// os/stdio.c, over the two above.
uint64_t orb_os_file_mtime(const char* path);
bool orb_os_is_dir(const char* path);
bool orb_os_read_file(const char* path, orb_arena* into, orb_span* out);
bool orb_os_write_file(const char* path, orb_span data);

// orb implements this in core/api.c; the OS audio thread calls it for every
// buffer it needs: frames * ORB_AUDIO_CHANNELS interleaved int16 at ORB_AUDIO_RATE.
void orb_audio_render(int16_t* out, int frames);

// Also in api.c, for an OS layer whose device is gone: renders silence in chunks
// until *rendered frames cover elapsed_ns, so sounds end, the ring drains, and
// song_position keeps real time while the layer retries the device. True once
// a second of silence, when the layer should try the device again.
bool orb_audio_idle(uint64_t elapsed_ns, uint64_t* rendered);

constexpr uint64_t ORB_AUDIO_TICK_NS = 20000000; // an outage loop's period

#ifdef ORB_OS_HEADLESS
const uint32_t* orb_os_headless_frame(void);
void orb_os_headless_set_input(const orb_input* in);
#endif

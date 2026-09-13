#pragma once

#include "../core/arena.h"
#include "../core/input.h"
#include "../orb.h"

#include <stdio.h>
#include <string.h>

constexpr int ORB_PATH_MAX = 600;
constexpr uint64_t ORB_NS_PER_SECOND = 1000000000;
constexpr uint64_t ORB_AUDIO_TICK_NS = 20000000;

#ifdef _WIN32
#define ORB_OS_LIB_SUFFIX ".dll"
#else
#define ORB_OS_LIB_SUFFIX ".so"
#endif

typedef char orb_path[ORB_PATH_MAX];

typedef struct orb_os_config {
    const char* title;
    orb_size size;
} orb_os_config;

typedef struct orb_os_entry {
    orb_path name;
    bool dir;
} orb_os_entry;

typedef struct orb_os_info {
    uint64_t size, mtime;
    bool dir;
} orb_os_info;

typedef struct orb_os_library orb_os_library;

static inline bool orb_has_suffix(const char* path, const char* suffix) {
    size_t n = strlen(path), m = strlen(suffix);

    return n >= m && strcmp(path + n - m, suffix) == 0;
}

static inline int orb_os_button(const uint32_t* keymap, uint32_t key) {
    for (int b = 0; b < ORB_BTN_COUNT; b++)
        if (keymap[b] == key) return b;

    return -1;
}

static inline int orb_os_open_scale(orb_size fb, orb_size screen) {
    int scale = 3;

    while (scale > 1 && (fb.w * scale > screen.w || fb.h * scale > screen.h))
        scale--;

    return scale;
}

// Paths are UTF-8 on every platform. On Windows this replaces main's ANSI argv
// with UTF-8 copies of the wide command line; elsewhere it leaves it alone.
void orb_os_args(int* argc, char*** argv);

bool orb_os_open(const orb_os_config* cfg);
void orb_os_close(void);
void orb_os_present(const uint32_t* rgb);
bool orb_os_pump(orb_input* out);

orb_os_library* orb_os_dlopen(const char* path);
void orb_os_dlclose(orb_os_library* lib);
void* orb_os_dlsym(orb_os_library* lib, const char* name);
bool orb_os_copy_file(const char* from, const char* to);
bool orb_os_make_dir(const char* path);
int orb_os_run(const char* command, void (*line)(const char* text));
void orb_os_sleep(uint64_t ns);
uint64_t orb_os_ticks(void);

bool orb_os_stat(const char* path, orb_os_info* out);
FILE* orb_os_fopen(const char* path, const char* mode);           // "rb" or "wb"
int orb_os_read_dir(const char* dir, orb_os_entry* out, int max); // unsorted, no dot entries

// A listing is sorted by name; a missing directory is empty.
uint64_t orb_os_file_mtime(const char* path);
int orb_os_list_dir(const char* dir, orb_os_entry* out, int max);
bool orb_os_read_file(const char* path, orb_arena* into, orb_span* out);
bool orb_os_write_file(const char* path, orb_span data);
void orb_path_join(orb_path out, const char* dir, const char* rel);

// orb implements this in core/api.c; the OS audio thread calls it for every
// buffer it needs: frames * ORB_AUDIO_CHANNELS interleaved int16 at ORB_AUDIO_RATE.
void orb_audio_render(int16_t* out, int frames);

// orb_mixer_idle on the one mixer.
bool orb_audio_idle(uint64_t elapsed_ns, uint64_t* rendered);

#ifdef ORB_OS_HEADLESS
const uint32_t* orb_os_headless_frame(void);
void orb_os_headless_set_input(const orb_input* in);
#endif

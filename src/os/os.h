#pragma once

#include "../core/arena.h"
#include "../orb.h"

#include <stdio.h>
#include <string.h>

constexpr u64 ORB_AUDIO_TICK_NS = 20000000;
constexpr u64 ORB_PAD_SCAN_NS = ORB_NS_PER_SECOND;
constexpr int ORB_PATH_MAX = 600;

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
    u64 size, mtime;
    bool dir;
} orb_os_info;

typedef struct orb_os_library orb_os_library;

static inline bool orb_has_suffix(const char* path, const char* suffix) {
    usize path_len = strlen(path), suffix_len = strlen(suffix);

    return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

// The native code a backend's table maps to a position, -1 when none does.
static inline int orb_os_key_code(const u8* table, int key) {
    if (key <= 0) return -1;

    for (int code = 0; code < 256; code++)
        if (table[code] == key) return code;

    return -1;
}

static inline int orb_os_open_scale(orb_size fb, orb_size screen) {
    int scale = 3;

    while (scale > 1 && (fb.width * scale > screen.width || fb.height * scale > screen.height))
        scale--;

    return scale;
}

// Paths are UTF-8 on every platform. On Windows this replaces main's ANSI argv
// with UTF-8 copies of the wide command line; elsewhere it leaves it alone.
void orb_os_args(int* argc, char*** argv);

bool orb_os_open(const orb_os_config* config);
bool orb_os_pump(orb_input* out);
void orb_os_present(const u32* rgb);

// The layout's unshifted codepoint for a printable position, 0 when it has none, and
// the position the layout produces a codepoint from, ORB_KEY_NONE when none does.
u32 orb_os_key_symbol(int key);
int orb_os_key_position(u32 codepoint);

void orb_os_close(void);

orb_os_library* orb_os_dlopen(const char* path);
void orb_os_dlclose(orb_os_library* lib);
void* orb_os_dlsym(orb_os_library* lib, const char* name);

bool orb_os_stat(const char* path, orb_os_info* out);
FILE* orb_os_fopen(const char* path, const char* mode); // "rb" or "wb"
bool orb_os_copy_file(const char* from, const char* to);
bool orb_os_make_dir(const char* path);
int orb_os_read_dir(const char* dir, orb_os_entry* out, int max); // unsorted, no dot entries

// A listing is sorted by name; a missing directory is empty.
u64 orb_os_file_mtime(const char* path);
int orb_os_list_dir(const char* dir, orb_os_entry* out, int max);
bool orb_os_read_file(const char* path, arena* into, u8_span* out);
bool orb_os_write_file(const char* path, u8_span data);
bool orb_path_absolute(const char* path);
void orb_path_join(orb_path out, const char* dir, const char* rel);

void orb_os_sleep(u64 duration_ns);
u64 orb_os_ticks(void);

u32 orb_os_pid(void);

// Address space with no memory behind it, then memory for part of it. at and size
// in commit are page-aligned; reserve returns nullptr when refused.
void* orb_os_reserve(usize size);
bool orb_os_commit(void* at, usize size);
void orb_os_release(void* base, usize size);

// Runs argv (null-terminated, argv[0] the program) with dir as its working
// directory, one output line at a time to line, and returns its exit status.
// On Windows the elements are joined with spaces behind cmd.exe /c, so none may need quoting.
int orb_os_run(const char* dir, const char* const* argv, void (*line)(const char* text));

// orb implements this in core/api.c; the OS audio thread calls it for every
// buffer it needs: frames * ORB_AUDIO_CHANNELS interleaved int16 at ORB_AUDIO_RATE.
void orb_audio_render(i16* out, int frames);

// orb_mixer_idle on the one mixer.
bool orb_audio_idle(u64 elapsed_ns, u64* rendered);

#ifdef ORB_OS_HEADLESS
const u32* orb_os_headless_frame(void);
void orb_os_headless_set_input(const orb_input* input);
#endif

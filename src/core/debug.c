#include "debug.h"
#include "../cast/file.h"
#include "../os/os.h"
#include "console.h"
#include "host.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int DEBUG_MAX_WATCHES = 1024;
constexpr uint64_t DEBUG_SETTLE_NS = 200000000;

static_assert(DEBUG_MAX_WATCHES >= ORB_CAST_MAX_READS, "every cast read fits the asset watch");

typedef struct debug_watches {
    orb_watch at[DEBUG_MAX_WATCHES];
    int count;
} debug_watches;

static orb_os_library* debug_lib;
static int debug_copy_count;
static orb_path debug_dir, debug_so_path, debug_copy_path;
static orb_watch debug_so_watch;
static debug_watches debug_assets, debug_sources;
static uint8_t debug_depfile_mem[1 << 16];

void orb_watch_init(orb_watch* w, const char* path) {
    snprintf(w->path, sizeof w->path, "%s", path);
    w->mtime = orb_os_file_mtime(path);
    w->pending_mtime = 0;
    w->pending_since = 0;
}

bool orb_watch_poll(orb_watch* w, uint64_t now) {
    uint64_t mtime = orb_os_file_mtime(w->path);

    if (mtime == 0) return false; // missing, mid-save: wait for it to come back

    if (mtime == w->mtime) {
        w->pending_mtime = 0;
        return false;
    }

    if (mtime != w->pending_mtime) {
        w->pending_mtime = mtime;
        w->pending_since = now;
        return false;
    }

    if (now - w->pending_since < DEBUG_SETTLE_NS) return false;

    w->mtime = mtime;
    w->pending_mtime = 0;
    return true;
}

// Copy the game library to a fresh name and load the copy, so the compiler can overwrite
// the original while it is loaded (Windows locks loaded DLLs; the copy keeps
// both platforms on one path). The name carries the process id and any leftover
// is removed before the copy, so a second orb on the same game never truncates
// a file this process has mapped. The previous copy is closed and removed only
// after the new one loaded, so a broken build keeps the old code running.
static const orb_game* debug_load_game(void) {
    char name[64];
    snprintf(
        name, sizeof name, "build/.orb-game-%u-%d" ORB_OS_LIB_SUFFIX, orb_os_pid(),
        debug_copy_count++
    );
    orb_path path;

    orb_path_join(path, debug_dir, name);
    remove(path);

    if (!orb_os_copy_file(debug_so_path, path)) {
        orb_log("cannot copy %s to %s", debug_so_path, path);
        return nullptr;
    }

    orb_os_library* lib = orb_os_dlopen(path);

    if (!lib) {
        orb_log("cannot load %s", path);
        remove(path);
        return nullptr;
    }

    const orb_game* (*entry)(void) = (const orb_game* (*)(void))orb_os_dlsym(lib, "orb_game_main");

    if (!entry) {
        orb_log("%s does not define orb_game_main", debug_so_path);
        orb_os_dlclose(lib);
        remove(path);
        return nullptr;
    }

    // Install the new game before the old library goes away, so the host
    // never points at unmapped memory, even for an instant.
    const orb_game* game = entry();

    if (debug_lib) {
        orb_set_game(game);
        orb_os_dlclose(debug_lib);
        remove(debug_copy_path);
    }

    debug_lib = lib;
    strcpy(debug_copy_path, path);
    return game;
}

static void debug_watch_add(debug_watches* w, const char* rel) {
    orb_path path;

    orb_path_join(path, debug_dir, rel);

    for (int i = 0; i < w->count; i++)
        if (strcmp(w->at[i].path, path) == 0) return;

    if (w->count == DEBUG_MAX_WATCHES) orb_fatal("more than %d watched files", DEBUG_MAX_WATCHES);

    orb_watch_init(&w->at[w->count++], path);
}

// Polls every watch, so each one's settle timer advances; true when any fired.
static bool debug_watch_any(debug_watches* w, uint64_t now) {
    bool changed = false;

    for (int i = 0; i < w->count; i++)
        if (orb_watch_poll(&w->at[i], now)) changed = true;

    return changed;
}

// Watch what the last cast read, the way sources are watched through what the build read.
static void debug_watch_assets(void) {
    const orb_cast_result* r = orb_last_cast();

    debug_assets.count = 0;

    for (int i = 0; i < r->read_count; i++)
        debug_watch_add(&debug_assets, r->reads[i]);
}

static void debug_watch_depfile(orb_span text) {
    for (size_t i = 0; i < text.len;) {
        orb_path token;
        size_t n = 0;

        for (; i < text.len; i++) {
            uint8_t c = text.ptr[i], next = i + 1 < text.len ? text.ptr[i + 1] : 0;

            if (c == '\\' && isspace(next)) {
                if (next != ' ') break;
                c = ' ';
                i++;
            } else if (isspace(c))
                break;

            if (n + 1 == sizeof token) orb_fatal("dependency path too long: %.*s", (int)n, token);

            token[n++] = (char)c;
        }

        i++;
        token[n] = 0;

        if (n && token[n - 1] != ':') debug_watch_add(&debug_sources, token);
    }
}

static bool debug_watch_sources(void) {
    static orb_os_entry entries[DEBUG_MAX_WATCHES];
    orb_path build;
    orb_arena a;

    orb_path_join(build, debug_dir, "build");
    orb_arena_init(&a, "depfile", debug_depfile_mem, sizeof debug_depfile_mem);

    int n = orb_os_list_dir(build, entries, DEBUG_MAX_WATCHES), depfiles = 0;

    for (int i = 0; i < n; i++)
        depfiles += orb_has_suffix(entries[i].name, ".d");

    if (!depfiles) {
        orb_log(
            "scry: no build/*.d files; compile with -MMD so scry can watch what the build reads"
        );
        return false;
    }

    debug_sources.count = 0;

    for (int i = 0; i < n; i++) {
        orb_path path;
        orb_span text;

        if (!orb_has_suffix(entries[i].name, ".d")) continue;

        orb_path_join(path, build, entries[i].name);
        orb_arena_reset(&a);

        if (!orb_os_read_file(path, &a, &text)) orb_fatal("cannot read %s", path);

        debug_watch_depfile(text);
    }

    return true;
}

static bool debug_is_make_noise(const char* line) {
    return strncmp(line, "make", 4) == 0 && (strstr(line, "*** [") || strstr(line, "directory '"));
}

static void debug_build_line(const char* line) {
    if (!debug_is_make_noise(line)) fprintf(stderr, "%s\n", line);
}

// Run the game's own build. orb contains no compiler; scry only invokes make.
static bool debug_build(void) {
    const char* argv[] = {
        "make", "--no-print-directory", "-s", "build/game" ORB_OS_LIB_SUFFIX, nullptr
    };

    return orb_os_run(debug_dir, argv, debug_build_line) == 0;
}

static void debug_recast(void) {
    orb_error err;

    if (orb_recast(&err)) {
        orb_log("scry: recast assets");
        debug_watch_assets();
    } else
        orb_log("scry: cast failed, keeping the previous assets: %s", err.text);
}

static void debug_poll_reload(void) {
    uint64_t now = orb_os_ticks();

    if (orb_watch_poll(&debug_so_watch, now)) {
        if (debug_load_game()) orb_log("scry: reloaded %s", debug_so_path);
    }

    if (debug_watch_any(&debug_assets, now)) debug_recast();
}

// Every sixth frame: a stat per watched file at 60 Hz would be most of a frame
// once a game has hundreds, and the settle window is time-based anyway.
static void debug_poll_scry(void) {
    static unsigned frame;

    if (frame++ % 6) return;

    uint64_t now = orb_os_ticks();

    if (debug_watch_any(&debug_sources, now)) {
        if (debug_build())
            debug_watch_sources();
        else
            orb_log("scry: build failed, keeping the running code");
    }

    debug_poll_reload();
}

static void debug_command_recast(void*, const orb_api*, int, const char* const*) {
    debug_recast();
}

static void debug_command_watch(void*, const orb_api*, int, const char* const*) {
    orb_log("%d sources, %d assets", debug_sources.count, debug_assets.count);
}

static void debug_command_stats(void*, const orb_api*, int, const char* const*) {
    orb_stats st = orb_stats_get();

    orb_log(
        "state %s, pool %s, assets %s, cast peak %s; release %s + %s sealed; frame %d ticks, %u us",
        orb_bytes_format(st.state).text, orb_bytes_format(st.pool).text,
        orb_bytes_format(st.assets).text, orb_bytes_format(st.cast_peak).text,
        orb_bytes_format(st.release).text, orb_bytes_format(st.assets).text, st.frame_ticks,
        st.frame_us
    );
}

static void debug_command_step(void*, const orb_api*, int, const char* const*) {
    orb_clock_get()->step = true;
}

// Before orb_boot, so these are orb's own and survive every clear.
static void debug_console_register(void) {
    static bool done;

    if (done) return;

    done = true;
    orb_console_var_float("timescale", &orb_clock_get()->timescale, "clock rate, 1 is real time");
    orb_console_var_bool("pause", &orb_clock_get()->paused, "stop ticking");
    orb_console_command("step", debug_command_step, "one tick while paused");
    orb_console_command("recast", debug_command_recast, "cast the art now");
    orb_console_command("watch", debug_command_watch, "the watched file counts");
    orb_console_command("stats", debug_command_stats, "memory and frame numbers");
}

static int debug_boot(const char* game_dir) {
    debug_console_register();
    snprintf(debug_dir, sizeof debug_dir, "%s", game_dir);
    orb_path_join(debug_so_path, game_dir, "build/game" ORB_OS_LIB_SUFFIX);

    const orb_game* game = debug_load_game();

    if (!game) return 1;

    orb_error err;

    if (!orb_boot(game, game_dir, (orb_span) {}, &err)) {
        orb_log("%s", err.text);
        return 1;
    }

    return 0;
}

static int debug_finish(void) {
    orb_quit();
    orb_os_dlclose(debug_lib);
    remove(debug_copy_path);
    return 0;
}

int orb_debug_cast(const char* dir, bool seal, const char* out_path) {
    static uint8_t boot_mem[1 << 18];
    orb_arena boot;
    orb_arena_init(&boot, "boot", boot_mem, sizeof boot_mem);
    orb_error err;
    orb_manifest m;

    if (!orb_manifest_load(&boot, dir, &m, &err)) {
        orb_log("orb: %s", err.text);
        return 1;
    }

    orb_arena scratch, out;

    if (!orb_arena_reserve(&scratch, "cast scratch", ORB_REGION_RESERVE) ||
        !orb_arena_reserve(&out, "asset", ORB_REGION_RESERVE)) {
        orb_log("orb: cannot reserve address space for the cast");
        return 1;
    }

    orb_cast_result result;
    orb_assets as;

    if (!orb_cast_game(&scratch, &out, dir, &m, &result, &err) ||
        !orb_file_load(result.file, &as, &err)) {
        orb_log("orb: %s", err.text);
        return 1;
    }

    orb_path bin, name, sealed;

    if (seal && !out_path) {
        orb_path_join(bin, dir, "bin");

        if (!orb_os_make_dir(bin)) {
            orb_log("orb: cannot create %s", bin);
            return 1;
        }

        snprintf(name, sizeof name, "%s.orb", m.id);
        orb_path_join(sealed, bin, name);
        out_path = sealed;
    }

    if (out_path && !orb_os_write_file(out_path, result.file)) {
        orb_log("orb: cannot write %s", out_path);
        return 1;
    }

    printf(
        "%s %u sprites, %u animations, %u levels, %u layers, %u types, %u entities, %u samples, "
        "%u songs, %u fonts, %u glyphs (%s sealed; cast peak %s)\n",
        out_path ? "sealed" : "cast", as.sprite_count, as.anim_count, as.level_count,
        as.layer_count, as.type_count, as.placement_count, as.sample_count, as.song_count,
        as.font_count, as.glyph_count, orb_bytes_format(result.file.len).text,
        orb_bytes_format(scratch.peak).text
    );

    return 0;
}

int orb_debug_run(const char* game_dir) {
    if (debug_boot(game_dir)) return 1;

    while (orb_frame())
        continue;

    return debug_finish();
}

int orb_debug_scry(const char* game_dir) {
    snprintf(debug_dir, sizeof debug_dir, "%s", game_dir);

    if (!debug_build() || !debug_watch_sources()) return 1;
    if (debug_boot(game_dir)) return 1;

    orb_watch_init(&debug_so_watch, debug_so_path);
    debug_watch_assets();
    orb_log(
        "scry: watching %d source files and %d asset files", debug_sources.count, debug_assets.count
    );

    while (orb_frame())
        debug_poll_scry();

    return debug_finish();
}

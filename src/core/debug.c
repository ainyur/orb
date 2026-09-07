#include "debug.h"
#include "../os/os.h"
#include "log.h"
#include "run.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr uint64_t DEBUG_SETTLE_NS = 200000000;
constexpr int DEBUG_MAX_WATCHES = 1024;

void orb_watch_init(orb_watch* w, const char* path) {
    snprintf(w->path, sizeof w->path, "%s", path);
    w->mtime = orb_os_file_mtime(path);
    w->pending = 0;
    w->pending_since = 0;
}

bool orb_watch_poll(orb_watch* w, uint64_t now) {
    uint64_t mtime = orb_os_file_mtime(w->path);

    if (mtime == 0) return false; // missing, mid-save: wait for it to come back

    if (mtime == w->mtime) {
        w->pending = 0;
        return false;
    }

    if (mtime != w->pending) {
        w->pending = mtime;
        w->pending_since = now;
        return false;
    }

    if (now - w->pending_since < DEBUG_SETTLE_NS) return false;

    w->mtime = mtime;
    w->pending = 0;
    return true;
}

static void* debug_lib;
static int debug_copy_count;
static orb_path debug_dir, debug_so_path, debug_copy_path;
static orb_watch debug_so_watch;
static orb_watch debug_asset_watches[DEBUG_MAX_WATCHES];
static int debug_asset_count;
static orb_watch debug_source_watches[DEBUG_MAX_WATCHES];
static int debug_source_count;
static uint8_t debug_depfile_mem[1 << 16];

// Copy the game library to a fresh name and load the copy, so the compiler can overwrite
// the original while it is loaded (Windows locks loaded DLLs; the copy keeps
// both platforms on one path). The previous copy is closed and removed only
// after the new one loaded, so a broken build keeps the old code running.
static const orb_game* debug_load_game(void) {
    char name[64];
    snprintf(name, sizeof name, "build/.orb-game-%d" ORB_OS_LIB_SUFFIX, debug_copy_count++);
    orb_path path;

    orb_path_join(path, debug_dir, name);

    if (!orb_os_copy_file(debug_so_path, path)) {
        orb_log("cannot copy %s to %s", debug_so_path, path);
        return nullptr;
    }

    void* lib = orb_os_dlopen(path);

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

    // Install the new game before the old library goes away, so run_game
    // never points at unmapped memory, even for an instant.
    const orb_game* game = entry();

    if (debug_lib) {
        orb_run_set_game(game);
        orb_os_dlclose(debug_lib);
        remove(debug_copy_path);
    }

    debug_lib = lib;
    strcpy(debug_copy_path, path);
    return game;
}

static void debug_watch_add(orb_watch* table, int* count, const char* rel) {
    orb_path path;

    orb_path_join(path, debug_dir, rel);

    for (int i = 0; i < *count; i++)
        if (strcmp(table[i].path, path) == 0) return;

    if (*count == DEBUG_MAX_WATCHES) orb_fatal("more than %d watched files", DEBUG_MAX_WATCHES);

    orb_watch_init(&table[(*count)++], path);
}

static void debug_watch_assets(void) {
    const orb_manifest* m = orb_run_manifest();

    debug_asset_count = 0;
    debug_watch_add(debug_asset_watches, &debug_asset_count, "orb.json");
    debug_watch_add(debug_asset_watches, &debug_asset_count, m->palette);

    for (int i = 0; i < m->sprite_count; i++)
        debug_watch_add(debug_asset_watches, &debug_asset_count, m->sprites[i]);
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

        if (n && token[n - 1] != ':')
            debug_watch_add(debug_source_watches, &debug_source_count, token);
    }
}

static bool debug_watch_sources(void) {
    static orb_path depfiles[DEBUG_MAX_WATCHES];
    orb_path build;
    orb_arena a;

    orb_path_join(build, debug_dir, "build");
    orb_arena_init(&a, "depfile", debug_depfile_mem, sizeof debug_depfile_mem);

    int files = orb_os_list_dir(build, ".d", depfiles, DEBUG_MAX_WATCHES);

    if (files <= 0) {
        orb_log(
            "scry: no build/*.d files; compile with -MMD so scry can watch what the build reads"
        );
        return false;
    }

    debug_source_count = 0;

    for (int i = 0; i < files; i++) {
        orb_span text;

        orb_arena_reset(&a);

        if (!orb_os_read_file(depfiles[i], &a, &text)) orb_fatal("cannot read %s", depfiles[i]);

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
// Double quotes: the one quoting both sh and cmd.exe understand.
static bool debug_build(void) {
    char command[ORB_PATH_MAX + 64];

    snprintf(
        command, sizeof command,
        "make --no-print-directory -s -C \"%s\" build/game" ORB_OS_LIB_SUFFIX, debug_dir
    );
    return orb_os_run(command, debug_build_line) == 0;
}

static void debug_poll_reload(void) {
    uint64_t now = orb_os_ticks();

    if (orb_watch_poll(&debug_so_watch, now)) {
        if (debug_load_game()) orb_log("scry: reloaded %s", debug_so_path);
    }

    bool changed = false;

    for (int i = 0; i < debug_asset_count; i++) {
        if (orb_watch_poll(&debug_asset_watches[i], now)) changed = true;
    }

    if (changed) {
        orb_error err;

        if (orb_run_recast(&err)) {
            orb_log("scry: recast assets");
            debug_watch_assets();
        } else
            orb_log("scry: cast failed, keeping the previous assets: %s", err.text);
    }
}

static void debug_poll_scry(void) {
    uint64_t now = orb_os_ticks();
    bool changed = false;

    for (int i = 0; i < debug_source_count; i++) {
        if (orb_watch_poll(&debug_source_watches[i], now)) changed = true;
    }

    if (changed) {
        if (debug_build())
            debug_watch_sources();
        else
            orb_log("scry: build failed, keeping the running code");
    }

    debug_poll_reload();
}

static int debug_boot(const char* game_dir) {
    snprintf(debug_dir, sizeof debug_dir, "%s", game_dir);
    orb_path_join(debug_so_path, game_dir, "build/game" ORB_OS_LIB_SUFFIX);

    const orb_game* game = debug_load_game();

    if (!game) return 1;

    orb_error err;

    if (!orb_run_boot(game, game_dir, &err)) {
        orb_log("%s", err.text);
        return 1;
    }

    return 0;
}

static int debug_finish(void) {
    orb_os_close();
    orb_os_dlclose(debug_lib);
    remove(debug_copy_path);
    return 0;
}

int orb_debug_run(const char* game_dir) {
    if (debug_boot(game_dir)) return 1;

    orb_run_loop(nullptr);
    return debug_finish();
}

int orb_debug_scry(const char* game_dir) {
    snprintf(debug_dir, sizeof debug_dir, "%s", game_dir);

    if (!debug_build() || !debug_watch_sources()) return 1;
    if (debug_boot(game_dir)) return 1;

    orb_watch_init(&debug_so_watch, debug_so_path);
    debug_watch_assets();
    orb_log(
        "scry: watching %d source files, %d art files, and orb.json", debug_source_count,
        debug_asset_count - 1
    );

    orb_run_loop(debug_poll_scry);
    return debug_finish();
}

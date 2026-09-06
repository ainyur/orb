#include "debug.h"
#include "../os/orb_os.h"
#include "log.h"
#include "run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_SETTLE_NS 200000000u
#define DEBUG_MAX_WATCHES 64

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
        return NULL;
    }

    void* lib = orb_os_dlopen(path);

    if (!lib) {
        orb_log("cannot load %s", path);
        remove(path);
        return NULL;
    }

    const orb_game* (*entry)(void) = (const orb_game* (*)(void))orb_os_dlsym(lib, "orb_game_main");

    if (!entry) {
        orb_log("%s does not define orb_game_main", debug_so_path);
        orb_os_dlclose(lib);
        remove(path);
        return NULL;
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

static void debug_add_watch(const char* rel) {
    if (debug_asset_count == DEBUG_MAX_WATCHES)
        orb_fatal("more than %d watched files", DEBUG_MAX_WATCHES);

    orb_path path;

    orb_path_join(path, debug_dir, rel);
    orb_watch_init(&debug_asset_watches[debug_asset_count++], path);
}

// game.json and everything it names. Rebuilt after every successful recast, since
// the manifest may have gained or lost files.
static void debug_watch_assets(void) {
    const orb_manifest* m = orb_run_manifest();

    debug_asset_count = 0;
    debug_add_watch("game.json");
    debug_add_watch(m->palette);

    for (int i = 0; i < m->sprite_count; i++)
        debug_add_watch(m->sprites[i]);
}

// make's own lines: its failure summary and directory chatter. The compiler's
// diagnostics are what the author needs, and scry reports the failure itself.
static bool debug_is_make_noise(const char* line) {
    return strncmp(line, "make", 4) == 0 && (line[4] == ':' || line[4] == '[');
}

static void debug_build_line(const char* line) {
    if (!debug_is_make_noise(line)) fprintf(stderr, "%s\n", line);
}

// Run the game's own build. orb contains no compiler; scry only invokes make.
// Double quotes: the one quoting both sh and cmd.exe understand.
static bool debug_build(void) {
    char command[ORB_PATH_MAX + 64];

    snprintf(command, sizeof command, "make --no-print-directory -s -C \"%s\"", debug_dir);
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

    if (changed && !debug_build()) orb_log("scry: build failed, keeping the running code");

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

    orb_run_loop(NULL);
    return debug_finish();
}

int orb_debug_scry(const char* game_dir) {
    snprintf(debug_dir, sizeof debug_dir, "%s", game_dir);

    if (!debug_build()) return 1;
    if (debug_boot(game_dir)) return 1;

    orb_watch_init(&debug_so_watch, debug_so_path);
    debug_watch_assets();

    orb_path sources[DEBUG_MAX_WATCHES];
    int c = orb_os_list_dir(game_dir, ".c", sources, DEBUG_MAX_WATCHES);
    int h = orb_os_list_dir(game_dir, ".h", sources + c, DEBUG_MAX_WATCHES - c);

    debug_source_count = c + h;

    for (int i = 0; i < debug_source_count; i++)
        orb_watch_init(&debug_source_watches[i], sources[i]);

    orb_log(
        "scry: watching %d source files, %d art files, and game.json", debug_source_count,
        debug_asset_count - 1
    );

    orb_run_loop(debug_poll_scry);
    return debug_finish();
}

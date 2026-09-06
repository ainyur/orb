#pragma once
#include "../os/orb_os.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct orb_watch {
    orb_path path;
    uint64_t mtime, pending, pending_since;
} orb_watch;

void orb_watch_init(orb_watch* w, const char* path);
bool orb_watch_poll(orb_watch* w, uint64_t now);
int orb_debug_run(const char* game_dir);  // plain run: no watching
int orb_debug_scry(const char* game_dir); // dev loop: build on change, reload, recast

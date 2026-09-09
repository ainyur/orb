#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

#include <stdbool.h>

typedef struct orb_manifest {
    const char* id;
    const char* name;
    const char* palette;
    const char** sprites;
    int sprite_count;
    const char** sounds;
    int sound_count;
    const char** songs;
    int song_count;
    int size_w, size_h;
    size_t asset_headroom;
} orb_manifest;

typedef struct orb_cast_result {
    orb_span file;
    uint32_t sprite_count;
    uint32_t animation_count;
    uint32_t sample_count;
    uint32_t song_count;
} orb_cast_result;

bool orb_manifest_load(orb_arena* a, const char* game_dir, orb_manifest* m, orb_error* err);
bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* m,
    orb_cast_result* r,
    orb_error* err
);
const char* orb_seal_path(orb_arena* a, const char* game_dir, const orb_manifest* m);

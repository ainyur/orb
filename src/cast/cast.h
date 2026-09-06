#pragma once
#include "../core/arena.h"
#include "../orb.h"
#include <stdbool.h>

typedef struct orb_manifest {
    const char* id;
    const char* name;
    const char* palette;
    const char** sprites;
    int sprite_count;
    int size_w, size_h;
    size_t asset_headroom;
} orb_manifest;

typedef struct orb_cast_result {
    orb_span file;
    const char** sprite_names;
    uint32_t sprite_count;
    const char** animation_names;
    uint32_t animation_count;
} orb_cast_result;

bool orb_manifest_load(orb_arena* a, const char* game_dir, orb_manifest* m, orb_error* err);
// Read game.json, cast everything it names into out, and write game_assets.h
// beside it. Exhausting either arena is a cast error, not a fatal.
bool orb_cast_game(
    orb_arena* scratch, orb_arena* out, const char* game_dir, orb_manifest* m, orb_cast_result* r,
    orb_error* err
);
// Where seal writes when no path is given: bin/<id>.orb beside the manifest.
const char* orb_seal_path(orb_arena* a, const char* game_dir, const orb_manifest* m);

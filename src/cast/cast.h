#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

#include <stdbool.h>

typedef struct orb_manifest_song {
    float bpm; // within 0..1000
    const char* stem;
} orb_manifest_song;

// orb.json. id, name, and size are required; the rest default to the layout
// art/palette.aseprite, art/, sfx/, and music/ and 16 MB of headroom. The
// directories are walked recursively at cast, so only songs list files, one
// stem to tempo each, since a rendered file carries none.
typedef struct orb_manifest {
    const char* id;
    const char* name;
    const char* palette;
    const char* art;
    const char* sfx;
    const char* music;
    const orb_manifest_song* songs;
    int song_count;
    int size_w, size_h;
    size_t asset_headroom;
} orb_manifest;

constexpr int ORB_CAST_MAX_READS = 1024;

typedef struct orb_cast_result {
    orb_span file;
    uint32_t sprite_count;
    uint32_t animation_count;
    uint32_t sample_count;
    uint32_t song_count;
    const char** reads; // every file the cast read, relative to game_dir, once each, in order
    int read_count;
} orb_cast_result;

bool orb_manifest_load(orb_arena* a, const char* game_dir, orb_manifest* m, orb_error* err);
bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* m, // loaded by the cast, so the caller reads what it cast from
    orb_cast_result* r,
    orb_error* err
);

#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

constexpr int ORB_CAST_MAX_READS = 1024;

list(reads, const char*);

typedef struct orb_cast_result {
    u8_span file;
    reads_list reads; // every file the cast read, relative to game_dir, once each, in order
} orb_cast_result;

typedef struct orb_manifest_song {
    f32 bpm;
    const char* stem;
} orb_manifest_song;

list(orb_manifest_song);

// orb.json. id, name, and size are required; the rest default to the layout
// art/palette.aseprite, art/, fonts/, sfx/, music/, and levels/world.ldtk. The
// directories are walked recursively at cast, so only songs list files, one stem
// to tempo each, since a rendered file carries none.
typedef struct orb_manifest {
    const char* id;
    const char* name;
    const char* pal;
    const char* art;
    const char* fonts;
    const char* sfx;
    const char* music;
    const char* world;
    orb_manifest_song_span songs;
    orb_size size;
} orb_manifest;

[[nodiscard]] bool orb_cast_game(
    arena* scratch,
    arena* out,
    const char* game_dir,
    orb_manifest* manifest, // loaded by the cast, so the caller reads what it cast from
    orb_cast_result* result,
    orb_error* err
);
[[nodiscard]] bool orb_manifest_load(
    arena* out,
    const char* game_dir,
    orb_manifest* manifest,
    orb_error* err
);

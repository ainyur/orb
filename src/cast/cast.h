#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

constexpr int ORB_CAST_MAX_READS = 1024;

typedef struct orb_cast_result {
    orb_span file;
    const char** reads; // every file the cast read, relative to game_dir, once each, in order
    int read_count;
} orb_cast_result;

typedef struct orb_manifest_song {
    float bpm;
    const char* stem;
} orb_manifest_song;

// orb.json. id, name, and size are required; the rest default to the layout
// art/palette.aseprite, art/, fonts/, sfx/, music/, and levels/world.ldtk. The
// directories are walked recursively at cast, so only songs list files, one stem
// to tempo each, since a rendered file carries none. buttons maps a button name
// to a key symbol.
typedef struct orb_manifest {
    const char* id;
    const char* name;
    const char* pal;
    const char* art;
    const char* fonts;
    const char* sfx;
    const char* music;
    const char* world;
    const orb_manifest_song* songs;
    int song_count;
    orb_key_symbol buttons[ORB_BTN_COUNT]; // empty where the manifest named none
    orb_size size;
} orb_manifest;

[[nodiscard]] bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* m, // loaded by the cast, so the caller reads what it cast from
    orb_cast_result* r,
    orb_error* err
);
[[nodiscard]] bool orb_manifest_load(
    orb_arena* a,
    const char* game_dir,
    orb_manifest* m,
    orb_error* err
);

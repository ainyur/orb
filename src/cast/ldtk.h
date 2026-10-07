// An LDtk project as orb reads it: tilesets with a path, layer definitions, and levels bottom to
// top.
#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"
#include "file.h"

orb_span(u8);

typedef struct orb_ldtk_tile {
    u16 cell_x, cell_y, id;
    u8 flip;
} orb_ldtk_tile;

orb_span(orb_ldtk_tile);

typedef struct orb_ldtk_layer {
    const char* name;
    int grid, columns, rows, offset_x, offset_y;
    f32 parallax_x, parallax_y;
    int tileset;              // index into orb_ldtk.tilesets, or -1
    const u8* cells;          // columns * rows, or nullptr
    orb_ldtk_tile_span tiles; // display order
} orb_ldtk_layer;

typedef struct orb_ldtk_neighbor {
    const char* level_iid;
    orb_level_dir dir;
} orb_ldtk_neighbor;

orb_slice(orb_ldtk_neighbor);

typedef union orb_ldtk_value {
    i32 integer;
    f32 number;
    bool boolean;
    const char* string; // a string, or the iid a ref names until the cast resolves it
    orb_vec2 point;     // world pixels
} orb_ldtk_value;

orb_slice(orb_ldtk_value);

typedef struct orb_ldtk_field {
    const char* name;
    orb_field_kind kind;
    orb_ldtk_value_slice values;
} orb_ldtk_field;

orb_slice(orb_ldtk_field);

typedef struct orb_ldtk_instance {
    const char* iid;
    int def;                     // index into orb_ldtk.entity_defs
    int x, y, width, height;     // world pixels
    orb_ldtk_field_slice fields; // the non-null values the definition declares
} orb_ldtk_instance;

orb_slice(orb_ldtk_instance);

orb_slice(orb_ldtk_layer);

typedef struct orb_ldtk_level {
    const char *name, *iid,
        *external_path; // external_path relative to the project file, or nullptr
    int world_x, world_y, width, height;
    orb_ldtk_layer_slice layers; // bottom to top
    orb_ldtk_neighbor_slice neighbors;
    orb_ldtk_instance_slice instances; // every Entities layer's, bottom to top
} orb_ldtk_level;

orb_slice(orb_ldtk_level);

typedef struct orb_ldtk_tileset {
    const char* path;
    int uid, grid, spacing, padding, columns, rows, width, height;
} orb_ldtk_tileset;

orb_slice(orb_ldtk_tileset);

typedef struct orb_ldtk_layer_def {
    const char* name;
    int uid, tileset_uid;
    f32 parallax_x, parallax_y;
    bool scaling;
} orb_ldtk_layer_def;

orb_slice(orb_ldtk_layer_def);

// A tileset definition dropped from orb_ldtk.tilesets: no relPath, or one that
// does not end in .aseprite (path is nullptr for the former).
typedef struct orb_ldtk_skipped {
    int uid;
    const char* path;
} orb_ldtk_skipped;

orb_slice(orb_ldtk_skipped);

orb_slice(field_names, const char*);

typedef struct orb_ldtk_entity_def {
    const char* name;
    int uid, width, height;
    field_names_slice field_names; // every declared field, skipped kinds included
    orb_ldtk_field_slice fields;   // the non-null defaults
} orb_ldtk_entity_def;

orb_slice(orb_ldtk_entity_def);

typedef struct orb_ldtk {
    orb_ldtk_tileset_slice tilesets; // definitions with an .aseprite relPath; others are skipped
    orb_ldtk_skipped_slice skipped;
    orb_ldtk_layer_def_slice layer_defs;
    orb_ldtk_entity_def_slice entity_defs;
    orb_ldtk_level_slice levels;
} orb_ldtk;

[[nodiscard]] bool orb_ldtk_parse(
    orb_arena* scratch,
    u8_span text,
    const char* name,
    orb_ldtk* out,
    orb_error* err
);
[[nodiscard]] bool orb_ldtk_parse_level(
    orb_arena* scratch,
    u8_span text,
    const char* name,
    const orb_ldtk* project,
    orb_ldtk_level* level,
    orb_error* err
);

// An LDtk project as orb reads it: tilesets with a path, layer definitions, and levels bottom to
// top.
#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"
#include "file.h"

typedef struct orb_ldtk_tile {
    uint16_t cell_x, cell_y, id;
    uint8_t flip;
} orb_ldtk_tile;

typedef struct orb_ldtk_layer {
    const char* name;
    int grid, columns, rows, offset_x, offset_y;
    float parallax_x, parallax_y;
    int tileset;                // index into orb_ldtk.tilesets, or -1
    const uint8_t* cells;       // columns * rows, or nullptr
    const orb_ldtk_tile* tiles; // display order
    int tile_count;
} orb_ldtk_layer;

typedef struct orb_ldtk_neighbor {
    const char* level_iid;
    orb_level_dir dir;
} orb_ldtk_neighbor;

typedef union orb_ldtk_value {
    int32_t i;
    float f;
    bool b;
    const char* s;  // a string, or the iid a ref names until the cast resolves it
    orb_vec2 point; // world pixels
} orb_ldtk_value;

typedef struct orb_ldtk_field {
    const char* name;
    orb_field_kind kind;
    int count;
    orb_ldtk_value* values;
} orb_ldtk_field;

typedef struct orb_ldtk_instance {
    const char* iid;
    int def;                 // index into orb_ldtk.entity_defs
    int x, y, width, height; // world pixels
    orb_ldtk_field* fields;  // the non-null values the definition declares
    int field_count;
} orb_ldtk_instance;

typedef struct orb_ldtk_level {
    const char *name, *iid,
        *external_path; // external_path relative to the project file, or nullptr
    int world_x, world_y, width, height;
    orb_ldtk_layer* layers; // bottom to top
    int layer_count;
    orb_ldtk_neighbor* neighbors;
    int neighbor_count;
    orb_ldtk_instance* instances; // every Entities layer's, bottom to top
    int instance_count;
} orb_ldtk_level;

typedef struct orb_ldtk_tileset {
    const char* path;
    int uid, grid, spacing, padding, columns, rows, width, height;
} orb_ldtk_tileset;

typedef struct orb_ldtk_layer_def {
    const char* name;
    int uid, tileset_uid;
    float parallax_x, parallax_y;
    bool scaling;
} orb_ldtk_layer_def;

// A tileset definition dropped from orb_ldtk.tilesets: no relPath, or one that
// does not end in .aseprite (path is nullptr for the former).
typedef struct orb_ldtk_skipped {
    int uid;
    const char* path;
} orb_ldtk_skipped;

typedef struct orb_ldtk_entity_def {
    const char* name;
    int uid, width, height;
    const char** field_names; // every declared field, skipped kinds included
    int field_name_count;
    orb_ldtk_field* fields; // the non-null defaults
    int field_count;
} orb_ldtk_entity_def;

typedef struct orb_ldtk {
    orb_ldtk_tileset* tilesets;
    int tileset_count; // definitions with an .aseprite relPath; others are skipped
    orb_ldtk_skipped* skipped;
    int skipped_count;
    orb_ldtk_layer_def* layer_defs;
    int layer_def_count;
    orb_ldtk_entity_def* entity_defs;
    int entity_def_count;
    orb_ldtk_level* levels;
    int level_count;
} orb_ldtk;

[[nodiscard]] bool orb_ldtk_parse(
    orb_arena* a,
    orb_span text,
    const char* name,
    orb_ldtk* out,
    orb_error* err
);
[[nodiscard]] bool orb_ldtk_parse_level(
    orb_arena* a,
    orb_span text,
    const char* name,
    const orb_ldtk* project,
    orb_ldtk_level* level,
    orb_error* err
);

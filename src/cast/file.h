#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

typedef struct orb_assets orb_assets;

constexpr u32 ORB_FILE_MAGIC = 0x0042524F;
constexpr u32 ORB_FILE_VERSION = 1;

typedef enum orb_section_kind {
    ORB_SEC_PAL = 1,
    ORB_SEC_SHEETS,
    ORB_SEC_PIXELS,
    ORB_SEC_SPRITES,
    ORB_SEC_ANIMS,
    ORB_SEC_DURATIONS,
    ORB_SEC_SPRITE_IDS,
    ORB_SEC_ANIM_IDS,
    ORB_SEC_SAMPLES,
    ORB_SEC_PCM,
    ORB_SEC_SAMPLE_IDS,
    ORB_SEC_SONGS,
    ORB_SEC_SONG_IDS,
    ORB_SEC_INFO,
    ORB_SEC_TILESETS,
    ORB_SEC_LEVELS,
    ORB_SEC_LAYERS,
    ORB_SEC_NEIGHBORS,
    ORB_SEC_TILES,
    ORB_SEC_CELLS,
    ORB_SEC_LEVEL_IDS,
    ORB_SEC_LAYER_IDS,
    ORB_SEC_FONTS,
    ORB_SEC_GLYPHS,
    ORB_SEC_FONT_IDS,
    ORB_SEC_TYPES,
    ORB_SEC_PLACEMENTS,
    ORB_SEC_FIELDS,
    ORB_SEC_FIELD_DATA,
    ORB_SEC_TYPE_IDS,
    ORB_SEC_COUNT_
} orb_section_kind;

// Handle indices are 24 bits; these bound the runtime's per-index generation tables.
constexpr u32 ORB_MAX_SPRITES = 1 << 14;
constexpr u32 ORB_MAX_ANIMS = 1 << 12;
constexpr u32 ORB_MAX_SAMPLES = 1 << 10;
constexpr u32 ORB_MAX_SONGS = 1 << 10;
constexpr u32 ORB_MAX_LEVELS = 1 << 10;
constexpr u32 ORB_MAX_FONTS = 1 << 8;
constexpr u32 ORB_MAX_TYPES = 1 << 10;

// What the caster refuses and the loader checks again, since a file is untrusted.
constexpr f32 ORB_MAX_BPM = 1000;
constexpr u32 ORB_MAX_RATE = 192000; // Hz
constexpr u32 ORB_MAX_LAYERS = 1 << 12;
constexpr u32 ORB_MAX_SUBLAYERS = 8;
constexpr u32 ORB_MAX_TILE_ID = 16382;
// A font covers codepoints 32 through 126; the range is fixed, so no font stores it.
constexpr u8 ORB_FONT_FIRST = 32;
constexpr u32 ORB_FONT_GLYPHS = 95;
constexpr u32 ORB_MAX_CELL = 254; // a cell any wider overflows advance, which is width + 1
constexpr u32 ORB_MAX_FIELDS = 1 << 20;
constexpr u32 ORB_MAX_PLACEMENTS = 1 << 16;

// A tile is 16 bits: zero is empty, otherwise the low fourteen bits are the tile id
// plus one and the top two bits are the flip flags.
constexpr u16 ORB_TILE_FLIP_X = 1 << 14;
constexpr u16 ORB_TILE_FLIP_Y = 1 << 15;
constexpr u16 ORB_TILE_ID_MASK = 0x3fff;

typedef struct orb_file_header {
    u32 magic, version, section_count, pad;
} orb_file_header;

static_assert(sizeof(orb_file_header) == 16, "orb_file_header layout");

typedef struct orb_section {
    u32 tag, offset, size, pad;
} orb_section;

static_assert(sizeof(orb_section) == 16, "orb_section layout");

typedef struct orb_info_desc {
    u16 width, height;
    char name[60];
} orb_info_desc;

static_assert(sizeof(orb_info_desc) == 64, "orb_info_desc layout");

typedef struct orb_sheet_desc {
    u16 width, height;
    u32 pixels;
} orb_sheet_desc;

static_assert(sizeof(orb_sheet_desc) == 8, "orb_sheet_desc layout");

typedef struct orb_sprite_desc {
    u16 sheet, x, y, width, height;
    i16 ox, oy;
    u16 frame_width, frame_height, pad;
} orb_sprite_desc;

static_assert(sizeof(orb_sprite_desc) == 20, "orb_sprite_desc layout");

typedef struct orb_anim_desc {
    u32 first_sprite, first_duration;
    u16 count;
    u8 pad[6];
} orb_anim_desc;

static_assert(sizeof(orb_anim_desc) == 16, "orb_anim_desc layout");

typedef struct orb_sample_desc {
    u32 first;      // int16 element offset into the PCM section
    u32 count;      // frames; the sample spans count * channels elements
    u32 loop_start; // frames; loop_end 0 means no loop
    u32 loop_end;   // exclusive
    u32 rate;       // Hz
    u8 channels;    // 1 or 2; PCM is interleaved
    u8 pad[3];
} orb_sample_desc;

static_assert(sizeof(orb_sample_desc) == 24, "orb_sample_desc layout");

typedef struct orb_song_desc {
    u32 sample;
    u32 millibpm; // the manifest's bpm * 1000
    u32 pad[2];
} orb_song_desc;

static_assert(sizeof(orb_song_desc) == 16, "orb_song_desc layout");

typedef struct orb_tileset_desc {
    u16 sheet;
    u16 grid, spacing, padding; // pixels
    u16 columns, count;
    u8 pad[4];
} orb_tileset_desc; // 16 bytes

static_assert(sizeof(orb_tileset_desc) == 16, "orb_tileset_desc layout");

typedef struct orb_level_desc {
    i32 world_x, world_y, pad;
    u16 width, height; // pixels
    u16 first_layer, layer_count;
    u16 first_neighbor, neighbor_count;
    u32 first_placement, placement_count;
} orb_level_desc; // 32 bytes

static_assert(sizeof(orb_level_desc) == 32, "orb_level_desc layout");

typedef struct orb_layer_desc {
    u32 tiles; // element offset into the tiles section
    u32 cells; // byte offset into the cells section, or ORB_NO_INDEX
    f32 parallax_x, parallax_y;
    u16 tileset; // unused when sublayers is 0
    u16 grid;    // cell size in pixels
    u16 columns, rows;
    i16 offset_x, offset_y;
    u8 sublayers; // 0 when the layer has no tiles
    u8 pad[3];
} orb_layer_desc; // 32 bytes

static_assert(sizeof(orb_layer_desc) == 32, "orb_layer_desc layout");

typedef struct orb_neighbor_desc {
    u16 level;
    u8 dir; // orb_level_dir
    u8 pad;
} orb_neighbor_desc; // 4 bytes

static_assert(sizeof(orb_neighbor_desc) == 4, "orb_neighbor_desc layout");

typedef struct orb_font_desc {
    u16 sheet;
    u16 first_glyph; // element offset into the glyphs section
    u16 line_height; // pixels, the cell height
    u8 pad[10];
} orb_font_desc; // 16 bytes

static_assert(sizeof(orb_font_desc) == 16, "orb_font_desc layout");

typedef struct orb_glyph_desc {
    u16 x, y;          // in the sheet
    u8 width, advance; // pixels
    u8 pad[2];
} orb_glyph_desc; // 8 bytes

static_assert(sizeof(orb_glyph_desc) == 8, "orb_glyph_desc layout");

typedef struct orb_type_desc {
    u16 width, height; // pixels
    u32 first_field, field_count;
    u8 pad[4];
} orb_type_desc; // 16 bytes

static_assert(sizeof(orb_type_desc) == 16, "orb_type_desc layout");

typedef struct orb_placement_desc {
    u64 iid; // orb_asset_id of the instance's iid
    u16 type, level;
    i32 x, y; // world pixels
    u16 width, height;
    u32 first_field, field_count;
} orb_placement_desc; // 32 bytes

static_assert(sizeof(orb_placement_desc) == 32, "orb_placement_desc layout");

typedef enum orb_field_kind : u8 {
    ORB_FIELD_INT,    // i32
    ORB_FIELD_FLOAT,  // f32
    ORB_FIELD_BOOL,   // u8
    ORB_FIELD_STRING, // u32 offset of a NUL-terminated string in field data
    ORB_FIELD_POINT,  // two i32, world pixels
    ORB_FIELD_REF     // u32 placement index
} orb_field_kind;

typedef struct orb_field_desc {
    u64 name; // orb_asset_id of the folded field name
    u32 data; // byte offset into the field data section
    u16 count;
    u8 kind; // orb_field_kind
    u8 pad;
} orb_field_desc; // 16 bytes

static_assert(sizeof(orb_field_desc) == 16, "orb_field_desc layout");

// Bytes per element of a field kind.
static inline u32 orb_field_width(orb_field_kind kind) {
    return kind == ORB_FIELD_BOOL ? 1 : kind == ORB_FIELD_POINT ? 8 : 4;
}

u8_span orb_file_write(arena* out, const orb_assets* in);
[[nodiscard]] bool orb_file_load(u8_span file, orb_assets* out, orb_error* err);

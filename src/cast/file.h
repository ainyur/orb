#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

typedef struct orb_assets orb_assets;

constexpr uint32_t ORB_FILE_MAGIC = 0x0042524F;
constexpr uint32_t ORB_FILE_VERSION = 1;

enum {
    ORB_SEC_PALETTE = 1,
    ORB_SEC_SHEETS,
    ORB_SEC_PIXELS,
    ORB_SEC_SPRITES,
    ORB_SEC_ANIMATIONS,
    ORB_SEC_DURATIONS,
    ORB_SEC_SPRITE_IDS,
    ORB_SEC_ANIMATION_IDS,
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
    ORB_SEC_BINDINGS,
    ORB_SEC_COUNT_
};

// Handle indices are 24 bits; these bound the runtime's per-index generation tables.
constexpr uint32_t ORB_MAX_SPRITES = 1 << 14;
constexpr uint32_t ORB_MAX_ANIMATIONS = 1 << 12;
constexpr uint32_t ORB_MAX_SAMPLES = 1 << 10;
constexpr uint32_t ORB_MAX_SONGS = 1 << 10;
constexpr uint32_t ORB_MAX_LEVELS = 1 << 10;
constexpr uint32_t ORB_MAX_FONTS = 1 << 8;

// What the caster refuses and the loader checks again, since a file is untrusted.
constexpr uint32_t ORB_MAX_RATE = 192000; // Hz
constexpr float ORB_MAX_BPM = 1000;
constexpr uint32_t ORB_MAX_LAYERS = 1 << 12;
constexpr uint32_t ORB_MAX_SUBLAYERS = 8;
constexpr uint32_t ORB_MAX_TILE_ID = 16382;
// A font covers codepoints 32 through 126; the range is fixed, so no font stores it.
constexpr uint8_t ORB_FONT_FIRST = 32;
constexpr uint32_t ORB_FONT_GLYPHS = 95;
constexpr uint32_t ORB_MAX_CELL = 254; // a cell any wider overflows advance, which is width + 1

// A tile is 16 bits: zero is empty, otherwise the low fourteen bits are the tile id
// plus one and the top two bits are the flip flags.
constexpr uint16_t ORB_TILE_FLIP_X = 1 << 14;
constexpr uint16_t ORB_TILE_FLIP_Y = 1 << 15;
constexpr uint16_t ORB_TILE_ID_MASK = 0x3fff;

typedef struct orb_animation_desc {
    uint32_t first_sprite, first_duration;
    uint16_t count;
    uint8_t pad[6];
} orb_animation_desc;

typedef struct orb_binding_desc {
    orb_key_symbol symbol; // UTF-8, NUL-terminated
} orb_binding_desc;        // 24 bytes

typedef struct orb_file_header {
    uint32_t magic, version, section_count, pad;
} orb_file_header;

typedef struct orb_font_desc {
    uint16_t sheet;
    uint16_t first_glyph; // element offset into the glyphs section
    uint16_t line_height; // pixels, the cell height
    uint8_t pad[10];
} orb_font_desc; // 16 bytes

typedef struct orb_glyph_desc {
    uint16_t x, y;          // in the sheet
    uint8_t width, advance; // pixels
    uint8_t pad[2];
} orb_glyph_desc; // 8 bytes

typedef struct orb_info_desc {
    uint16_t width, height;
    char name[60];
} orb_info_desc;

typedef struct orb_layer_desc {
    uint32_t tiles; // element offset into the tiles section
    uint32_t cells; // byte offset into the cells section, or ORB_NO_INDEX
    float parallax_x, parallax_y;
    uint16_t tileset; // unused when sublayers is 0
    uint16_t grid;    // cell size in pixels
    uint16_t columns, rows;
    int16_t offset_x, offset_y;
    uint8_t sublayers; // 0 when the layer has no tiles
    uint8_t pad[3];
} orb_layer_desc; // 32 bytes

typedef struct orb_level_desc {
    int32_t world_x, world_y, depth;
    uint16_t width, height; // pixels
    uint16_t first_layer, layer_count;
    uint16_t first_neighbor, neighbor_count;
    uint8_t pad[8];
} orb_level_desc; // 32 bytes

typedef struct orb_neighbor_desc {
    uint16_t level;
    uint8_t dir; // orb_neighbor_dir
    uint8_t pad;
} orb_neighbor_desc; // 4 bytes

typedef struct orb_sample_desc {
    uint32_t first;      // int16 element offset into the PCM section
    uint32_t count;      // frames; the sample spans count * channels elements
    uint32_t loop_start; // frames; loop_end 0 means no loop
    uint32_t loop_end;   // exclusive
    uint32_t rate;       // Hz
    uint8_t channels;    // 1 or 2; PCM is interleaved
    uint8_t pad[3];
} orb_sample_desc;

typedef struct orb_section {
    uint32_t tag, offset, size, pad;
} orb_section;

typedef struct orb_sheet_desc {
    uint16_t width, height;
    uint32_t pixels;
} orb_sheet_desc;

typedef struct orb_song_desc {
    uint32_t sample;
    float bpm; // from the manifest; beats = seconds * bpm / 60
    uint32_t pad[2];
} orb_song_desc;

typedef struct orb_sprite_desc {
    uint16_t sheet, x, y, width, height;
    int16_t ox, oy;
    uint16_t frame_width, frame_height, pad;
} orb_sprite_desc;

typedef struct orb_tileset_desc {
    uint16_t sheet;
    uint16_t grid, spacing, padding; // pixels
    uint16_t columns, count;
    uint8_t pad[4];
} orb_tileset_desc; // 16 bytes

static_assert(sizeof(orb_animation_desc) == 16, "orb_animation_desc layout");
static_assert(sizeof(orb_binding_desc) == 24, "orb_binding_desc layout");
static_assert(sizeof(orb_file_header) == 16, "orb_file_header layout");
static_assert(sizeof(orb_font_desc) == 16, "orb_font_desc layout");
static_assert(sizeof(orb_glyph_desc) == 8, "orb_glyph_desc layout");
static_assert(sizeof(orb_info_desc) == 64, "orb_info_desc layout");
static_assert(sizeof(orb_layer_desc) == 32, "orb_layer_desc layout");
static_assert(sizeof(orb_level_desc) == 32, "orb_level_desc layout");
static_assert(sizeof(orb_neighbor_desc) == 4, "orb_neighbor_desc layout");
static_assert(sizeof(orb_sample_desc) == 24, "orb_sample_desc layout");
static_assert(sizeof(orb_section) == 16, "orb_section layout");
static_assert(sizeof(orb_sheet_desc) == 8, "orb_sheet_desc layout");
static_assert(sizeof(orb_song_desc) == 16, "orb_song_desc layout");
static_assert(sizeof(orb_sprite_desc) == 20, "orb_sprite_desc layout");
static_assert(sizeof(orb_tileset_desc) == 16, "orb_tileset_desc layout");

bool orb_file_load(orb_span file, orb_assets* out, orb_error* err);
orb_span orb_file_write(orb_arena* a, const orb_assets* in);

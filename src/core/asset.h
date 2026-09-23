#pragma once

#include "../cast/file.h"
#include "../orb.h"

struct orb_assets {
    const orb_info_desc* info;
    const uint8_t* pal;
    const orb_sheet_desc* sheets;
    uint32_t sheet_count;
    const uint8_t* pixels;
    uint32_t pixel_count;
    const orb_sprite_desc* sprites;
    uint32_t sprite_count;
    const orb_anim_desc* anims;
    uint32_t anim_count;
    const uint16_t* durations;
    uint32_t duration_count;
    const uint64_t* sprite_ids;
    const uint64_t* anim_ids;
    const orb_sample_desc* samples;
    uint32_t sample_count;
    const int16_t* pcm;
    uint32_t pcm_count; // int16 elements
    const orb_song_desc* songs;
    uint32_t song_count;
    const uint64_t* sample_ids;
    const uint64_t* song_ids;
    const orb_tileset_desc* tilesets;
    uint32_t tileset_count;
    const orb_level_desc* levels;
    uint32_t level_count;
    const orb_layer_desc* layers;
    uint32_t layer_count;
    const orb_neighbor_desc* neighbors;
    uint32_t neighbor_count;
    const uint16_t* tiles;
    uint32_t tile_count;
    const uint8_t* cells;
    uint32_t cell_count;
    const uint64_t* level_ids;
    const uint64_t* layer_ids;
    const orb_font_desc* fonts;
    uint32_t font_count;
    const orb_glyph_desc* glyphs;
    uint32_t glyph_count;
    const uint64_t* font_ids;
    const orb_binding_desc* bindings;
    uint32_t binding_count; // 0 or ORB_BTN_COUNT
    const orb_type_desc* types;
    uint32_t type_count;
    const orb_placement_desc* placements;
    uint32_t placement_count;
    const orb_field_desc* fields;
    uint32_t field_count;
    const uint8_t* field_data;
    uint32_t field_data_count; // bytes
    const uint64_t* type_ids;
    const uint8_t* sprite_gens;
    const uint8_t* anim_gens;
    const uint8_t* sample_gens;
    const uint8_t* song_gens;
    const uint8_t* level_gens;
    const uint8_t* font_gens;
    const uint8_t* type_gens;
};

typedef enum orb_asset_kind {
    ORB_ASSET_SPRITE,
    ORB_ASSET_ANIM,
    ORB_ASSET_SAMPLE,
    ORB_ASSET_SONG,
    ORB_ASSET_LEVEL,
    ORB_ASSET_FONT,
    ORB_ASSET_TYPE,
    ORB_ASSET_KIND_COUNT
} orb_asset_kind;

typedef struct orb_asset_table {
    orb_assets assets;
    uint8_t gens
        [ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS +
         ORB_MAX_FONTS + ORB_MAX_TYPES];
} orb_asset_table;

// A name's id, case-insensitive: cast stores it, find hashes the request the same way.
uint64_t orb_asset_id(const char* stem, const char* suffix);

// The handle whose id matches, or ORB_NO_INDEX.
uint32_t orb_asset_find(const orb_asset_table* table, orb_asset_kind kind, uint64_t id);

// A handle's index, or ORB_NO_INDEX when it is stale.
static inline uint32_t orb_asset_index(const uint8_t* gens, uint32_t count, uint32_t value) {
    uint32_t index = value & ORB_NO_INDEX, gen = value >> 24;

    if (index >= count || (gens ? gens[index] : 0) != gen) return ORB_NO_INDEX;

    return index;
}

#define orb_asset_index_of(assets, handle)                                                         \
    _Generic(                                                                                      \
        (handle),                                                                                  \
        orb_sprite: orb_asset_index((assets)->sprite_gens, (assets)->sprite_count, (handle).v),    \
        orb_anim: orb_asset_index((assets)->anim_gens, (assets)->anim_count, (handle).v),          \
        orb_sample: orb_asset_index((assets)->sample_gens, (assets)->sample_count, (handle).v),    \
        orb_song: orb_asset_index((assets)->song_gens, (assets)->song_count, (handle).v),          \
        orb_level: orb_asset_index((assets)->level_gens, (assets)->level_count, (handle).v),       \
        orb_font: orb_asset_index((assets)->font_gens, (assets)->font_count, (handle).v),          \
        orb_type: orb_asset_index((assets)->type_gens, (assets)->type_count, (handle).v)           \
    )

void orb_asset_set(orb_asset_table* table, const orb_assets* assets);

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
    const uint8_t* sprite_gens;
    const uint8_t* anim_gens;
    const uint8_t* sample_gens;
    const uint8_t* song_gens;
    const uint8_t* level_gens;
    const uint8_t* font_gens;
};

typedef enum orb_asset_kind {
    ORB_ASSET_SPRITE,
    ORB_ASSET_ANIM,
    ORB_ASSET_SAMPLE,
    ORB_ASSET_SONG,
    ORB_ASSET_LEVEL,
    ORB_ASSET_FONT,
    ORB_ASSET_KIND_COUNT
} orb_asset_kind;

typedef struct orb_asset_table {
    orb_assets assets;
    uint8_t gens
        [ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS +
         ORB_MAX_FONTS];
} orb_asset_table;

// A name's id, case-insensitive: cast stores it, find hashes the request the same way.
uint64_t orb_asset_id(const char* stem, const char* suffix);

// The handle whose id matches, or ORB_NO_INDEX.
uint32_t orb_asset_find(const orb_asset_table* t, orb_asset_kind kind, uint64_t id);

// A handle's index, or ORB_NO_INDEX when it is stale.
static inline uint32_t orb_asset_index(const uint8_t* gens, uint32_t count, uint32_t v) {
    uint32_t index = v & ORB_NO_INDEX, gen = v >> 24;

    if (index >= count || (gens ? gens[index] : 0) != gen) return ORB_NO_INDEX;

    return index;
}

#define orb_asset_index_of(as, h)                                                                  \
    _Generic(                                                                                      \
        (h),                                                                                       \
        orb_sprite: orb_asset_index((as)->sprite_gens, (as)->sprite_count, (h).v),                 \
        orb_anim: orb_asset_index((as)->anim_gens, (as)->anim_count, (h).v),                       \
        orb_sample: orb_asset_index((as)->sample_gens, (as)->sample_count, (h).v),                 \
        orb_song: orb_asset_index((as)->song_gens, (as)->song_count, (h).v),                       \
        orb_level: orb_asset_index((as)->level_gens, (as)->level_count, (h).v),                    \
        orb_font: orb_asset_index((as)->font_gens, (as)->font_count, (h).v)                        \
    )

void orb_asset_set(orb_asset_table* t, const orb_assets* assets);

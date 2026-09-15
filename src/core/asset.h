#pragma once

#include "../cast/file.h"
#include "../orb.h"

struct orb_assets {
    const orb_info_desc* info;
    const uint8_t* palette;
    const orb_sheet_desc* sheets;
    uint32_t sheet_count;
    const uint8_t* pixels;
    uint32_t pixel_count;
    const orb_sprite_desc* sprites;
    uint32_t sprite_count;
    const orb_animation_desc* animations;
    uint32_t animation_count;
    const uint16_t* durations;
    uint32_t duration_count;
    const uint64_t* sprite_ids;
    const uint64_t* animation_ids;
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
    const uint8_t* sprite_generations;
    const uint8_t* animation_generations;
    const uint8_t* sample_generations;
    const uint8_t* song_generations;
    const uint8_t* level_generations;
};

enum {
    ORB_ASSET_SPRITE,
    ORB_ASSET_ANIMATION,
    ORB_ASSET_SAMPLE,
    ORB_ASSET_SONG,
    ORB_ASSET_LEVEL,
    ORB_ASSET_KIND_COUNT
};

typedef struct orb_asset_table {
    orb_assets assets;
    uint8_t generations
        [ORB_MAX_SPRITES + ORB_MAX_ANIMATIONS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS];
} orb_asset_table;

// The handle whose id matches, or ORB_NO_INDEX.
uint32_t orb_asset_find(const orb_asset_table* t, int kind, uint64_t id);

// A name's id, case-insensitive: cast stores it, find hashes the request the same way.
uint64_t orb_asset_id(const char* stem, const char* suffix);

// A handle's index, or ORB_NO_INDEX when it is stale.
static inline uint32_t orb_asset_index(const uint8_t* generations, uint32_t count, uint32_t v) {
    uint32_t index = v & ORB_NO_INDEX, generation = v >> 24;

    if (index >= count || (generations ? generations[index] : 0) != generation) return ORB_NO_INDEX;

    return index;
}

#define orb_asset_index_of(as, h)                                                                  \
    _Generic(                                                                                      \
        (h),                                                                                       \
        orb_sprite: orb_asset_index((as)->sprite_generations, (as)->sprite_count, (h).v),          \
        orb_animation: orb_asset_index((as)->animation_generations, (as)->animation_count, (h).v), \
        orb_sample: orb_asset_index((as)->sample_generations, (as)->sample_count, (h).v),          \
        orb_song: orb_asset_index((as)->song_generations, (as)->song_count, (h).v),                \
        orb_level: orb_asset_index((as)->level_generations, (as)->level_count, (h).v)              \
    )

void orb_asset_set(orb_asset_table* t, const orb_assets* assets);

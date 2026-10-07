#pragma once

#include "../cast/file.h"
#include "../orb.h"

orb_span(u8);
orb_span(u16);
orb_span(orb_sheet_desc);
orb_span(orb_sprite_desc);
orb_span(orb_anim_desc);
orb_span(orb_sample_desc);
orb_span(i16);
orb_span(orb_song_desc);
orb_span(orb_tileset_desc);
orb_span(orb_level_desc);
orb_span(orb_layer_desc);
orb_span(orb_neighbor_desc);
orb_span(orb_font_desc);
orb_span(orb_glyph_desc);
orb_span(orb_type_desc);
orb_span(orb_placement_desc);
orb_span(orb_field_desc);

struct orb_assets {
    const orb_info_desc* info;
    const u8* pal;
    orb_sheet_desc_span sheets;
    u8_span pixels;
    orb_sprite_desc_span sprites;
    orb_anim_desc_span anims;
    u16_span durations;
    const u64* sprite_ids;
    const u64* anim_ids;
    orb_sample_desc_span samples;
    i16_span pcm;
    orb_song_desc_span songs;
    const u64* sample_ids;
    const u64* song_ids;
    orb_tileset_desc_span tilesets;
    orb_level_desc_span levels;
    orb_layer_desc_span layers;
    orb_neighbor_desc_span neighbors;
    u16_span tiles;
    u8_span cells;
    const u64* level_ids;
    const u64* layer_ids;
    orb_font_desc_span fonts;
    orb_glyph_desc_span glyphs;
    const u64* font_ids;
    orb_type_desc_span types;
    orb_placement_desc_span placements;
    orb_field_desc_span fields;
    u8_span field_data;
    const u64* type_ids;
    const u8* sprite_gens;
    const u8* anim_gens;
    const u8* sample_gens;
    const u8* song_gens;
    const u8* level_gens;
    const u8* font_gens;
    const u8* type_gens;
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
    u8 gens
        [ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS +
         ORB_MAX_FONTS + ORB_MAX_TYPES];
} orb_asset_table;

// A name's id, case-insensitive: cast stores it, find hashes the request the same way.
u64 orb_asset_id(const char* stem, const char* suffix);

// The handle whose id matches, or ORB_NO_INDEX.
u32 orb_asset_find(const orb_asset_table* table, orb_asset_kind kind, u64 id);

// A handle's index, or ORB_NO_INDEX when it is stale.
static inline u32 orb_asset_index(const u8* gens, u32 count, u32 value) {
    u32 index = value & ORB_NO_INDEX, gen = value >> 24;

    if (index >= count || (gens ? gens[index] : 0) != gen) return ORB_NO_INDEX;

    return index;
}

#define orb_asset_index_of(assets, handle)                                                         \
    _Generic(                                                                                      \
        (handle),                                                                                  \
        orb_sprite: orb_asset_index((assets)->sprite_gens, (assets)->sprites.len, (handle).v),     \
        orb_anim: orb_asset_index((assets)->anim_gens, (assets)->anims.len, (handle).v),           \
        orb_sample: orb_asset_index((assets)->sample_gens, (assets)->samples.len, (handle).v),     \
        orb_song: orb_asset_index((assets)->song_gens, (assets)->songs.len, (handle).v),           \
        orb_level: orb_asset_index((assets)->level_gens, (assets)->levels.len, (handle).v),        \
        orb_font: orb_asset_index((assets)->font_gens, (assets)->fonts.len, (handle).v),           \
        orb_type: orb_asset_index((assets)->type_gens, (assets)->types.len, (handle).v)            \
    )

void orb_asset_set(orb_asset_table* table, const orb_assets* assets);

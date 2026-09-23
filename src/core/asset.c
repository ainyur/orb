#include "asset.h"
#include "macros.h"

#include <ctype.h>
#include <stddef.h>

typedef struct asset_kind {
    uint16_t ids, count, gens;
    uint32_t first;
} asset_kind;

#define ASSET_KIND(kind, first)                                                                    \
    {offsetof(orb_assets, kind##_ids), offsetof(orb_assets, kind##_count),                         \
     offsetof(orb_assets, kind##_gens), first}

static const asset_kind asset_kinds[ORB_ASSET_KIND_COUNT] = {
    ASSET_KIND(sprite, 0),
    ASSET_KIND(anim, ORB_MAX_SPRITES),
    ASSET_KIND(sample, ORB_MAX_SPRITES + ORB_MAX_ANIMS),
    ASSET_KIND(song, ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES),
    ASSET_KIND(level, ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS),
    ASSET_KIND(
        font,
        ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS
    ),
    ASSET_KIND(
        type,
        ORB_MAX_SPRITES + ORB_MAX_ANIMS + ORB_MAX_SAMPLES + ORB_MAX_SONGS + ORB_MAX_LEVELS +
            ORB_MAX_FONTS
    ),
};

// FNV-1a over STEM_SUFFIX with every non-alphanumeric folded to '_' and letters
// uppercased, so "player" + "walk" and "Player" + "WALK" are one id.
static uint64_t asset_hash(uint64_t hash, const char* text) {
    for (const unsigned char* c = (const unsigned char*)text; *c; c++) {
        unsigned char folded = isalnum(*c) ? (unsigned char)toupper(*c) : (unsigned char)'_';

        hash = (hash ^ folded) * 0x100000001b3u;
    }

    return hash;
}

static uint32_t asset_count_of(const orb_assets* assets, const asset_kind* layout) {
    return *(const uint32_t*)((const char*)assets + layout->count);
}

static const uint64_t* asset_ids_of(const orb_assets* assets, const asset_kind* layout) {
    return *(const uint64_t* const*)((const char*)assets + layout->ids);
}

uint64_t orb_asset_id(const char* stem, const char* suffix) {
    uint64_t hash = asset_hash(0xcbf29ce484222325u, stem);

    hash = (hash ^ (unsigned char)'_') * 0x100000001b3u;
    return asset_hash(hash, suffix);
}

uint32_t orb_asset_find(const orb_asset_table* table, orb_asset_kind kind, uint64_t id) {
    const asset_kind* layout = &asset_kinds[kind];
    const uint64_t* ids = asset_ids_of(&table->assets, layout);
    const uint8_t* gens = table->gens + layout->first;
    uint32_t count = asset_count_of(&table->assets, layout);

    for (uint32_t i = 0; i < count; i++)
        if (ids[i] == id) return i | (uint32_t)gens[i] << 24;

    return ORB_NO_INDEX;
}

// A recast that lands a different source at an index bumps that index, so a
// handle found before it fails closed until reload finds the name again.
void orb_asset_set(orb_asset_table* table, const orb_assets* assets) {
    for (int kind = 0; kind < ORB_ASSET_KIND_COUNT; kind++) {
        const asset_kind* layout = &asset_kinds[kind];
        const uint64_t *old = asset_ids_of(&table->assets, layout),
                       *new = asset_ids_of(assets, layout);
        uint32_t n =
            orb_min(asset_count_of(&table->assets, layout), asset_count_of(assets, layout));

        for (uint32_t i = 0; old && new && i < n; i++)
            if (old[i] != new[i]) table->gens[layout->first + i]++;
    }

    table->assets = *assets;

    for (int kind = 0; kind < ORB_ASSET_KIND_COUNT; kind++)
        *(const uint8_t**)((char*)&table->assets + asset_kinds[kind].gens) =
            table->gens + asset_kinds[kind].first;
}

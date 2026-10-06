#include "asset.h"
#include "../orb_math.h"

#include <ctype.h>
#include <stddef.h>

typedef struct asset_kind {
    u16 ids, items, gens; // offsetof the ids pointer, the span, and the gens pointer
    u32 first;
} asset_kind;

#define ASSET_KIND(kind, first)                                                                    \
    {offsetof(orb_assets, kind##_ids), offsetof(orb_assets, kind##s),                              \
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
static u64 asset_hash(u64 hash, const char* text) {
    for (const unsigned char* c = (const unsigned char*)text; *c; c++) {
        unsigned char folded = isalnum(*c) ? (unsigned char)toupper(*c) : (unsigned char)'_';

        hash = (hash ^ folded) * 0x100000001b3u;
    }

    return hash;
}

static u32 asset_count_of(const orb_assets* assets, const asset_kind* layout) {
    return *(const u32*)((const char*)assets + layout->items + offsetof(u8_span, len));
}

static const u64* asset_ids_of(const orb_assets* assets, const asset_kind* layout) {
    return *(const u64* const*)((const char*)assets + layout->ids);
}

u64 orb_asset_id(const char* stem, const char* suffix) {
    u64 hash = asset_hash(0xcbf29ce484222325u, stem);

    hash = (hash ^ (unsigned char)'_') * 0x100000001b3u;
    return asset_hash(hash, suffix);
}

u32 orb_asset_find(const orb_asset_table* table, orb_asset_kind kind, u64 id) {
    const asset_kind* layout = &asset_kinds[kind];
    const u64* ids = asset_ids_of(&table->assets, layout);
    const u8* gens = table->gens + layout->first;
    u32 count = asset_count_of(&table->assets, layout);

    for (u32 i = 0; i < count; i++)
        if (ids[i] == id) return i | (u32)gens[i] << 24;

    return ORB_NO_INDEX;
}

// A recast that lands a different source at an index bumps that index, so a
// handle found before it fails closed until reload finds the name again.
void orb_asset_set(orb_asset_table* table, const orb_assets* assets) {
    for (int kind = 0; kind < ORB_ASSET_KIND_COUNT; kind++) {
        const asset_kind* layout = &asset_kinds[kind];
        const u64 *old = asset_ids_of(&table->assets, layout), *new = asset_ids_of(assets, layout);
        u32 n = orb_min(asset_count_of(&table->assets, layout), asset_count_of(assets, layout));

        for (u32 i = 0; old && new && i < n; i++)
            if (old[i] != new[i]) table->gens[layout->first + i]++;
    }

    table->assets = *assets;

    for (int kind = 0; kind < ORB_ASSET_KIND_COUNT; kind++)
        *(const u8**)((char*)&table->assets + asset_kinds[kind].gens) =
            table->gens + asset_kinds[kind].first;
}

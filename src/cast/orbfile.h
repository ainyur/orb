#pragma once
#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

#include <assert.h>
#include <stdbool.h>

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
    ORB_SEC_COUNT_ = 8
};

// Handle indices are 24 bits; these bound the runtime's per-index generation tables.
constexpr uint32_t ORB_MAX_SPRITES = 1 << 14;
constexpr uint32_t ORB_MAX_ANIMATIONS = 1 << 12;

// The id of an asset: a hash of its file stem and frame number or tag name,
// case-insensitive. Cast stores one per entry; find hashes the request the
// same way and scans for it.
uint64_t orb_asset_id(const char* stem, const char* suffix);

typedef struct orb_file_header {
    uint32_t magic, version, section_count, pad;
} orb_file_header;

typedef struct orb_section {
    uint32_t tag, offset, size, pad;
} orb_section;

typedef struct orb_sheet_desc {
    uint16_t w, h;
    uint32_t pixels;
} orb_sheet_desc;

typedef struct orb_sprite_desc {
    uint16_t sheet, x, y, w, h;
    int16_t ox, oy;
    uint16_t fw, fh, pad;
} orb_sprite_desc;

typedef struct orb_animation_desc {
    uint32_t first_sprite, first_duration;
    uint16_t count;
    uint8_t direction, pad[5];
} orb_animation_desc;

static_assert(sizeof(orb_file_header) == 16, "orb_file_header layout");
static_assert(sizeof(orb_section) == 16, "orb_section layout");
static_assert(sizeof(orb_sheet_desc) == 8, "orb_sheet_desc layout");
static_assert(sizeof(orb_sprite_desc) == 20, "orb_sprite_desc layout");
static_assert(sizeof(orb_animation_desc) == 16, "orb_animation_desc layout");

typedef struct orb_assets {
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
    const uint64_t* sprite_ids;    // one per sprite: a hash of the name it was cast from
    const uint64_t* animation_ids; // one per animation
    // Runtime only, never in a file: the generation a handle must carry to be
    // valid at each index. nullptr means every index is at generation 0.
    const uint8_t* sprite_generations;
    const uint8_t* animation_generations;
} orb_assets;

bool orb_file_load(orb_span file, orb_assets* out, orb_error* err);
orb_span orb_file_write(orb_arena* a, const orb_assets* in);

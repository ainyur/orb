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
    ORB_SEC_COUNT_ = ORB_SEC_INFO
};

// Handle indices are 24 bits; these bound the runtime's per-index generation tables.
constexpr uint32_t ORB_MAX_SPRITES = 1 << 14;
constexpr uint32_t ORB_MAX_ANIMATIONS = 1 << 12;
constexpr uint32_t ORB_MAX_SAMPLES = 1 << 10;
constexpr uint32_t ORB_MAX_SONGS = 1 << 10;

// What the caster refuses and the loader checks again, since a file is untrusted.
constexpr uint32_t ORB_MAX_RATE = 192000; // Hz
constexpr float ORB_MAX_BPM = 1000;

typedef struct orb_animation_desc {
    uint32_t first_sprite, first_duration;
    uint16_t count;
    uint8_t pad[6];
} orb_animation_desc;

typedef struct orb_file_header {
    uint32_t magic, version, section_count, pad;
} orb_file_header;

typedef struct orb_info_desc {
    uint16_t w, h;
    char name[60];
} orb_info_desc;

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
    uint16_t w, h;
    uint32_t pixels;
} orb_sheet_desc;

typedef struct orb_song_desc {
    uint32_t sample;
    float bpm; // from the manifest; beats = seconds * bpm / 60
    uint32_t pad[2];
} orb_song_desc;

typedef struct orb_sprite_desc {
    uint16_t sheet, x, y, w, h;
    int16_t ox, oy;
    uint16_t fw, fh, pad;
} orb_sprite_desc;

static_assert(sizeof(orb_animation_desc) == 16, "orb_animation_desc layout");
static_assert(sizeof(orb_file_header) == 16, "orb_file_header layout");
static_assert(sizeof(orb_info_desc) == 64, "orb_info_desc layout");
static_assert(sizeof(orb_sample_desc) == 24, "orb_sample_desc layout");
static_assert(sizeof(orb_section) == 16, "orb_section layout");
static_assert(sizeof(orb_sheet_desc) == 8, "orb_sheet_desc layout");
static_assert(sizeof(orb_song_desc) == 16, "orb_song_desc layout");
static_assert(sizeof(orb_sprite_desc) == 20, "orb_sprite_desc layout");

bool orb_file_load(orb_span file, orb_assets* out, orb_error* err);
orb_span orb_file_write(orb_arena* a, const orb_assets* in);

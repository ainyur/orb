#include "file.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// FNV-1a over STEM_SUFFIX with every non-alphanumeric folded to '_' and letters
// uppercased, so "player" + "walk" and "Player" + "WALK" are one id.
static uint64_t asset_hash(uint64_t h, const char* s) {
    for (const unsigned char* c = (const unsigned char*)s; *c; c++) {
        unsigned char x = isalnum(*c) ? (unsigned char)toupper(*c) : (unsigned char)'_';

        h = (h ^ x) * 0x100000001b3u;
    }

    return h;
}

uint64_t orb_asset_id(const char* stem, const char* suffix) {
    uint64_t h = asset_hash(0xcbf29ce484222325u, stem);

    h = (h ^ (unsigned char)'_') * 0x100000001b3u;
    return asset_hash(h, suffix);
}

// One row per section: where its bytes and count live in orb_assets. A row with
// no count field is one element. An id section holds one id per element of its
// desc section, so it must be at least that long rather than setting a count.
typedef struct file_row {
    uint8_t tag;
    bool ids;
    uint16_t elem;
    uint16_t ptr, count; // offsetof; FILE_NO_COUNT for a single element
    uint32_t max;        // elements, 0 for no bound
    const char* name;
} file_row;

constexpr uint16_t FILE_NO_COUNT = 0xffff;

// FILE_ONE: a single struct. FILE_ROW: an array with its count field and bound.
// FILE_IDS: one id per element of the section whose count field is named.
#define FILE_ENTRY(sec, ids, T, ptr, count, max)                                                   \
    {ORB_SEC_##sec, ids, sizeof(T), offsetof(orb_assets, ptr), count, max, #sec}
#define FILE_ONE(sec, T, ptr) FILE_ENTRY(sec, false, T, ptr, FILE_NO_COUNT, 0)
#define FILE_ROW(sec, T, ptr, count, max)                                                          \
    FILE_ENTRY(sec, false, T, ptr, offsetof(orb_assets, count), max)
#define FILE_IDS(sec, ptr, count)                                                                  \
    FILE_ENTRY(sec, true, uint64_t, ptr, offsetof(orb_assets, count), 0)

static const file_row file_rows[] = {
    FILE_ONE(INFO, orb_info_desc, info),
    FILE_ONE(PALETTE, uint8_t[256 * 4], palette),
    FILE_ROW(SHEETS, orb_sheet_desc, sheets, sheet_count, 0),
    FILE_ROW(PIXELS, uint8_t, pixels, pixel_count, 0),
    FILE_ROW(SPRITES, orb_sprite_desc, sprites, sprite_count, ORB_MAX_SPRITES),
    FILE_ROW(ANIMATIONS, orb_animation_desc, animations, animation_count, ORB_MAX_ANIMATIONS),
    FILE_ROW(DURATIONS, uint16_t, durations, duration_count, 0),
    FILE_IDS(SPRITE_IDS, sprite_ids, sprite_count),
    FILE_IDS(ANIMATION_IDS, animation_ids, animation_count),
    FILE_ROW(SAMPLES, orb_sample_desc, samples, sample_count, ORB_MAX_SAMPLES),
    FILE_ROW(PCM, int16_t, pcm, pcm_count, 0),
    FILE_IDS(SAMPLE_IDS, sample_ids, sample_count),
    FILE_ROW(SONGS, orb_song_desc, songs, song_count, ORB_MAX_SONGS),
    FILE_IDS(SONG_IDS, song_ids, song_count),
};

constexpr uint32_t FILE_ROW_COUNT = sizeof file_rows / sizeof *file_rows;

static_assert(FILE_ROW_COUNT == ORB_SEC_COUNT_, "every section tag has a row");

static const void** file_ptr(const orb_assets* as, const file_row* row) {
    return (const void**)((char*)as + row->ptr);
}

static uint32_t* file_count(const orb_assets* as, const file_row* row) {
    return (uint32_t*)((char*)as + row->count);
}

static const file_row* file_row_for(uint32_t tag) {
    for (uint32_t i = 0; i < FILE_ROW_COUNT; i++)
        if (file_rows[i].tag == tag) return &file_rows[i];

    return nullptr;
}

orb_span orb_file_write(orb_arena* a, const orb_assets* in) {
    orb_file_header* header = orb_arena_push(a, sizeof *header, 16);
    uint8_t* base = (uint8_t*)header;

    header->magic = ORB_FILE_MAGIC;
    header->version = ORB_FILE_VERSION;
    header->section_count = FILE_ROW_COUNT;

    orb_section* table = orb_arena_push(a, sizeof(orb_section) * FILE_ROW_COUNT, 16);

    for (uint32_t i = 0; i < FILE_ROW_COUNT; i++) {
        const file_row* row = &file_rows[i];
        uint32_t n = row->count == FILE_NO_COUNT ? 1 : *file_count(in, row);
        size_t size = (size_t)row->elem * n;
        uint8_t* dst = orb_arena_push(a, size, 16);

        if (size) memcpy(dst, *file_ptr(in, row), size);

        table[i] = (orb_section) {
            .tag = row->tag, .offset = (uint32_t)(dst - base), .size = (uint32_t)size
        };
    }

    return (orb_span) {base, (size_t)(a->base + a->used - base)};
}

bool orb_file_load(orb_span file, orb_assets* out, orb_error* err) {
    if (file.len < sizeof(orb_file_header)) return orb_error_set(err, "orb file: too short");

    const orb_file_header* header = (const orb_file_header*)file.ptr;

    if (header->magic != ORB_FILE_MAGIC) return orb_error_set(err, "orb file: bad magic");

    if (header->version != ORB_FILE_VERSION)
        return orb_error_set(
            err, "orb file: version %u, runtime expects %u", header->version, ORB_FILE_VERSION
        );

    if (sizeof(orb_file_header) + sizeof(orb_section) * header->section_count > file.len)
        return orb_error_set(err, "orb file: truncated section table");

    const orb_section* table = (const orb_section*)(file.ptr + sizeof(orb_file_header));
    uint32_t id_sizes[ORB_SEC_COUNT_ + 1] = {};

    memset(out, 0, sizeof *out);

    for (uint32_t i = 0; i < header->section_count; i++) {
        const orb_section* s = &table[i];
        const file_row* row = file_row_for(s->tag);

        if ((size_t)s->offset + s->size > file.len || (s->offset & 15) != 0)
            return orb_error_set(err, "orb file: section %u out of bounds or misaligned", s->tag);

        if (!row) continue; // unknown sections are skipped

        if (row->count == FILE_NO_COUNT && s->size < row->elem)
            return orb_error_set(err, "orb file: short %s section", row->name);

        *file_ptr(out, row) = file.ptr + s->offset;

        if (row->ids)
            id_sizes[row->tag] = s->size;
        else if (row->count != FILE_NO_COUNT)
            *file_count(out, row) = s->size / row->elem;
    }

    if (out->info && !memchr((const char*)out->info + 4, 0, sizeof(orb_info_desc) - 4))
        return orb_error_set(err, "orb file: bad info section");

    if (!out->info || !out->palette)
        return orb_error_set(err, "orb file: no info or palette section");

    for (uint32_t i = 0; i < FILE_ROW_COUNT; i++) {
        const file_row* row = &file_rows[i];
        uint32_t need = row->count == FILE_NO_COUNT ? 0 : *file_count(out, row);

        if (row->max && need > row->max)
            return orb_error_set(err, "orb file: %u %s, at most %u", need, row->name, row->max);

        if (row->ids && id_sizes[row->tag] < (uint64_t)need * row->elem)
            return orb_error_set(
                err, "orb file: %s section holds %u ids, need %u", row->name,
                id_sizes[row->tag] / row->elem, need
            );
    }

    for (uint32_t i = 0; i < out->sample_count; i++) {
        const orb_sample_desc* d = &out->samples[i];
        uint64_t end = (uint64_t)d->first + (uint64_t)d->count * d->channels;

        if (d->channels != 1 && d->channels != 2)
            return orb_error_set(
                err, "orb file: sample %u has %u channels, expected 1 or 2", i, d->channels
            );

        if (end > out->pcm_count)
            return orb_error_set(err, "orb file: sample %u runs past the PCM section", i);

        if (d->rate < 1 || d->rate > 192000)
            return orb_error_set(err, "orb file: sample %u has a bad rate %u", i, d->rate);

        if (d->loop_end != 0 && (d->loop_end > d->count || d->loop_start >= d->loop_end))
            return orb_error_set(
                err, "orb file: sample %u has a bad loop start %u end %u for %u frames", i,
                d->loop_start, d->loop_end, d->count
            );
    }

    for (uint32_t i = 0; i < out->song_count; i++) {
        if (out->songs[i].sample >= out->sample_count)
            return orb_error_set(
                err, "orb file: song %u names sample %u of %u", i, out->songs[i].sample,
                out->sample_count
            );

        if (!(out->songs[i].bpm > 0 && out->songs[i].bpm <= 1000)) { // also refuses NaN
            return orb_error_set(
                err, "orb file: song %u has a bad bpm %g", i, (double)out->songs[i].bpm
            );
        }
    }

    return true;
}

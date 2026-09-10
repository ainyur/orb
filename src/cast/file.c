#include "file.h"

#include <ctype.h>
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

static void
file_section(orb_arena* a, const uint8_t* base, orb_section* s, uint32_t tag, orb_span data) {
    uint8_t* dst = orb_arena_push(a, data.len, 16);

    if (data.len) memcpy(dst, data.ptr, data.len);
    s->tag = tag;
    s->offset = (uint32_t)(dst - base);
    s->size = (uint32_t)data.len;
}

orb_span orb_file_write(orb_arena* a, const orb_assets* in) {
    orb_file_header* header = orb_arena_push(a, sizeof *header, 16);
    uint8_t* base = (uint8_t*)header;

    header->magic = ORB_FILE_MAGIC;
    header->version = ORB_FILE_VERSION;
    header->section_count = ORB_SEC_COUNT_;

    orb_section* table = orb_arena_push(a, sizeof(orb_section) * ORB_SEC_COUNT_, 16);
    orb_section* s = table;

    file_section(
        a, base, s++, ORB_SEC_INFO, (orb_span) {(const uint8_t*)in->info, sizeof *in->info}
    );
    file_section(a, base, s++, ORB_SEC_PALETTE, (orb_span) {(const uint8_t*)in->palette, 256 * 4});
    file_section(
        a, base, s++, ORB_SEC_SHEETS,
        (orb_span) {(const uint8_t*)in->sheets, sizeof(orb_sheet_desc) * in->sheet_count}
    );
    file_section(
        a, base, s++, ORB_SEC_PIXELS, (orb_span) {(const uint8_t*)in->pixels, in->pixel_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SPRITES,
        (orb_span) {(const uint8_t*)in->sprites, sizeof(orb_sprite_desc) * in->sprite_count}
    );
    file_section(
        a, base, s++, ORB_SEC_ANIMATIONS,
        (orb_span) {
            (const uint8_t*)in->animations, sizeof(orb_animation_desc) * in->animation_count
        }
    );
    file_section(
        a, base, s++, ORB_SEC_DURATIONS,
        (orb_span) {(const uint8_t*)in->durations, sizeof(uint16_t) * in->duration_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SPRITE_IDS,
        (orb_span) {(const uint8_t*)in->sprite_ids, sizeof(uint64_t) * in->sprite_count}
    );
    file_section(
        a, base, s++, ORB_SEC_ANIMATION_IDS,
        (orb_span) {(const uint8_t*)in->animation_ids, sizeof(uint64_t) * in->animation_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SAMPLES,
        (orb_span) {(const uint8_t*)in->samples, sizeof(orb_sample_desc) * in->sample_count}
    );
    file_section(
        a, base, s++, ORB_SEC_PCM,
        (orb_span) {(const uint8_t*)in->pcm, sizeof(int16_t) * in->pcm_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SAMPLE_IDS,
        (orb_span) {(const uint8_t*)in->sample_ids, sizeof(uint64_t) * in->sample_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SONGS,
        (orb_span) {(const uint8_t*)in->songs, sizeof(orb_song_desc) * in->song_count}
    );
    file_section(
        a, base, s++, ORB_SEC_SONG_IDS,
        (orb_span) {(const uint8_t*)in->song_ids, sizeof(uint64_t) * in->song_count}
    );
    return (orb_span) {base, (size_t)(a->base + a->used - base)};
}

bool orb_file_load(orb_span file, orb_assets* out, orb_error* err) {
    if (file.len < sizeof(orb_file_header)) {
        orb_error_set(err, "orb file: too short");
        return false;
    }

    const orb_file_header* header = (const orb_file_header*)file.ptr;

    if (header->magic != ORB_FILE_MAGIC) {
        orb_error_set(err, "orb file: bad magic");
        return false;
    }

    if (header->version != ORB_FILE_VERSION) {
        orb_error_set(
            err, "orb file: version %u, runtime expects %u", header->version, ORB_FILE_VERSION
        );
        return false;
    }

    if (sizeof(orb_file_header) + sizeof(orb_section) * header->section_count > file.len) {
        orb_error_set(err, "orb file: truncated section table");
        return false;
    }

    const orb_section* table = (const orb_section*)(file.ptr + sizeof(orb_file_header));

    memset(out, 0, sizeof *out);

    uint32_t sprite_ids_size = 0, animation_ids_size = 0, sample_ids_size = 0, song_ids_size = 0;

    for (uint32_t i = 0; i < header->section_count; i++) {
        const orb_section* s = &table[i];

        if ((size_t)s->offset + s->size > file.len || (s->offset & 15) != 0) {
            orb_error_set(err, "orb file: section %u out of bounds or misaligned", s->tag);
            return false;
        }

        const uint8_t* data = file.ptr + s->offset;

        switch (s->tag) {
        case ORB_SEC_INFO:
            if (s->size < sizeof(orb_info_desc) ||
                !memchr(data + 4, 0, sizeof(orb_info_desc) - 4)) {
                orb_error_set(err, "orb file: bad info section");
                return false;
            }

            out->info = (const orb_info_desc*)data;
            break;
        case ORB_SEC_PALETTE:
            out->palette = data;
            break;
        case ORB_SEC_SHEETS:
            out->sheets = (const orb_sheet_desc*)data;
            out->sheet_count = s->size / sizeof(orb_sheet_desc);
            break;
        case ORB_SEC_PIXELS:
            out->pixels = data;
            out->pixel_count = s->size;
            break;
        case ORB_SEC_SPRITES:
            out->sprites = (const orb_sprite_desc*)data;
            out->sprite_count = s->size / sizeof(orb_sprite_desc);
            break;
        case ORB_SEC_ANIMATIONS:
            out->animations = (const orb_animation_desc*)data;
            out->animation_count = s->size / sizeof(orb_animation_desc);
            break;
        case ORB_SEC_DURATIONS:
            out->durations = (const uint16_t*)data;
            out->duration_count = s->size / sizeof(uint16_t);
            break;
        case ORB_SEC_SPRITE_IDS:
            out->sprite_ids = (const uint64_t*)data;
            sprite_ids_size = s->size;
            break;
        case ORB_SEC_ANIMATION_IDS:
            out->animation_ids = (const uint64_t*)data;
            animation_ids_size = s->size;
            break;
        case ORB_SEC_SAMPLES:
            out->samples = (const orb_sample_desc*)data;
            out->sample_count = s->size / sizeof(orb_sample_desc);
            break;
        case ORB_SEC_PCM:
            out->pcm = (const int16_t*)data;
            out->pcm_count = s->size / sizeof(int16_t);
            break;
        case ORB_SEC_SAMPLE_IDS:
            out->sample_ids = (const uint64_t*)data;
            sample_ids_size = s->size;
            break;
        case ORB_SEC_SONGS:
            out->songs = (const orb_song_desc*)data;
            out->song_count = s->size / sizeof(orb_song_desc);
            break;
        case ORB_SEC_SONG_IDS:
            out->song_ids = (const uint64_t*)data;
            song_ids_size = s->size;
            break;
        default:
            break; // unknown sections are skipped
        }
    }

    if (!out->info || !out->palette) {
        orb_error_set(err, "orb file: no info or palette section");
        return false;
    }

    if (out->sprite_count > ORB_MAX_SPRITES || out->animation_count > ORB_MAX_ANIMATIONS) {
        orb_error_set(
            err, "orb file: %u sprites and %u animations, at most %u and %u", out->sprite_count,
            out->animation_count, ORB_MAX_SPRITES, ORB_MAX_ANIMATIONS
        );
        return false;
    }

    if (sprite_ids_size < (uint64_t)out->sprite_count * sizeof(uint64_t)) {
        orb_error_set(
            err, "orb file: SPRITE_IDS section holds %zu ids, need %u",
            sprite_ids_size / sizeof(uint64_t), out->sprite_count
        );
        return false;
    }

    if (animation_ids_size < (uint64_t)out->animation_count * sizeof(uint64_t)) {
        orb_error_set(
            err, "orb file: ANIMATION_IDS section holds %zu ids, need %u",
            animation_ids_size / sizeof(uint64_t), out->animation_count
        );
        return false;
    }

    if (out->sample_count > ORB_MAX_SAMPLES || out->song_count > ORB_MAX_SONGS) {
        orb_error_set(
            err, "orb file: %u samples and %u songs, at most %u and %u", out->sample_count,
            out->song_count, ORB_MAX_SAMPLES, ORB_MAX_SONGS
        );
        return false;
    }

    if (sample_ids_size < (uint64_t)out->sample_count * sizeof(uint64_t)) {
        orb_error_set(
            err, "orb file: SAMPLE_IDS section holds %zu ids, need %u",
            sample_ids_size / sizeof(uint64_t), out->sample_count
        );
        return false;
    }

    if (song_ids_size < (uint64_t)out->song_count * sizeof(uint64_t)) {
        orb_error_set(
            err, "orb file: SONG_IDS section holds %zu ids, need %u",
            song_ids_size / sizeof(uint64_t), out->song_count
        );
        return false;
    }

    for (uint32_t i = 0; i < out->sample_count; i++) {
        const orb_sample_desc* d = &out->samples[i];
        uint64_t end = (uint64_t)d->first + (uint64_t)d->count * d->channels;

        if (d->channels != 1 && d->channels != 2) {
            orb_error_set(
                err, "orb file: sample %u has %u channels, expected 1 or 2", i, d->channels
            );
            return false;
        }

        if (end > out->pcm_count) {
            orb_error_set(err, "orb file: sample %u runs past the PCM section", i);
            return false;
        }

        if (d->rate < 1 || d->rate > 192000) {
            orb_error_set(err, "orb file: sample %u has a bad rate %u", i, d->rate);
            return false;
        }

        if (d->loop_end != 0 && (d->loop_end > d->count || d->loop_start >= d->loop_end)) {
            orb_error_set(
                err, "orb file: sample %u has a bad loop start %u end %u for %u frames", i,
                d->loop_start, d->loop_end, d->count
            );
            return false;
        }
    }

    for (uint32_t i = 0; i < out->song_count; i++) {
        if (out->songs[i].sample >= out->sample_count) {
            orb_error_set(
                err, "orb file: song %u names sample %u of %u", i, out->songs[i].sample,
                out->sample_count
            );
            return false;
        }

        if (!(out->songs[i].bpm > 0 && out->songs[i].bpm <= 1000)) { // also refuses NaN
            orb_error_set(err, "orb file: song %u has a bad bpm %g", i, (double)out->songs[i].bpm);
            return false;
        }
    }

    return true;
}

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

    memcpy(dst, data.ptr, data.len);
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
            break;
        case ORB_SEC_ANIMATION_IDS:
            out->animation_ids = (const uint64_t*)data;
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

    return true;
}

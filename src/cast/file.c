#include "file.h"
#include "../core/asset.h"
#include "../core/bytes.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
    FILE_ONE(PAL, uint8_t[256 * 4], pal),
    FILE_ROW(SHEETS, orb_sheet_desc, sheets, sheet_count, 0),
    FILE_ROW(PIXELS, uint8_t, pixels, pixel_count, 0),
    FILE_ROW(SPRITES, orb_sprite_desc, sprites, sprite_count, ORB_MAX_SPRITES),
    FILE_ROW(ANIMS, orb_anim_desc, anims, anim_count, ORB_MAX_ANIMS),
    FILE_ROW(DURATIONS, uint16_t, durations, duration_count, 0),
    FILE_IDS(SPRITE_IDS, sprite_ids, sprite_count),
    FILE_IDS(ANIM_IDS, anim_ids, anim_count),
    FILE_ROW(SAMPLES, orb_sample_desc, samples, sample_count, ORB_MAX_SAMPLES),
    FILE_ROW(PCM, int16_t, pcm, pcm_count, 0),
    FILE_IDS(SAMPLE_IDS, sample_ids, sample_count),
    FILE_ROW(SONGS, orb_song_desc, songs, song_count, ORB_MAX_SONGS),
    FILE_IDS(SONG_IDS, song_ids, song_count),
    FILE_ROW(TILESETS, orb_tileset_desc, tilesets, tileset_count, 0),
    FILE_ROW(LEVELS, orb_level_desc, levels, level_count, ORB_MAX_LEVELS),
    FILE_ROW(LAYERS, orb_layer_desc, layers, layer_count, ORB_MAX_LAYERS),
    FILE_ROW(NEIGHBORS, orb_neighbor_desc, neighbors, neighbor_count, 0),
    FILE_ROW(TILES, uint16_t, tiles, tile_count, 0),
    FILE_ROW(CELLS, uint8_t, cells, cell_count, 0),
    FILE_IDS(LEVEL_IDS, level_ids, level_count),
    FILE_IDS(LAYER_IDS, layer_ids, layer_count),
    FILE_ROW(FONTS, orb_font_desc, fonts, font_count, ORB_MAX_FONTS),
    FILE_ROW(GLYPHS, orb_glyph_desc, glyphs, glyph_count, 0),
    FILE_IDS(FONT_IDS, font_ids, font_count),
    FILE_ROW(BINDINGS, orb_binding_desc, bindings, binding_count, ORB_BTN_COUNT),
    FILE_ROW(TYPES, orb_type_desc, types, type_count, ORB_MAX_TYPES),
    FILE_ROW(PLACEMENTS, orb_placement_desc, placements, placement_count, ORB_MAX_PLACEMENTS),
    FILE_ROW(FIELDS, orb_field_desc, fields, field_count, ORB_MAX_FIELDS),
    FILE_ROW(FIELD_DATA, uint8_t, field_data, field_data_count, 0),
    FILE_IDS(TYPE_IDS, type_ids, type_count),
};

constexpr uint32_t FILE_ROW_COUNT = sizeof file_rows / sizeof *file_rows;

static_assert(FILE_ROW_COUNT == ORB_SEC_COUNT_ - 1, "every section tag has a row");

static const void** file_ptr(const orb_assets* assets, const file_row* row) {
    return (const void**)((char*)assets + row->ptr);
}

static uint32_t* file_count(const orb_assets* assets, const file_row* row) {
    return (uint32_t*)((char*)assets + row->count);
}

static const file_row* file_row_for(uint32_t tag) {
    for (uint32_t i = 0; i < FILE_ROW_COUNT; i++)
        if (file_rows[i].tag == tag) return &file_rows[i];

    return nullptr;
}

orb_span orb_file_write(orb_arena* arena, const orb_assets* in) {
    orb_file_header* header = orb_arena_push(arena, sizeof *header, 16);
    uint8_t* base = (uint8_t*)header;

    header->magic = ORB_FILE_MAGIC;
    header->version = ORB_FILE_VERSION;
    header->section_count = FILE_ROW_COUNT;

    orb_section* table = orb_arena_push(arena, sizeof(orb_section) * FILE_ROW_COUNT, 16);

    for (uint32_t i = 0; i < FILE_ROW_COUNT; i++) {
        const file_row* row = &file_rows[i];
        uint32_t n = row->count == FILE_NO_COUNT ? 1 : *file_count(in, row);
        size_t size = (size_t)row->elem * n;
        uint8_t* dst = orb_arena_push(arena, size, 16);

        if (size) memcpy(dst, *file_ptr(in, row), size);

        table[i] = (orb_section) {
            .tag = row->tag, .offset = (uint32_t)(dst - base), .size = (uint32_t)size
        };
    }

    return (orb_span) {base, (size_t)(arena->base + arena->used - base)};
}

static bool file_check_audio(const orb_assets* out, orb_error* err) {
    for (uint32_t i = 0; i < out->sample_count; i++) {
        const orb_sample_desc* sample = &out->samples[i];
        uint64_t end = (uint64_t)sample->first + (uint64_t)sample->count * sample->channels;

        if (sample->channels != 1 && sample->channels != 2)
            return orb_error_set(
                err, "orb file: sample %u has %u channels, expected 1 or 2", i, sample->channels
            );

        if (end > out->pcm_count)
            return orb_error_set(err, "orb file: sample %u runs past the PCM section", i);

        if (sample->rate < 1 || sample->rate > ORB_MAX_RATE)
            return orb_error_set(err, "orb file: sample %u has a bad rate %u", i, sample->rate);

        if (sample->loop_end != 0 &&
            (sample->loop_end > sample->count || sample->loop_start >= sample->loop_end))
            return orb_error_set(
                err, "orb file: sample %u has a bad loop start %u end %u for %u frames", i,
                sample->loop_start, sample->loop_end, sample->count
            );
    }

    for (uint32_t i = 0; i < out->song_count; i++) {
        if (out->songs[i].sample >= out->sample_count)
            return orb_error_set(
                err, "orb file: song %u names sample %u of %u", i, out->songs[i].sample,
                out->sample_count
            );

        if (out->songs[i].millibpm == 0 || out->songs[i].millibpm > ORB_MAX_BPM * 1000)
            return orb_error_set(
                err, "orb file: song %u has a bad bpm: %u thousandths", i, out->songs[i].millibpm
            );
    }

    return true;
}

static bool file_check_levels(const orb_assets* out, orb_error* err) {
    for (uint32_t i = 0; i < out->tileset_count; i++) {
        const orb_tileset_desc* tileset = &out->tilesets[i];

        if (tileset->sheet >= out->sheet_count || tileset->grid == 0 || tileset->columns == 0)
            return orb_error_set(err, "orb file: tileset %u names a bad sheet or grid", i);
    }

    for (uint32_t i = 0; i < out->layer_count; i++) {
        const orb_layer_desc* layer = &out->layers[i];
        uint64_t area = (uint64_t)layer->columns * layer->rows;

        if (layer->grid == 0 || area == 0)
            return orb_error_set(err, "orb file: layer %u has an empty grid", i);

        if (layer->sublayers > ORB_MAX_SUBLAYERS)
            return orb_error_set(err, "orb file: layer %u has %u sub-layers", i, layer->sublayers);

        if (layer->sublayers && layer->tileset >= out->tileset_count)
            return orb_error_set(err, "orb file: layer %u names tileset %u", i, layer->tileset);

        if (layer->sublayers && (uint64_t)layer->tiles + area * layer->sublayers > out->tile_count)
            return orb_error_set(err, "orb file: layer %u runs past the tiles section", i);

        if (layer->cells != ORB_NO_INDEX && (uint64_t)layer->cells + area > out->cell_count)
            return orb_error_set(err, "orb file: layer %u runs past the cells section", i);

        for (uint64_t k = 0; layer->sublayers && k < area * layer->sublayers; k++) {
            uint16_t id = out->tiles[layer->tiles + k] & ORB_TILE_ID_MASK;

            if (id > out->tilesets[layer->tileset].count)
                return orb_error_set(
                    err, "orb file: layer %u holds tile %u past its tileset", i, id
                );
        }
    }

    for (uint32_t i = 0; i < out->level_count; i++) {
        const orb_level_desc* level = &out->levels[i];

        if ((uint32_t)level->first_layer + level->layer_count > out->layer_count ||
            (uint32_t)level->first_neighbor + level->neighbor_count > out->neighbor_count)
            return orb_error_set(err, "orb file: level %u runs past its layers or neighbors", i);
    }

    for (uint32_t i = 0; i < out->neighbor_count; i++) {
        if (out->neighbors[i].level >= out->level_count ||
            out->neighbors[i].dir > ORB_LEVEL_OVERLAP)
            return orb_error_set(
                err, "orb file: neighbor %u names level %u", i, out->neighbors[i].level
            );
    }

    return true;
}

static bool file_check_fonts(const orb_assets* out, orb_error* err) {
    for (uint32_t i = 0; i < out->font_count; i++) {
        const orb_font_desc* font = &out->fonts[i];

        if (font->sheet >= out->sheet_count)
            return orb_error_set(err, "orb file: font %u names a bad sheet", i);

        const orb_sheet_desc* sheet = &out->sheets[font->sheet];

        if (font->line_height == 0 || font->line_height > sheet->height)
            return orb_error_set(err, "orb file: font %u has a bad line height", i);

        if ((uint64_t)font->first_glyph + ORB_FONT_GLYPHS > out->glyph_count)
            return orb_error_set(err, "orb file: font %u leaves the glyphs section", i);

        for (uint32_t glyph_index = 0; glyph_index < ORB_FONT_GLYPHS; glyph_index++) {
            const orb_glyph_desc* glyph = &out->glyphs[font->first_glyph + glyph_index];

            if ((uint32_t)glyph->x + glyph->width > sheet->width ||
                (uint32_t)glyph->y + font->line_height > sheet->height)
                return orb_error_set(
                    err, "orb file: font %u glyph %u leaves its sheet", i, glyph_index
                );
        }
    }

    return true;
}

static bool file_check_bindings(const orb_assets* out, orb_error* err) {
    if (out->binding_count != 0 && out->binding_count != ORB_BTN_COUNT)
        return orb_error_set(
            err, "orb file: %u bindings, 0 or %u are valid", out->binding_count, ORB_BTN_COUNT
        );

    for (uint32_t i = 0; i < out->binding_count; i++)
        if (!memchr(out->bindings[i].symbol, 0, sizeof out->bindings[i].symbol))
            return orb_error_set(err, "orb file: binding %u has no terminator", i);

    return true;
}

static bool file_check_entities(const orb_assets* out, orb_error* err) {
    for (uint32_t i = 0; i < out->type_count; i++) {
        const orb_type_desc* type = &out->types[i];

        if ((uint64_t)type->first_field + type->field_count > out->field_count)
            return orb_error_set(err, "orb file: type %u runs past the fields section", i);
    }

    for (uint32_t i = 0; i < out->placement_count; i++) {
        const orb_placement_desc* placement = &out->placements[i];

        if (placement->type >= out->type_count || placement->level >= out->level_count)
            return orb_error_set(
                err, "orb file: placement %u names type %u or level %u", i, placement->type,
                placement->level
            );

        if ((uint64_t)placement->first_field + placement->field_count > out->field_count)
            return orb_error_set(err, "orb file: placement %u runs past the fields section", i);
    }

    for (uint32_t i = 0; i < out->level_count; i++) {
        const orb_level_desc* level = &out->levels[i];

        if ((uint64_t)level->first_placement + level->placement_count > out->placement_count)
            return orb_error_set(err, "orb file: level %u runs past its placements", i);
    }

    for (uint32_t i = 0; i < out->field_count; i++) {
        const orb_field_desc* field = &out->fields[i];

        if (field->kind > ORB_FIELD_REF)
            return orb_error_set(err, "orb file: field %u has kind %u", i, field->kind);

        uint32_t width = orb_field_width(field->kind);

        if ((field->data & 3) != 0 ||
            (uint64_t)field->data + (uint64_t)field->count * width > out->field_data_count)
            return orb_error_set(err, "orb file: field %u runs past the field data", i);

        for (uint32_t k = 0; k < field->count; k++) {
            const uint8_t* element = out->field_data + field->data + k * width;

            if (field->kind == ORB_FIELD_STRING) {
                uint32_t at = orb_bytes_u32(element);

                if (at >= out->field_data_count ||
                    !memchr(out->field_data + at, 0, out->field_data_count - at))
                    return orb_error_set(
                        err, "orb file: field %u string %u leaves the field data", i, k
                    );
            }

            if (field->kind == ORB_FIELD_REF && orb_bytes_u32(element) >= out->placement_count)
                return orb_error_set(err, "orb file: field %u ref %u names no placement", i, k);
        }
    }

    return true;
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
        const orb_section* section = &table[i];
        const file_row* row = file_row_for(section->tag);

        if ((size_t)section->offset + section->size > file.len || (section->offset & 15) != 0)
            return orb_error_set(
                err, "orb file: section %u out of bounds or misaligned", section->tag
            );

        if (!row) continue; // unknown sections are skipped

        if (row->count == FILE_NO_COUNT && section->size < row->elem)
            return orb_error_set(err, "orb file: short %s section", row->name);

        *file_ptr(out, row) = file.ptr + section->offset;

        if (row->ids)
            id_sizes[row->tag] = section->size;
        else if (row->count != FILE_NO_COUNT)
            *file_count(out, row) = section->size / row->elem;
    }

    if (out->info && !memchr((const char*)out->info + 4, 0, sizeof(orb_info_desc) - 4))
        return orb_error_set(err, "orb file: bad info section");

    if (!out->info || !out->pal) return orb_error_set(err, "orb file: no info or palette section");

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

    return file_check_audio(out, err) && file_check_levels(out, err) &&
           file_check_fonts(out, err) && file_check_bindings(out, err) &&
           file_check_entities(out, err);
}

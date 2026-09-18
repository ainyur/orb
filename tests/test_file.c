#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    orb_arena a;
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena_init(&a, "test", mem, sizeof mem);

    uint8_t pal[256 * 4] = {0};

    pal[4] = 32;
    pal[5] = 32;
    pal[6] = 64;

    orb_sheet_desc sheets[1] = {{.width = 4, .height = 2, .pixels = 0}};
    uint8_t pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    orb_sprite_desc sprites[2] = {
        {.sheet = 0,
         .x = 0,
         .y = 0,
         .width = 2,
         .height = 2,
         .ox = 1,
         .oy = 1,
         .frame_width = 8,
         .frame_height = 8},
        {.sheet = 0, .x = 2, .y = 0, .width = 2, .height = 2, .frame_width = 8, .frame_height = 8}
    };
    orb_anim_desc anims[1] = {{.first_sprite = 0, .first_duration = 0, .count = 2}};
    uint16_t durations[2] = {6, 12};
    uint64_t sprite_ids[2] = {11, 22}, anim_ids[1] = {44};
    // a mono sample of 4 frames, then a stereo one of 2 frames, interleaved
    int16_t pcm[8] = {100, 200, 300, 400, 1000, -1000, 2000, -2000};
    orb_sample_desc samples[2] = {
        {.first = 0, .count = 4, .rate = 22050, .channels = 1},
        {.first = 4, .count = 2, .loop_start = 0, .loop_end = 2, .rate = 48000, .channels = 2}
    };
    orb_song_desc songs[1] = {{.sample = 1, .millibpm = 120000}};
    uint64_t sample_ids[2] = {55, 66}, song_ids[1] = {77};
    orb_info_desc info = {.width = 64, .height = 32, .name = "fixture"};
    orb_assets in = {
        .info = &info,
        .pal = pal,
        .sheets = sheets,
        .sheet_count = 1,
        .pixels = pixels,
        .pixel_count = 8,
        .sprites = sprites,
        .sprite_count = 2,
        .anims = anims,
        .anim_count = 1,
        .durations = durations,
        .duration_count = 2,
        .sprite_ids = sprite_ids,
        .anim_ids = anim_ids,
        .samples = samples,
        .sample_count = 2,
        .pcm = pcm,
        .pcm_count = 8,
        .songs = songs,
        .song_count = 1,
        .sample_ids = sample_ids,
        .song_ids = song_ids
    };

    orb_span file = orb_file_write(&a, &in);
    CHECK(file.len > sizeof(orb_file_header) + 6 * sizeof(orb_section));
    CHECK(((uintptr_t)file.ptr & 15) == 0);

    orb_assets out;
    orb_error err;
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.info->width, 64);
    CHECK_EQ(out.info->height, 32);
    CHECK(strcmp(out.info->name, "fixture") == 0);
    CHECK_EQ(out.pal[6], 64);
    CHECK_EQ(out.sheet_count, 1);
    CHECK_EQ(out.sheets[0].width, 4);
    CHECK_EQ(out.pixel_count, 8);
    CHECK_EQ(out.pixels[7], 8);
    CHECK_EQ(out.sprite_count, 2);
    CHECK_EQ(out.sprites[1].x, 2);
    CHECK_EQ(out.sprites[0].ox, 1);
    CHECK_EQ(out.anim_count, 1);
    CHECK_EQ(out.anims[0].count, 2);
    CHECK_EQ(out.duration_count, 2);
    CHECK_EQ(out.durations[1], 12);
    CHECK(((uintptr_t)out.sprites & 15) == 0);
    CHECK_EQ(out.sprite_ids[1], 22);
    CHECK_EQ(out.anim_ids[0], 44);
    CHECK(out.sprite_gens == nullptr); // a loaded file carries no runtime generations
    CHECK_EQ(out.sample_count, 2);
    CHECK_EQ(out.samples[1].first, 4);
    CHECK_EQ(out.samples[1].channels, 2);
    CHECK_EQ(out.samples[1].loop_end, 2);
    CHECK_EQ(out.pcm_count, 8);
    CHECK_EQ(out.pcm[5], -1000);
    CHECK(((uintptr_t)out.pcm & 15) == 0);
    CHECK_EQ(out.song_count, 1);
    CHECK_EQ(out.songs[0].sample, 1);
    CHECK_EQ(out.sample_ids[1], 66);
    CHECK_EQ(out.song_ids[0], 77);
    CHECK(out.sample_gens == nullptr);

    // a sample that runs past the PCM section and a song naming a missing sample are refused
    samples[1].count = 3;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "PCM") != nullptr);
    samples[1].count = 2;
    songs[0].sample = 5;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "song") != nullptr);
    songs[0].sample = 1;
    file = orb_file_write(&a, &in);
    CHECK(orb_file_load(file, &out, &err));

    // a sample with a bad channel count is refused
    samples[1].channels = 3;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "channels") != nullptr);
    samples[1].channels = 2;

    // a rate or bpm out of range is refused
    samples[1].rate = 0;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "rate") != nullptr);
    samples[1].rate = 48000;
    songs[0].millibpm = 0;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "bpm") != nullptr);
    songs[0].millibpm = 120000;

    // a sample whose loop runs past its own frame count is refused
    samples[1].loop_end = samples[1].count + 1;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "loop") != nullptr);
    samples[1].loop_end = 2;

    // a short SAMPLE_IDS section is refused
    file = orb_file_write(&a, &in);
    {
        uint32_t section_count = ((const orb_file_header*)file.ptr)->section_count;
        orb_section* table = (orb_section*)(mem + (file.ptr - mem) + sizeof(orb_file_header));

        for (uint32_t i = 0; i < section_count; i++)
            if (table[i].tag == ORB_SEC_SAMPLE_IDS) table[i].size -= 8;
    }
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "ids") != nullptr);

    file = orb_file_write(&a, &in);
    CHECK(orb_file_load(file, &out, &err));

    mem[file.ptr - mem + 4] = 99; // version
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "version") != nullptr);

    // levels: two tilesets, two levels, three layers, one neighbor link
    orb_tileset_desc tilesets[1] = {
        {.sheet = 0, .grid = 2, .spacing = 0, .padding = 0, .columns = 2, .count = 2}
    };
    uint16_t tiles[2 * 2 * 2 + 1 * 1] = {1, 2, 0, 2 | ORB_TILE_FLIP_X, 0, 0, 1, 0, 1};
    uint8_t cells[2 * 2] = {0, 1, 1, 0};
    orb_layer_desc layers[3] = {
        {.tiles = 0,
         .cells = ORB_NO_INDEX,
         .tileset = 0,
         .grid = 2,
         .columns = 2,
         .rows = 2,
         .sublayers = 2},
        {.tiles = 0, .cells = 0, .grid = 2, .columns = 2, .rows = 2, .sublayers = 0},
        {.tiles = 8,
         .cells = ORB_NO_INDEX,
         .tileset = 0,
         .grid = 2,
         .columns = 1,
         .rows = 1,
         .offset_x = 3,
         .offset_y = -1,
         .parallax_x = 0.5f,
         .parallax_y = 1,
         .sublayers = 1},
    };
    orb_level_desc levels[2] = {
        {.world_x = 0,
         .world_y = 0,
         .depth = 0,
         .width = 4,
         .height = 4,
         .first_layer = 0,
         .layer_count = 2,
         .first_neighbor = 0,
         .neighbor_count = 1},
        {.world_x = 4,
         .world_y = -8,
         .depth = 1,
         .width = 2,
         .height = 2,
         .first_layer = 2,
         .layer_count = 1,
         .first_neighbor = 1,
         .neighbor_count = 0},
    };
    orb_neighbor_desc neighbors[1] = {{.level = 1, .dir = ORB_NEIGHBOR_E}};
    uint64_t level_ids[2] = {88, 99}, layer_ids[3] = {1, 2, 3};

    in.tilesets = tilesets;
    in.tileset_count = 1;
    in.levels = levels;
    in.level_count = 2;
    in.layers = layers;
    in.layer_count = 3;
    in.neighbors = neighbors;
    in.neighbor_count = 1;
    in.tiles = tiles;
    in.tile_count = 9;
    in.cells = cells;
    in.cell_count = 4;
    in.level_ids = level_ids;
    in.layer_ids = layer_ids;

    file = orb_file_write(&a, &in);
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.level_count, 2);
    CHECK_EQ(out.levels[1].world_y, -8);
    CHECK_EQ(out.levels[1].depth, 1);
    CHECK_EQ(out.layer_count, 3);
    CHECK_EQ(out.layers[2].offset_y, -1);
    CHECK(out.layers[2].parallax_x == 0.5f);
    CHECK_EQ(out.layers[1].cells, 0);
    CHECK_EQ(out.layers[0].cells, ORB_NO_INDEX);
    CHECK_EQ(out.tile_count, 9);
    CHECK_EQ(out.tiles[3], 2 | ORB_TILE_FLIP_X);
    CHECK_EQ(out.cell_count, 4);
    CHECK_EQ(out.neighbor_count, 1);
    CHECK_EQ(out.neighbors[0].dir, ORB_NEIGHBOR_E);
    CHECK_EQ(out.level_ids[1], 99);
    CHECK_EQ(out.layer_ids[2], 3);
    CHECK_EQ(out.tilesets[0].count, 2);
    CHECK(out.level_gens == nullptr);
    CHECK(((uintptr_t)out.layers & 15) == 0);

    // a level whose layers run past the layer section
    levels[1].layer_count = 2;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "level 1") != nullptr);
    levels[1].layer_count = 1;

    // a layer whose tiles run past the tile section
    layers[2].tiles = 9;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "layer 2") != nullptr);
    layers[2].tiles = 8;

    // a layer whose cells run past the cell section
    layers[1].cells = 1;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "cells") != nullptr);
    layers[1].cells = 0;

    // too many sub-layers, a missing tileset, a tile id past the tileset, a bad neighbor
    layers[0].sublayers = 9;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "sub-layers") != nullptr);
    layers[0].sublayers = 2;
    layers[0].tileset = 4;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "tileset") != nullptr);
    layers[0].tileset = 0;
    tiles[1] = 3; // tileset count is 2, so ids 1 and 2 are the only valid stored values
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "tile") != nullptr);
    tiles[1] = 2;
    neighbors[0].level = 2;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "neighbor") != nullptr);
    neighbors[0].level = 1;
    tilesets[0].sheet = 1;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "sheet") != nullptr);
    tilesets[0].sheet = 0;

    // a file sealed before levels existed loads with none
    in.tileset_count = in.level_count = in.layer_count = in.neighbor_count = 0;
    in.tile_count = in.cell_count = 0;
    file = orb_file_write(&a, &in);
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.level_count, 0);

    // A font whose glyphs sit inside its sheet round-trips; one that leaves it is refused.
    orb_sheet_desc font_sheets[] = {{64, 8, 0}};
    orb_font_desc fonts[] = {{.sheet = 0, .first_glyph = 0, .line_height = 8}};
    orb_glyph_desc glyphs[ORB_FONT_GLYPHS] = {};
    uint64_t font_ids[] = {orb_asset_id("body", "font")};

    for (uint32_t i = 0; i < ORB_FONT_GLYPHS; i++)
        glyphs[i] = (orb_glyph_desc) {.x = 0, .y = 0, .width = 4, .advance = 5};

    orb_assets font_in = {
        .info = &info,
        .pal = pal,
        .sheets = font_sheets,
        .sheet_count = 1,
        .fonts = fonts,
        .font_count = 1,
        .glyphs = glyphs,
        .glyph_count = ORB_FONT_GLYPHS,
        .font_ids = font_ids
    };
    orb_assets font_out = {};
    orb_span font_file = orb_file_write(&a, &font_in);

    CHECK(orb_file_load(font_file, &font_out, &err));
    CHECK_EQ(font_out.font_count, 1);
    CHECK_EQ(font_out.glyph_count, ORB_FONT_GLYPHS);
    CHECK_EQ(font_out.fonts[0].line_height, 8);
    CHECK_EQ(font_out.glyphs[7].advance, 5);
    CHECK_EQ(font_out.font_ids[0], orb_asset_id("body", "font"));

    glyphs[3].x = 61; // 61 + 4 leaves a 64-wide sheet
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &font_in), &font_out, &err));

    glyphs[3].x = 0;
    fonts[0].line_height = 0;
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &font_in), &font_out, &err));

    fonts[0].line_height = 8;
    fonts[0].sheet = 1; // only one sheet exists
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &font_in), &font_out, &err));

    fonts[0].sheet = 0;
    fonts[0].first_glyph = 1; // 95 glyphs from index 1 leave the 95-element section
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &font_in), &font_out, &err));

    fonts[0].first_glyph = 0;

    // bindings: twelve records round-trip, zero load, anything else is refused
    orb_binding_desc bindings[ORB_BTN_COUNT] = {};

    for (int i = 0; i < ORB_BTN_COUNT; i++)
        snprintf(bindings[i].symbol, sizeof bindings[i].symbol, "%s", orb_button_names[i]);

    orb_assets binding_in = {
        .info = &info, .pal = pal, .bindings = bindings, .binding_count = ORB_BTN_COUNT
    };
    orb_assets binding_out = {};

    orb_arena_reset(&a);
    CHECK(orb_file_load(orb_file_write(&a, &binding_in), &binding_out, &err));
    CHECK_EQ(binding_out.binding_count, ORB_BTN_COUNT);
    CHECK(strcmp(binding_out.bindings[ORB_BTN_SELECT].symbol, "select") == 0);

    binding_in.binding_count = 0;
    orb_arena_reset(&a);
    CHECK(orb_file_load(orb_file_write(&a, &binding_in), &binding_out, &err));
    CHECK_EQ(binding_out.binding_count, 0);

    binding_in.binding_count = 5;
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &binding_in), &binding_out, &err));
    CHECK(strstr(err.text, "0 or 12 are valid") != nullptr);

    binding_in.binding_count = ORB_BTN_COUNT;
    memset(bindings[3].symbol, 'x', sizeof bindings[3].symbol);
    orb_arena_reset(&a);
    CHECK(!orb_file_load(orb_file_write(&a, &binding_in), &binding_out, &err));
    CHECK(strstr(err.text, "binding 3 has no terminator") != nullptr);

    return 0;
}

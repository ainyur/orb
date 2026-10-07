#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// One level, one type with a default, one placement with every field kind, round-tripped
// and then broken one way at a time.
static int test_entities(orb_arena* scratch) {
    u8 pal[256 * 4] = {0};
    orb_info_desc info = {.width = 8, .height = 8, .name = "e"};
    orb_level_desc levels[1] = {
        {.width = 64, .height = 32, .first_placement = 0, .placement_count = 1}
    };
    u64 level_ids[1] = {1};
    orb_type_desc types[1] = {{.width = 8, .height = 8, .first_field = 0, .field_count = 1}};
    u64 type_ids[1] = {orb_asset_id("crate", "")};
    orb_placement_desc placements[1] = {
        {.iid = 99,
         .type = 0,
         .level = 0,
         .x = 16,
         .y = 8,
         .width = 8,
         .height = 8,
         .first_field = 1,
         .field_count = 4}
    };
    // data: int 10 | int 3 | bool 1 (padded) | string offset 24 | point (40,16) ... "box\0"
    u8 data[64] = {0};
    i32 ten = 10, three = 3, x = 40, y = 16;
    u32 string_at = 24, ref = 0;

    memcpy(data + 0, &ten, 4);
    memcpy(data + 4, &three, 4);
    data[8] = 1;
    memcpy(data + 12, &string_at, 4);
    memcpy(data + 16, &x, 4);
    memcpy(data + 20, &y, 4);
    memcpy(data + 24, "box", 4);
    memcpy(data + 28, &ref, 4);

    orb_field_desc fields[5] = {
        {.name = orb_asset_id("hp", ""), .data = 0, .count = 1, .kind = ORB_FIELD_INT},
        {.name = orb_asset_id("hp", ""), .data = 4, .count = 1, .kind = ORB_FIELD_INT},
        {.name = orb_asset_id("locked", ""), .data = 8, .count = 1, .kind = ORB_FIELD_BOOL},
        {.name = orb_asset_id("label", ""), .data = 12, .count = 1, .kind = ORB_FIELD_STRING},
        {.name = orb_asset_id("exit", ""), .data = 16, .count = 1, .kind = ORB_FIELD_POINT},
    };
    orb_assets in = {
        .info = &info,
        .pal = pal,
        .levels = {levels, 1},
        .level_ids = level_ids,
        .types = {types, 1},
        .type_ids = type_ids,
        .placements = {placements, 1},
        .fields = {fields, 5},
        .field_data = {data, 32}
    };
    orb_assets out;
    orb_error err;

    u8_span file = orb_file_write(scratch, &in);
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.types.len, 1);
    CHECK_EQ(out.placements.len, 1);
    CHECK_EQ(out.fields.len, 5);
    CHECK_EQ(out.field_data.len, 32);
    CHECK_EQ(out.levels.elems[0].placement_count, 1);
    CHECK(out.type_ids[0] == orb_asset_id("crate", ""));
    CHECK_EQ(out.placements.elems[0].x, 16);
    CHECK_EQ(orb_bytes_i32(out.field_data.elems + out.fields.elems[1].data), 3);

    // a ref row past the placements
    fields[4] = (orb_field_desc) {.name = 5, .data = 28, .count = 1, .kind = ORB_FIELD_REF};
    ref = 1;
    memcpy(data + 28, &ref, 4);
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "ref 0 names no placement"));
    ref = 0;
    memcpy(data + 28, &ref, 4);
    file = orb_file_write(scratch, &in);
    CHECK(orb_file_load(file, &out, &err));

    // a string whose offset leaves the data, then one whose bytes run to the end unterminated
    string_at = 40;
    memcpy(data + 12, &string_at, 4);
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "string 0 leaves"));
    string_at = 29; // "ox" and then the ref bytes, cut before any NUL
    memcpy(data + 12, &string_at, 4);
    in.field_data.len = 31;
    fields[4].count = 0;
    data[29] = 'o'; // non-zero past the "box\0" terminator, so the window holds no NUL
    data[30] = 'x';
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "string 0 leaves"));
    string_at = 24;
    memcpy(data + 12, &string_at, 4);
    in.field_data.len = 32;
    fields[4].count = 1;
    data[29] = 0;
    data[30] = 0;

    // elements past the data, a misaligned offset, a bad kind
    fields[0].count = 9;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "field 0 runs past"));
    fields[0].count = 1;
    fields[0].data = 2;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    fields[0].data = 0;
    fields[0].kind = 9;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "field 0 has kind 9"));
    fields[0].kind = ORB_FIELD_INT;

    // a placement naming a type or level out of range, a level running past its placements,
    // a type running past the fields
    placements[0].type = 1;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    placements[0].type = 0;
    levels[0].placement_count = 2;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "level 0 runs past its placements"));
    levels[0].placement_count = 1;
    types[0].field_count = 6;
    file = orb_file_write(scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "type 0 runs past"));
    types[0].field_count = 1;
    file = orb_file_write(scratch, &in);
    CHECK(orb_file_load(file, &out, &err));

    return 0;
}

int main(void) {
    orb_arena scratch;
    static alignas(16) u8 mem[1 << 20];
    orb_arena_init(&scratch, "test", mem, sizeof mem);

    // orb_bytes_sub never wraps: a range past the end or an absurd length is empty
    static const u8 ten_bytes[10] = {0};
    u8_span bytes = {ten_bytes, 10};

    CHECK_EQ(orb_bytes_sub(bytes, 4, 6).len, 6);
    CHECK_EQ(orb_bytes_sub(bytes, 4, 7).len, 0);
    CHECK_EQ(orb_bytes_sub(bytes, 11, 0).len, 0);
    CHECK_EQ(orb_bytes_sub(bytes, 2, SIZE_MAX).len, 0);
    CHECK(orb_bytes_sub(bytes, 2, SIZE_MAX).elems == nullptr);

    u8 pal[256 * 4] = {0};

    pal[4] = 32;
    pal[5] = 32;
    pal[6] = 64;

    orb_sheet_desc sheets[1] = {{.width = 4, .height = 2, .pixels = 0}};
    u8 pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
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
    u16 durations[2] = {6, 12};
    u64 sprite_ids[2] = {11, 22}, anim_ids[1] = {44};
    // a mono sample of 4 frames, then a stereo one of 2 frames, interleaved
    i16 pcm[8] = {100, 200, 300, 400, 1000, -1000, 2000, -2000};
    orb_sample_desc samples[2] = {
        {.first = 0, .count = 4, .rate = 22050, .channels = 1},
        {.first = 4, .count = 2, .loop_start = 0, .loop_end = 2, .rate = 48000, .channels = 2}
    };
    orb_song_desc songs[1] = {{.sample = 1, .millibpm = 120000}};
    u64 sample_ids[2] = {55, 66}, song_ids[1] = {77};
    orb_info_desc info = {
        .width = 64, .height = 32, .window_width = 128, .window_height = 64, .name = "fixture"
    };
    orb_assets in = {
        .info = &info,
        .pal = pal,
        .sheets = {sheets, 1},
        .pixels = {pixels, 8},
        .sprites = {sprites, 2},
        .anims = {anims, 1},
        .durations = {durations, 2},
        .sprite_ids = sprite_ids,
        .anim_ids = anim_ids,
        .samples = {samples, 2},
        .pcm = {pcm, 8},
        .songs = {songs, 1},
        .sample_ids = sample_ids,
        .song_ids = song_ids
    };

    u8_span file = orb_file_write(&scratch, &in);
    CHECK(file.len > sizeof(orb_file_header) + 6 * sizeof(orb_section));
    CHECK(((uintptr_t)file.elems & 15) == 0);

    orb_assets out;
    orb_error err;
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.info->width, 64);
    CHECK_EQ(out.info->height, 32);
    CHECK_EQ(out.info->window_width, 128);
    CHECK_EQ(out.info->window_height, 64);
    CHECK(strcmp(out.info->name, "fixture") == 0);
    CHECK_EQ(out.pal[6], 64);
    CHECK_EQ(out.sheets.len, 1);
    CHECK_EQ(out.sheets.elems[0].width, 4);
    CHECK_EQ(out.pixels.len, 8);
    CHECK_EQ(out.pixels.elems[7], 8);
    CHECK_EQ(out.sprites.len, 2);
    CHECK_EQ(out.sprites.elems[1].x, 2);
    CHECK_EQ(out.sprites.elems[0].ox, 1);
    CHECK_EQ(out.anims.len, 1);
    CHECK_EQ(out.anims.elems[0].count, 2);
    CHECK_EQ(out.durations.len, 2);
    CHECK_EQ(out.durations.elems[1], 12);
    CHECK(((uintptr_t)out.sprites.elems & 15) == 0);
    CHECK_EQ(out.sprite_ids[1], 22);
    CHECK_EQ(out.anim_ids[0], 44);
    CHECK(out.sprite_gens == nullptr); // a loaded file carries no runtime generations
    CHECK_EQ(out.samples.len, 2);
    CHECK_EQ(out.samples.elems[1].first, 4);
    CHECK_EQ(out.samples.elems[1].channels, 2);
    CHECK_EQ(out.samples.elems[1].loop_end, 2);
    // every counted section is a span whose len is its element count
    CHECK_EQ(out.sprites.len, 2);
    CHECK(out.sprite_ids != nullptr);
    CHECK_EQ(out.pcm.len * sizeof(i16), 8 * sizeof(i16));
    CHECK_EQ(out.pcm.len, 8);
    CHECK_EQ(out.pcm.elems[5], -1000);
    CHECK(((uintptr_t)out.pcm.elems & 15) == 0);
    CHECK_EQ(out.songs.len, 1);
    CHECK_EQ(out.songs.elems[0].sample, 1);
    CHECK_EQ(out.sample_ids[1], 66);
    CHECK_EQ(out.song_ids[0], 77);
    CHECK(out.sample_gens == nullptr);

    // a sample that runs past the PCM section and a song naming a missing sample are refused
    samples[1].count = 3;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "PCM") != nullptr);
    samples[1].count = 2;
    songs[0].sample = 5;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "song") != nullptr);
    songs[0].sample = 1;
    file = orb_file_write(&scratch, &in);
    CHECK(orb_file_load(file, &out, &err));

    // an info name with no zero byte is refused
    memset(info.name, 'x', sizeof info.name);
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strcmp(err.text, "orb file: bad info section") == 0);
    strcpy(info.name, "fixture");
    file = orb_file_write(&scratch, &in);
    CHECK(orb_file_load(file, &out, &err));

    // a sample with a bad channel count is refused
    samples[1].channels = 3;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "channels") != nullptr);
    samples[1].channels = 2;

    // a rate or bpm out of range is refused
    samples[1].rate = 0;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "rate") != nullptr);
    samples[1].rate = 48000;
    songs[0].millibpm = 0;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "bpm") != nullptr);
    songs[0].millibpm = 120000;

    // a sample whose loop runs past its own frame count is refused
    samples[1].loop_end = samples[1].count + 1;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "loop") != nullptr);
    samples[1].loop_end = 2;

    // a short SAMPLE_IDS section is refused
    file = orb_file_write(&scratch, &in);
    {
        u32 section_count = ((const orb_file_header*)file.elems)->section_count;
        orb_section* table = (orb_section*)(mem + (file.elems - mem) + sizeof(orb_file_header));

        for (u32 i = 0; i < section_count; i++)
            if (table[i].tag == ORB_SEC_SAMPLE_IDS) table[i].size -= 8;
    }
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "ids") != nullptr);

    file = orb_file_write(&scratch, &in);
    CHECK(orb_file_load(file, &out, &err));

    mem[file.elems - mem + 4] = 99; // version
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "version") != nullptr);

    // levels: two tilesets, two levels, three layers, one neighbor link
    orb_tileset_desc tilesets[1] = {
        {.sheet = 0, .grid = 2, .spacing = 0, .padding = 0, .columns = 2, .count = 2}
    };
    u16 tiles[2 * 2 * 2 + 1 * 1] = {1, 2, 0, 2 | ORB_TILE_FLIP_X, 0, 0, 1, 0, 1};
    u8 cells[2 * 2] = {0, 1, 1, 0};
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
         .width = 4,
         .height = 4,
         .first_layer = 0,
         .layer_count = 2,
         .first_neighbor = 0,
         .neighbor_count = 1},
        {.world_x = 4,
         .world_y = -8,
         .width = 2,
         .height = 2,
         .first_layer = 2,
         .layer_count = 1,
         .first_neighbor = 1,
         .neighbor_count = 0},
    };
    orb_neighbor_desc neighbors[1] = {{.level = 1, .dir = ORB_LEVEL_E}};
    u64 level_ids[2] = {88, 99}, layer_ids[3] = {1, 2, 3};

    in.tilesets = (orb_tileset_desc_span) {tilesets, 1};
    in.levels = (orb_level_desc_span) {levels, 2};
    in.layers = (orb_layer_desc_span) {layers, 3};
    in.neighbors = (orb_neighbor_desc_span) {neighbors, 1};
    in.tiles = (u16_span) {tiles, 9};
    in.cells = (u8_span) {cells, 4};
    in.level_ids = level_ids;
    in.layer_ids = layer_ids;

    file = orb_file_write(&scratch, &in);
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.levels.len, 2);
    CHECK_EQ(out.levels.elems[1].world_y, -8);
    CHECK_EQ(out.layers.len, 3);
    CHECK_EQ(out.layers.elems[2].offset_y, -1);
    CHECK(out.layers.elems[2].parallax_x == 0.5f);
    CHECK_EQ(out.layers.elems[1].cells, 0);
    CHECK_EQ(out.layers.elems[0].cells, ORB_NO_INDEX);
    CHECK_EQ(out.tiles.len, 9);
    CHECK_EQ(out.tiles.elems[3], 2 | ORB_TILE_FLIP_X);
    CHECK_EQ(out.cells.len, 4);
    CHECK_EQ(out.neighbors.len, 1);
    CHECK_EQ(out.neighbors.elems[0].dir, ORB_LEVEL_E);
    CHECK_EQ(out.level_ids[1], 99);
    CHECK_EQ(out.layer_ids[2], 3);
    CHECK_EQ(out.tilesets.elems[0].count, 2);
    CHECK(out.level_gens == nullptr);
    CHECK(((uintptr_t)out.layers.elems & 15) == 0);

    // a level whose layers run past the layer section
    levels[1].layer_count = 2;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "level 1") != nullptr);
    levels[1].layer_count = 1;

    // a layer whose tiles run past the tile section
    layers[2].tiles = 9;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "layer 2") != nullptr);
    layers[2].tiles = 8;

    // a layer whose cells run past the cell section
    layers[1].cells = 1;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "cells") != nullptr);
    layers[1].cells = 0;

    // too many sub-layers, a missing tileset, a tile id past the tileset, a bad neighbor
    layers[0].sublayers = 9;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "sub-layers") != nullptr);
    layers[0].sublayers = 2;
    layers[0].tileset = 4;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "tileset") != nullptr);
    layers[0].tileset = 0;
    tiles[1] = 3; // tileset count is 2, so ids 1 and 2 are the only valid stored values
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "tile") != nullptr);
    tiles[1] = 2;
    neighbors[0].level = 2;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "neighbor") != nullptr);
    neighbors[0].level = 1;
    tilesets[0].sheet = 1;
    file = orb_file_write(&scratch, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "sheet") != nullptr);
    tilesets[0].sheet = 0;

    // a file sealed before levels existed loads with none
    in.tilesets.len = in.levels.len = in.layers.len = in.neighbors.len = 0;
    in.tiles.len = in.cells.len = 0;
    file = orb_file_write(&scratch, &in);
    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.levels.len, 0);

    // A font whose glyphs sit inside its sheet round-trips; one that leaves it is refused.
    orb_sheet_desc font_sheets[] = {{64, 8, 0}};
    orb_font_desc fonts[] = {{.sheet = 0, .first_glyph = 0, .line_height = 8}};
    orb_glyph_desc glyphs[ORB_FONT_GLYPHS] = {};
    u64 font_ids[] = {orb_asset_id("body", "font")};

    for (u32 i = 0; i < ORB_FONT_GLYPHS; i++)
        glyphs[i] = (orb_glyph_desc) {.x = 0, .y = 0, .width = 4, .advance = 5};

    orb_assets font_in = {
        .info = &info,
        .pal = pal,
        .sheets = {font_sheets, 1},
        .fonts = {fonts, 1},
        .glyphs = {glyphs, ORB_FONT_GLYPHS},
        .font_ids = font_ids
    };
    orb_assets font_out = {};
    u8_span font_file = orb_file_write(&scratch, &font_in);

    CHECK(orb_file_load(font_file, &font_out, &err));
    CHECK_EQ(font_out.fonts.len, 1);
    CHECK_EQ(font_out.glyphs.len, ORB_FONT_GLYPHS);
    CHECK_EQ(font_out.fonts.elems[0].line_height, 8);
    CHECK_EQ(font_out.glyphs.elems[7].advance, 5);
    CHECK_EQ(font_out.font_ids[0], orb_asset_id("body", "font"));

    glyphs[3].x = 61; // 61 + 4 leaves a 64-wide sheet
    orb_arena_clear(&scratch);
    CHECK(!orb_file_load(orb_file_write(&scratch, &font_in), &font_out, &err));

    glyphs[3].x = 0;
    fonts[0].line_height = 0;
    orb_arena_clear(&scratch);
    CHECK(!orb_file_load(orb_file_write(&scratch, &font_in), &font_out, &err));

    fonts[0].line_height = 8;
    fonts[0].sheet = 1; // only one sheet exists
    orb_arena_clear(&scratch);
    CHECK(!orb_file_load(orb_file_write(&scratch, &font_in), &font_out, &err));

    fonts[0].sheet = 0;
    fonts[0].first_glyph = 1; // 95 glyphs from index 1 leave the 95-element section
    orb_arena_clear(&scratch);
    CHECK(!orb_file_load(orb_file_write(&scratch, &font_in), &font_out, &err));

    fonts[0].first_glyph = 0;

    if (test_entities(&scratch)) return 1;

    return 0;
}

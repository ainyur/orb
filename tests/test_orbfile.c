#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    orb_error err;

    uint8_t palette[256 * 4] = {0};

    palette[4] = 32;
    palette[5] = 32;
    palette[6] = 64;

    orb_sheet_desc sheets[1] = {{.w = 4, .h = 2, .pixels = 0}};
    uint8_t pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    orb_sprite_desc sprites[2] = {
        {.sheet = 0, .x = 0, .y = 0, .w = 2, .h = 2, .ox = 1, .oy = 1, .fw = 8, .fh = 8},
        {.sheet = 0, .x = 2, .y = 0, .w = 2, .h = 2, .fw = 8, .fh = 8}
    };
    orb_animation_desc animations[1] = {{.first_sprite = 0, .first_duration = 0, .count = 2}};
    uint16_t durations[2] = {6, 12};
    uint64_t sprite_ids[2] = {11, 22}, animation_ids[1] = {44};
    orb_assets in = {
        .palette = palette,
        .sheets = sheets,
        .sheet_count = 1,
        .pixels = pixels,
        .pixel_count = 8,
        .sprites = sprites,
        .sprite_count = 2,
        .animations = animations,
        .animation_count = 1,
        .durations = durations,
        .duration_count = 2,
        .sprite_ids = sprite_ids,
        .animation_ids = animation_ids
    };

    orb_span file = orb_file_write(&a, &in);

    CHECK(file.len > sizeof(orb_file_header) + 6 * sizeof(orb_section));
    CHECK(((uintptr_t)file.ptr & 15) == 0);

    orb_assets out;

    CHECK(orb_file_load(file, &out, &err));
    CHECK_EQ(out.palette[6], 64);
    CHECK_EQ(out.sheet_count, 1);
    CHECK_EQ(out.sheets[0].w, 4);
    CHECK_EQ(out.pixel_count, 8);
    CHECK_EQ(out.pixels[7], 8);
    CHECK_EQ(out.sprite_count, 2);
    CHECK_EQ(out.sprites[1].x, 2);
    CHECK_EQ(out.sprites[0].ox, 1);
    CHECK_EQ(out.animation_count, 1);
    CHECK_EQ(out.animations[0].count, 2);
    CHECK_EQ(out.duration_count, 2);
    CHECK_EQ(out.durations[1], 12);
    CHECK(((uintptr_t)out.sprites & 15) == 0);
    CHECK_EQ(out.sprite_ids[1], 22);
    CHECK_EQ(out.animation_ids[0], 44);
    CHECK(out.sprite_generations == NULL); // a loaded file carries no runtime generations

    file.ptr[4] = 99; // version
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "version") != NULL);
    return 0;
}

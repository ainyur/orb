#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    orb_arena a;
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena_init(&a, "test", mem, sizeof mem);

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
    // a mono sample of 4 frames, then a stereo one of 2 frames, interleaved
    int16_t pcm[8] = {100, 200, 300, 400, 1000, -1000, 2000, -2000};
    orb_sample_desc samples[2] = {
        {.first = 0, .count = 4, .rate = 22050, .channels = 1},
        {.first = 4, .count = 2, .loop_start = 0, .loop_end = 2, .rate = 48000, .channels = 2}
    };
    orb_song_desc songs[1] = {{.sample = 1}};
    uint64_t sample_ids[2] = {55, 66}, song_ids[1] = {77};
    orb_info_desc info = {.w = 64, .h = 32, .name = "fixture"};
    orb_assets in = {
        .info = &info,
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
        .animation_ids = animation_ids,
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
    CHECK_EQ(out.info->w, 64);
    CHECK_EQ(out.info->h, 32);
    CHECK(strcmp(out.info->name, "fixture") == 0);
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
    CHECK(out.sprite_generations == nullptr); // a loaded file carries no runtime generations
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
    CHECK(out.sample_generations == nullptr);

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

    // a sample whose loop runs past its own frame count is refused
    samples[1].loop_end = samples[1].count + 1;
    file = orb_file_write(&a, &in);
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "loop") != nullptr);
    samples[1].loop_end = 2;

    // a short SAMPLE_IDS section is refused
    file = orb_file_write(&a, &in);
    {
        orb_section* table = (orb_section*)(mem + (file.ptr - mem) + sizeof(orb_file_header));

        for (uint32_t i = 0; i < ORB_SEC_COUNT_; i++)
            if (table[i].tag == ORB_SEC_SAMPLE_IDS) table[i].size -= 8;
    }
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "ids") != nullptr);

    file = orb_file_write(&a, &in);
    CHECK(orb_file_load(file, &out, &err));

    mem[file.ptr - mem + 4] = 99; // version
    CHECK(!orb_file_load(file, &out, &err));
    CHECK(strstr(err.text, "version") != nullptr);

    return 0;
}

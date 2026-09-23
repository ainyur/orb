#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static const uint8_t player_ase[] = {
#embed "fixtures/art/player.aseprite"
};
static const uint8_t pal_ase[] = {
#embed "fixtures/art/palette.aseprite"
};

int main(void) {
    static uint8_t mem[1 << 20];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    orb_error err;

    orb_span file = {(uint8_t*)player_ase, sizeof player_ase};
    orb_ase ase;

    CHECK(orb_ase_parse(&a, file, &ase, &err));
    CHECK_EQ(ase.width, 16);
    CHECK_EQ(ase.height, 16);
    CHECK_EQ(ase.frame_count, 2);
    CHECK_EQ(ase.color_count, 4);
    CHECK_EQ(ase.transparent, 0);
    CHECK_EQ(ase.rgb[1][0], 255);
    CHECK_EQ(ase.rgb[2][1], 255);
    CHECK_EQ(ase.durations[0], 100);
    CHECK_EQ(ase.durations[1], 200);

    // frame 0: red square at 4..11, frame 1: green square at x 6..13
    CHECK_EQ(ase.frames[4 * 16 + 4], 1);
    CHECK_EQ(ase.frames[11 * 16 + 11], 1);
    CHECK_EQ(ase.frames[3 * 16 + 3], 0);
    CHECK_EQ(ase.frames[12 * 16 + 12], 0);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 6], 2);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 13], 2);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 5], 0);

    CHECK_EQ(ase.tag_count, 1);
    CHECK(strcmp(ase.tags[0].name, "walk") == 0);
    CHECK_EQ(ase.tags[0].from, 0);
    CHECK_EQ(ase.tags[0].to, 1);
    CHECK_EQ(ase.tags[0].direction, 0);

    // Aseprite writes a 16x16 grid at the origin unless the file sets one.
    CHECK_EQ(ase.grid_x, 0);
    CHECK_EQ(ase.grid_y, 0);
    CHECK_EQ(ase.grid_width, 16);
    CHECK_EQ(ase.grid_height, 16);

    orb_span pal = {(uint8_t*)pal_ase, sizeof pal_ase};
    orb_ase p;

    CHECK(orb_ase_parse(&a, pal, &p, &err));
    CHECK_EQ(p.color_count, 8);
    CHECK_EQ(p.rgb[1][2], 64);
    CHECK_EQ(p.rgb[5][0], 255);

    uint8_t junk[200] = {0};

    CHECK(!orb_ase_parse(&a, (orb_span) {junk, sizeof junk}, &ase, &err));
    CHECK(strstr(err.text, "aseprite") != nullptr);

    // A cel chunk (0x2005) with only the 6-byte generic header: the fixed cel
    // fields it reads at d+0..d+19 are not there.
    static const uint8_t cel_truncated[150] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 22,                 // frame size: 16-byte frame header + 6-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 6,                  // chunk size: the generic header, no cel payload
        [148] = 0x05, [149] = 0x20, // chunk type: cel
    };

    CHECK(!orb_ase_parse(&a, (orb_span) {cel_truncated, sizeof cel_truncated}, &ase, &err));
    CHECK(strstr(err.text, "truncated chunk") != nullptr);

    // A tags chunk (0x2018) claiming one tag but ending right where its record would start.
    static const uint8_t tag_truncated[160] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 32,                 // frame size: 16-byte frame header + 16-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 16,                 // chunk size: header and reserved bytes, no tag record
        [148] = 0x18, [149] = 0x20, // chunk type: tags
        [150] = 1,                  // tag_count
    };

    CHECK(!orb_ase_parse(&a, (orb_span) {tag_truncated, sizeof tag_truncated}, &ase, &err));
    CHECK(strstr(err.text, "truncated chunk") != nullptr);

    // A tags chunk (0x2018) with one full tag record whose "to" reaches past frame_count.
    static const uint8_t tag_out_of_range[180] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 51,                 // frame size: 16-byte frame header + 35-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 35,                 // chunk size: header, reserved, and one tag record
        [148] = 0x18, [149] = 0x20, // chunk type: tags
        [150] = 1,                  // tag_count
        [162] = 1,                  // tag[0].to = 1, but frame_count is 1
    };

    CHECK(!orb_ase_parse(&a, (orb_span) {tag_out_of_range, sizeof tag_out_of_range}, &ase, &err));
    CHECK(strstr(err.text, "out of bounds") != nullptr);

    // A header claiming more than 256 colors.
    static const uint8_t too_many_colors[128] = {
        [4] = 0xE0, [5] = 0xA5, // magic
        [12] = 8,               // color depth: indexed
        [32] = 44,  [33] = 1,   // color_count: 300
    };

    CHECK(!orb_ase_parse(&a, (orb_span) {too_many_colors, sizeof too_many_colors}, &ase, &err));
    CHECK(strstr(err.text, "more than 256 colors") != nullptr);
    return 0;
}

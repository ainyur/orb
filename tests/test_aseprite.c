#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static uint8_t mem[1 << 20];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    orb_error err;

    orb_span file;
    CHECK(orb_os_read_file("tests/fixtures/player.aseprite", &a, &file));
    orb_ase ase;

    CHECK(orb_ase_parse(&a, file, &ase, &err));
    CHECK_EQ(ase.w, 16);
    CHECK_EQ(ase.h, 16);
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

    orb_span pal;
    CHECK(orb_os_read_file("tests/fixtures/palette.aseprite", &a, &pal));
    orb_ase p;

    CHECK(orb_ase_parse(&a, pal, &p, &err));
    CHECK_EQ(p.color_count, 8);
    CHECK_EQ(p.rgb[1][2], 64);
    CHECK_EQ(p.rgb[5][0], 255);

    uint8_t junk[200] = {0};

    CHECK(!orb_ase_parse(&a, (orb_span) {junk, sizeof junk}, &ase, &err));
    CHECK(strstr(err.text, "aseprite") != NULL);
    return 0;
}

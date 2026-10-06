#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static u8 mem[1 << 20];
    arena scratch;

    orb_arena_init(&scratch, "test", mem, sizeof mem);

    // three 8x8 frames: a 2x2 block at (1,1), the same block again, and an empty frame
    u8 frames[3 * 64] = {0};

    frames[1 * 8 + 1] = 5;
    frames[1 * 8 + 2] = 5;
    frames[2 * 8 + 1] = 5;
    frames[2 * 8 + 2] = 6;
    memcpy(frames + 64, frames, 64);

    orb_pack pack;

    orb_pack_frames(&scratch, frames, 3, (orb_size) {8, 8}, &pack);
    CHECK_EQ(pack.rects.len, 2);
    CHECK_EQ(pack.frames[0].rect, 0);
    CHECK_EQ(pack.frames[1].rect, 0);
    CHECK_EQ(pack.frames[2].rect, 1);
    CHECK_EQ(pack.frames[0].ox, 1);
    CHECK_EQ(pack.frames[0].oy, 1);
    CHECK_EQ(pack.rects.elems[0].width, 2);
    CHECK_EQ(pack.rects.elems[0].height, 2);
    CHECK_EQ(pack.rects.elems[1].width, 0);
    CHECK_EQ(pack.sheet_width, 256);
    CHECK_EQ(pack.sheet_height, 2);

    const orb_pack_rect* rect = &pack.rects.elems[0];

    CHECK_EQ(pack.pixels[rect->y * pack.sheet_width + rect->x], 5);
    CHECK_EQ(pack.pixels[(rect->y + 1) * pack.sheet_width + rect->x + 1], 6);

    u8* big = orb_arena_push(&scratch, 300 * 256, 1);
    for (int i = 0; i < 300; i++)
        memset(big + i * 256, 1 + i % 250, 256);

    orb_pack_frames(&scratch, big, 300, (orb_size) {16, 16}, &pack);

    CHECK_EQ(pack.rects.len, 250);
    CHECK_EQ(pack.sheet_width, 256);
    CHECK_EQ(pack.sheet_height, 16 * 16); // 250 rects, 16 per shelf, 16 shelves
    CHECK_EQ(pack.rects.elems[16].x, 0);
    CHECK_EQ(pack.rects.elems[16].y, 16);

    return 0;
}

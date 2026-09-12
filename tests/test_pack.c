#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static uint8_t mem[1 << 20];
    orb_arena a;

    orb_arena_init(&a, "test", mem, sizeof mem);

    // three 8x8 frames: a 2x2 block at (1,1), the same block again, and an empty frame
    uint8_t frames[3 * 64] = {0};

    frames[1 * 8 + 1] = 5;
    frames[1 * 8 + 2] = 5;
    frames[2 * 8 + 1] = 5;
    frames[2 * 8 + 2] = 6;
    memcpy(frames + 64, frames, 64);

    orb_pack pack;

    orb_pack_frames(&a, frames, 3, (orb_size) {8, 8}, &pack);
    CHECK_EQ(pack.rect_count, 2);
    CHECK_EQ(pack.frames[0].rect, 0);
    CHECK_EQ(pack.frames[1].rect, 0);
    CHECK_EQ(pack.frames[2].rect, 1);
    CHECK_EQ(pack.frames[0].ox, 1);
    CHECK_EQ(pack.frames[0].oy, 1);
    CHECK_EQ(pack.rects[0].w, 2);
    CHECK_EQ(pack.rects[0].h, 2);
    CHECK_EQ(pack.rects[1].w, 0);
    CHECK_EQ(pack.sheet_w, 256);
    CHECK_EQ(pack.sheet_h, 2);

    const orb_pack_rect* r = &pack.rects[0];

    CHECK_EQ(pack.pixels[r->y * pack.sheet_w + r->x], 5);
    CHECK_EQ(pack.pixels[(r->y + 1) * pack.sheet_w + r->x + 1], 6);

    uint8_t* big = orb_arena_push(&a, 300 * 256, 1);
    for (int f = 0; f < 300; f++)
        memset(big + f * 256, 1 + f % 250, 256);

    orb_pack_frames(&a, big, 300, (orb_size) {16, 16}, &pack);

    CHECK_EQ(pack.rect_count, 250);
    CHECK_EQ(pack.sheet_w, 256);
    CHECK_EQ(pack.sheet_h, 16 * 16); // 250 rects, 16 per shelf, 16 shelves
    CHECK_EQ(pack.rects[16].x, 0);
    CHECK_EQ(pack.rects[16].y, 16);

    return 0;
}

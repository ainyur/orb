#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"
#include "fixtures/game.c"

#define W 64
#define H 32
#define BACKGROUND 0x202040u // master index 1, what the fixture clears to
#define RED 0xff0000u        // walk frame 0: an 8x8 body at (4,4) of the 16x16 sprite
#define GREEN 0x00ff00u      // walk frame 1: an 8x8 body at (6,4)

static uint32_t pixel(int x, int y) {
    return orb_os_headless_frame()[y * W + x];
}

static bool find(uint32_t color, int* x, int* y) {
    for (*y = 0; *y < H; (*y)++)
        for (*x = 0; *x < W; (*x)++)
            if (pixel(*x, *y) == color) return true;

    return false;
}

int main(void) {
    orb_error err;

    if (!orb_run_boot(orb_game_main(), "tests/fixtures", &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    orb_input in = {0};
    orb_os_headless_set_input(&in);

    CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(0, 0), BACKGROUND);

    int bx, by;
    CHECK(find(RED, &bx, &by));

    CHECK_EQ(pixel(bx + 7, by + 7), RED);
    CHECK_EQ(pixel(bx - 1, by), BACKGROUND);
    CHECK_EQ(pixel(bx + 8, by), BACKGROUND);

    in.down[ORB_BTN_RIGHT] = true;
    orb_os_headless_set_input(&in);

    for (int i = 0; i < 10; i++) CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(bx + 10, by), BACKGROUND);
    CHECK_EQ(pixel(bx + 12, by), GREEN);
    CHECK_EQ(pixel(bx + 19, by), GREEN);
    CHECK_EQ(pixel(bx + 20, by), BACKGROUND);

    in.down[ORB_BTN_RIGHT] = false;
    in.down[ORB_BTN_LEFT] = true;
    orb_os_headless_set_input(&in);

    CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(bx + 7, by), GREEN);
    CHECK_EQ(pixel(bx + 14, by), GREEN);
    CHECK_EQ(pixel(bx + 15, by), BACKGROUND);

    CHECK(orb_run_recast(&err));

    orb_run_draw();

    CHECK_EQ(pixel(bx + 7, by), GREEN);

    orb_os_close();

    return 0;
}

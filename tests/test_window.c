#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"
#include "fixtures/game.c"

static int test_parse(void) {
    orb_size size;

    CHECK(orb_window_parse("1280x720", &size));
    CHECK_EQ(size.width, 1280);
    CHECK_EQ(size.height, 720);
    CHECK(orb_window_parse("8192x8192", &size));
    CHECK(orb_window_parse("1x1", &size));

    static const char* const refused[] = {
        "1280",      "x720",     "1280x",     "0x720",     "1280X720", "1280x720x2",
        "99999x720", "8193x720", "+1280x720", "1280x-720", "",         " 1280x720",
    };

    for (usize i = 0; i < sizeof refused / sizeof refused[0]; i++)
        CHECK(!orb_window_parse(refused[i], &size));

    return 0;
}

static int test_open_size(void) {
    orb_size fb = {320, 180}, screen = {1920, 1080};
    orb_size asked = orb_os_open_size(fb, (orb_size) {1280, 720}, screen);
    orb_size wide = orb_os_open_size(fb, (orb_size) {2560, 720}, screen);
    orb_size plain = orb_os_open_size(fb, (orb_size) {}, screen);
    orb_size small = orb_os_open_size(fb, (orb_size) {}, (orb_size) {800, 600});

    CHECK(asked.width == 1280 && asked.height == 720);
    CHECK(wide.width == 1920 && wide.height == 720);  // clamped to the screen on one side only
    CHECK(plain.width == 960 && plain.height == 540); // 3 times
    CHECK(small.width == 640 && small.height == 360); // 2 times, the most that fits
    return 0;
}

static int test_boot(void) {
    orb_error err;

    // a --window equal to the game's 64 by 32 opens at exactly that size
    CHECK(orb_boot(orb_game_main(), "tests/fixtures", (u8_span) {}, (orb_size) {64, 32}, &err));
    CHECK_EQ(orb_os_headless_window().width, 64);
    CHECK_EQ(orb_os_headless_window().height, 32);
    orb_quit();

    // no --window, and the fixture's orb.json has none: 3 times on the headless 1920 by 1080
    CHECK(orb_boot(orb_game_main(), "tests/fixtures", (u8_span) {}, (orb_size) {}, &err));
    CHECK_EQ(orb_os_headless_window().width, 192);
    CHECK_EQ(orb_os_headless_window().height, 96);
    orb_quit();

    // a request wider than the headless screen is clamped to it
    CHECK(orb_boot(orb_game_main(), "tests/fixtures", (u8_span) {}, (orb_size) {4000, 64}, &err));
    CHECK_EQ(orb_os_headless_window().width, 1920);
    CHECK_EQ(orb_os_headless_window().height, 64);
    orb_quit();

    CHECK(!orb_boot(orb_game_main(), "tests/fixtures", (u8_span) {}, (orb_size) {32, 16}, &err));
    CHECK(strcmp(err.text, "--window 32x16 is smaller than the game's 64x32") == 0);
    return 0;
}

int main(void) {
    CHECK_EQ(test_parse(), 0);
    CHECK_EQ(test_open_size(), 0);
    CHECK_EQ(test_boot(), 0);
    return 0;
}

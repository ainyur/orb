#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"
#include <sys/stat.h>

// A game dir the test can rewrite, borrowing the example's art.
#define DIR "build/scratch/manifest"
#define ART "\"../../../examples/hello/art/"

static orb_config test_config(void) {
    return (orb_config) {.state_size = 16, .state_version = 1};
}

static void test_noop(void* state, const orb_api* orb) {
    (void)state, (void)orb;
}

static const orb_game test_game = {test_config, test_noop, test_noop, test_noop, test_noop};

static bool write_manifest(const char* size, const char* sprites) {
    char text[512];
    int n = snprintf(
        text, sizeof text,
        "{\"id\": \"m\", \"name\": \"m\", \"size\": %s, \"asset_headroom\": 1048576,\n"
        " \"palette\": " ART "palette.aseprite\", \"sprites\": [%s]}\n",
        size, sprites
    );

    return orb_os_write_file(DIR "/game.json", (orb_span) {(uint8_t*)text, (size_t)n});
}

int main(void) {
    mkdir(DIR, 0777);
    CHECK(write_manifest("[64, 32]", ART "player.aseprite\""));

    orb_error err;

    if (!orb_run_boot(&test_game, DIR, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    const orb_api* api = orb_api_table();

    api->clear(0);
    api->sprite_draw(NULL, ORB_SPRITE(2), 0, 0, 0, NULL); // one file: two sprites, no third
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);

    // adding a sprite to game.json and recasting picks it up without a restart
    CHECK(write_manifest("[64, 32]", ART "player.aseprite\", " ART "player.aseprite\""));
    CHECK(orb_run_recast(&err));
    CHECK_EQ(orb_run_manifest()->sprite_count, 2);
    api->clear(0);
    api->sprite_draw(NULL, ORB_SPRITE(2), 0, 0, 0, NULL);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2); // frame 0's red body

    // renaming the source behind index 0 (a copy of player as hero) changes the id
    // at that index: the handle the running code holds fails closed until the code
    // reloads against the regenerated header
    orb_span player;
    static alignas(16) uint8_t copy_mem[1 << 16];
    orb_arena copy;

    orb_arena_init(&copy, "copy", copy_mem, sizeof copy_mem);
    CHECK(orb_os_read_file("examples/hello/art/player.aseprite", &copy, &player));
    CHECK(orb_os_write_file(DIR "/hero.aseprite", player));
    CHECK(write_manifest("[64, 32]", "\"hero.aseprite\""));
    CHECK(orb_run_recast(&err));
    api->clear(0);
    api->sprite_draw(NULL, ORB_SPRITE(0), 0, 0, 0, NULL);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);
    orb_run_set_game(&test_game); // the rebuilt code arrives
    api->clear(0);
    api->sprite_draw(NULL, ORB_SPRITE(0), 0, 0, 0, NULL);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2);

    // the window size and the region sizes are fixed at boot: say so instead of
    // silently casting against the old ones
    CHECK(write_manifest("[128, 32]", ART "player.aseprite\""));
    CHECK(!orb_run_recast(&err));
    CHECK(strstr(err.text, "restart") != NULL);
    orb_os_close();
    return 0;
}

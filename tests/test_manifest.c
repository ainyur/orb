#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#define DIR "build/scratch/manifest"
#define ART "\"../../../tests/fixtures/"

static orb_config test_config(void) {
    return (orb_config) {.state_size = 16, .state_version = 1};
}

static void test_noop(void* state, const orb_api* orb) {
    (void)state, (void)orb;
}

static const orb_game test_game = {test_config, test_noop, test_noop, test_noop, test_noop};

static bool write_manifest(const char* size) {
    char text[512];
    int n = snprintf(
        text, sizeof text,
        "{\"id\": \"m\", \"name\": \"m\", \"size\": %s, \"asset_headroom\": 1048576,\n"
        " \"palette\": " ART "art/palette.aseprite\", \"art\": \".\"}\n",
        size
    );

    return orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)text, (size_t)n});
}

static bool copy_player(const char* to) {
    static alignas(16) uint8_t copy_mem[1 << 16];
    orb_arena copy;
    orb_span player;

    orb_arena_init(&copy, "copy", copy_mem, sizeof copy_mem);

    return orb_os_read_file("tests/fixtures/art/player.aseprite", &copy, &player) &&
           orb_os_write_file(to, player);
}

int main(void) {
    CHECK(orb_os_make_dir(DIR));
    remove(DIR "/hero.aseprite");
    remove(DIR "/zed.aseprite");
    CHECK(copy_player(DIR "/player.aseprite"));
    CHECK(write_manifest("[64, 32]"));

    orb_error err;

    if (!orb_run_boot(&test_game, DIR, (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    const orb_api* api = orb_api_table();

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);

    // a file added to the art directory is picked up by a recast without touching orb.json
    CHECK(copy_player(DIR "/zed.aseprite"));
    CHECK(orb_run_recast(&err));

    orb_assets as;

    CHECK(orb_file_load(orb_run_cast_result()->file, &as, &err));
    CHECK_EQ(as.sprite_count, 4); // two frames from each of two files
    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2);

    remove(DIR "/player.aseprite");
    remove(DIR "/zed.aseprite");
    CHECK(copy_player(DIR "/hero.aseprite"));
    CHECK(orb_run_recast(&err));

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);
    CHECK_EQ(api->sprite_find("player", 0).v, ORB_NO_SPRITE.v); // gone by that name

    // finding by the new name, as reload does, yields the live handle
    api->clear(0);
    api->sprite_draw(api->sprite_find("hero", 0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2);

    CHECK(write_manifest("[128, 32]"));
    CHECK(!orb_run_recast(&err));
    CHECK(strstr(err.text, "restart") != nullptr);

    orb_os_close();

    return 0;
}

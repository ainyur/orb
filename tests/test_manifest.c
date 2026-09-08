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

static bool write_manifest(const char* size, const char* sprites) {
    char text[512];
    int n = snprintf(
        text, sizeof text,
        "{\"id\": \"m\", \"name\": \"m\", \"size\": %s, \"asset_headroom\": 1048576,\n"
        " \"palette\": " ART "palette.aseprite\", \"sprites\": [%s]}\n",
        size, sprites
    );

    return orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)text, (size_t)n});
}

int main(void) {
    CHECK(orb_os_make_dir(DIR));
    CHECK(write_manifest("[64, 32]", ART "player.aseprite\""));

    orb_error err;

    if (!orb_run_boot(&test_game, DIR, (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    const orb_api* api = orb_api_table();

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);

    // adding a sprite to orb.json and recasting picks it up without a restart
    CHECK(write_manifest("[64, 32]", ART "player.aseprite\", " ART "player.aseprite\""));
    CHECK(orb_run_recast(&err));
    CHECK_EQ(orb_run_manifest()->sprite_count, 2);
    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2);

    orb_span player;
    static alignas(16) uint8_t copy_mem[1 << 16];
    orb_arena copy;
    orb_arena_init(&copy, "copy", copy_mem, sizeof copy_mem);

    CHECK(orb_os_read_file("tests/fixtures/player.aseprite", &copy, &player));
    CHECK(orb_os_write_file(DIR "/hero.aseprite", player));
    CHECK(write_manifest("[64, 32]", "\"hero.aseprite\""));
    CHECK(orb_run_recast(&err));

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 0);
    CHECK_EQ(api->sprite_find("player", 0).v, ORB_NO_SPRITE.v); // gone by that name

    // finding by the new name, as reload does, yields the live handle
    api->clear(0);
    api->sprite_draw(api->sprite_find("hero", 0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_framebuffer()->px[4 * 64 + 4], 2);

    CHECK(write_manifest("[128, 32]", ART "player.aseprite\""));
    CHECK(!orb_run_recast(&err));
    CHECK(strstr(err.text, "restart") != nullptr);

    orb_os_close();

    return 0;
}

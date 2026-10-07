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

static bool write_manifest(const char* framebuffer) {
    char text[512];
    int n = snprintf(
        text, sizeof text,
        "{\"id\": \"m\", \"name\": \"m\", \"framebuffer\": %s,\n"
        " \"palette\": " ART "art/palette.aseprite\", \"art\": \".\"}\n",
        framebuffer
    );

    return orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)text, (usize)n});
}

static bool copy_player(const char* to) {
    static alignas(16) u8 copy_mem[1 << 16];
    orb_arena copy;
    u8_span player;

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

    // "world" defaults to levels/world.ldtk and can be overridden like art or sfx
    static alignas(16) u8 world_mem[1 << 16];
    orb_arena world_arena;
    orb_arena_init(&world_arena, "world manifest", world_mem, sizeof world_mem);

    orb_manifest manifest;
    CHECK(orb_manifest_load(&world_arena, DIR, &manifest, &err));
    CHECK(strcmp(manifest.world, "levels/world.ldtk") == 0);

    const char* world_override =
        "{\"id\": \"m\", \"name\": \"m\", \"framebuffer\": [64, 32],\n"
        " \"palette\": " ART
        "art/palette.aseprite\", \"art\": \".\", \"world\": \"maps/w.ldtk\"}\n";
    CHECK(
        orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)world_override, strlen(world_override)})
    );
    orb_arena_clear(&world_arena);
    CHECK(orb_manifest_load(&world_arena, DIR, &manifest, &err));
    CHECK(strcmp(manifest.world, "maps/w.ldtk") == 0);

    // "framebuffer" is two integers from 1 to 4096
    CHECK(write_manifest("[64.5, 32]"));
    orb_arena_clear(&world_arena);
    CHECK(!orb_manifest_load(&world_arena, DIR, &manifest, &err));
    CHECK(strcmp(err.text, "orb.json: \"framebuffer\" must be [width, height]") == 0);
    CHECK(write_manifest("[0, 32]"));
    orb_arena_clear(&world_arena);
    CHECK(!orb_manifest_load(&world_arena, DIR, &manifest, &err));
    CHECK(strcmp(err.text, "orb.json: \"framebuffer\" must be within 1..4096") == 0);
    CHECK(write_manifest("[64, 32]"));

    // "window" is optional; when present it is two integers from framebuffer to 8192
    static const struct {
        const char* window;
        const char* error; // nullptr: accepted
    } windows[] = {
        {"[128, 64]", nullptr},
        {"[64, 32]", nullptr},
        {"[32, 64]", "orb.json: \"window\" must be at least \"framebuffer\" and at most 8192"},
        {"[0, 0]", "orb.json: \"window\" must be at least \"framebuffer\" and at most 8192"},
        {"[128, 9000]", "orb.json: \"window\" must be at least \"framebuffer\" and at most 8192"},
        {"5", "orb.json: \"window\" must be [width, height]"},
        {"[128]", "orb.json: \"window\" must be [width, height]"},
        {"[128.5, 64]", "orb.json: \"window\" must be [width, height]"},
        {"[1e300, 64]", "orb.json: \"window\" must be [width, height]"},
    };

    orb_arena_clear(&world_arena);
    CHECK(orb_manifest_load(&world_arena, DIR, &manifest, &err)); // no "window" in this one
    CHECK_EQ(manifest.window.width, 0);
    CHECK_EQ(manifest.window.height, 0);

    for (usize i = 0; i < sizeof windows / sizeof windows[0]; i++) {
        char text[512];
        int n = snprintf(
            text, sizeof text,
            "{\"id\": \"m\", \"name\": \"m\", \"framebuffer\": [64, 32], \"window\": %s,\n"
            " \"palette\": " ART "art/palette.aseprite\", \"art\": \".\"}\n",
            windows[i].window
        );

        CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)text, (usize)n}));
        orb_arena_clear(&world_arena);

        bool loaded = orb_manifest_load(&world_arena, DIR, &manifest, &err);

        CHECK(loaded == !windows[i].error);

        if (windows[i].error) {
            CHECK(strcmp(err.text, windows[i].error) == 0);
        } else {
            CHECK_EQ(manifest.window.width, i == 0 ? 128 : 64);
            CHECK_EQ(manifest.window.height, i == 0 ? 64 : 32);
        }
    }

    // the cast writes the window into the info section and cuts the name to 55 bytes
    const char* boot_manifest =
        "{\"id\": \"m\", \"name\": "
        "\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz01234567\",\n"
        " \"framebuffer\": [64, 32], \"window\": [128, 64],\n"
        " \"palette\": " ART "art/palette.aseprite\", \"art\": \".\"}\n";

    CHECK(
        orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)boot_manifest, strlen(boot_manifest)})
    );

    if (!orb_boot(&test_game, DIR, (u8_span) {}, (orb_size) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    const orb_api* api = orb_api_table();

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_fb()->px[4 * 64 + 4], 0);

    // a file added to the art directory is picked up by a recast without touching orb.json
    CHECK(copy_player(DIR "/zed.aseprite"));
    CHECK(orb_recast(&err));

    orb_assets assets;

    CHECK(orb_file_load(orb_last_cast()->file, &assets, &err));
    CHECK_EQ(assets.info->window_width, 128);
    CHECK_EQ(assets.info->window_height, 64);
    CHECK_EQ(strlen(assets.info->name), 55);
    CHECK_EQ(assets.sprites.len, 4); // two frames from each of two files
    api->clear(0);
    api->sprite_draw(ORB_SPRITE(2), (orb_vec2) {0, 0}, 0, nullptr);
    CHECK_EQ(orb_api_fb()->px[4 * 64 + 4], 2);

    remove(DIR "/player.aseprite");
    remove(DIR "/zed.aseprite");
    CHECK(copy_player(DIR "/hero.aseprite"));
    CHECK(orb_recast(&err));

    api->clear(0);
    api->sprite_draw(ORB_SPRITE(0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_fb()->px[4 * 64 + 4], 0);
    CHECK_EQ(api->sprite_find("player", 0).v, ORB_NO_SPRITE.v); // gone by that name

    // finding by the new name, as reload does, yields the live handle
    api->clear(0);
    api->sprite_draw(api->sprite_find("hero", 0), (orb_vec2) {0, 0}, 0, nullptr);

    CHECK_EQ(orb_api_fb()->px[4 * 64 + 4], 2);

    CHECK(write_manifest("[128, 32]"));
    CHECK(!orb_recast(&err));
    CHECK(strstr(err.text, "restart") != nullptr);

    orb_quit();

    return 0;
}

#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena a;

    orb_arena_init(&a, "test", mem, sizeof mem);

    orb_framebuffer fb;
    orb_framebuffer_init(&fb, &a, 8, 4);
    orb_framebuffer_clear(&fb, 7);

    CHECK_EQ(fb.px[0], 7);
    CHECK_EQ(fb.px[31], 7);

    uint8_t src[6] = {1, 0, 2, 3, 4, 5}; // 3x2
    orb_framebuffer_blit(
        &fb, src, 3, 3, 2, 6, 3, false, false, nullptr
    ); // clipped right and bottom

    CHECK_EQ(fb.px[3 * 8 + 6], 1);
    CHECK_EQ(fb.px[3 * 8 + 7], 7);

    orb_framebuffer_blit(&fb, src, 3, 3, 2, 0, 0, true, false, nullptr);
    CHECK_EQ(fb.px[0], 2);
    CHECK_EQ(fb.px[2], 1);
    CHECK_EQ(fb.px[8], 5);

    uint8_t remap[256];
    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;
    remap[1] = 9;

    orb_framebuffer_blit(&fb, src, 3, 3, 2, 0, 2, false, true, remap);

    CHECK_EQ(fb.px[2 * 8 + 0], 3);
    CHECK_EQ(fb.px[3 * 8 + 0], 9);

    orb_framebuffer_blit(&fb, src, 3, 3, 2, -1, -1, false, false, nullptr); // clipped top-left
                                                                            //
    CHECK_EQ(fb.px[0], 4);

    orb_palette pal;
    uint8_t rgba[256 * 4] = {0};
    rgba[7 * 4 + 0] = 0x12;
    rgba[7 * 4 + 1] = 0x34;
    rgba[7 * 4 + 2] = 0x56;
    orb_palette_load(&pal, rgba);

    CHECK_EQ(pal.base[7], 0x123456);

    orb_palette_set(&pal, 3, 1, 2, 3);

    CHECK_EQ(orb_palette_get(&pal, 3), 0x010203);

    orb_palette_reset(&pal);

    CHECK_EQ(orb_palette_get(&pal, 3), 0);

    uint32_t rgb[32];
    orb_framebuffer_clear(&fb, 7);
    orb_framebuffer_resolve(&fb, pal.live, rgb);

    CHECK_EQ(rgb[31], 0x123456);

    uint8_t pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    orb_sheet_desc sheets[1] = {{.w = 4, .h = 2, .pixels = 0}};
    orb_sprite_desc sprites[2] = {
        {.sheet = 0, .x = 0, .y = 0, .w = 2, .h = 2, .ox = 1, .oy = 1, .fw = 4, .fh = 4},
        {.sheet = 0, .x = 2, .y = 0, .w = 2, .h = 2, .ox = 0, .oy = 0, .fw = 4, .fh = 4}
    };
    orb_animation_desc animations[1] = {{.first_sprite = 0, .first_duration = 0, .count = 2}};
    uint16_t durations[2] = {2, 1};
    orb_assets as = {
        .palette = rgba,
        .sheets = sheets,
        .sheet_count = 1,
        .pixels = pixels,
        .pixel_count = 8,
        .sprites = sprites,
        .sprite_count = 2,
        .animations = animations,
        .animation_count = 1,
        .durations = durations,
        .duration_count = 2
    };

    orb_framebuffer_clear(&fb, 0);
    orb_sprite_draw(
        &fb, &as, nullptr, ORB_SPRITE(0), 1, 0, 0, nullptr
    ); // origin (1,1) -> lands at (2,1)
    CHECK_EQ(fb.px[1 * 8 + 2], 1);
    CHECK_EQ(fb.px[2 * 8 + 3], 6);
    orb_framebuffer_clear(&fb, 0);
    orb_sprite_draw(
        &fb, &as, nullptr, ORB_SPRITE(0), 0, 0, ORB_FLIP_X, nullptr
    ); // x = fw - ox - w = 1
    CHECK_EQ(fb.px[1 * 8 + 1], 2);
    CHECK_EQ(fb.px[1 * 8 + 2], 1);

    orb_camera cam = {.x = 2, .y = 0};

    orb_framebuffer_clear(&fb, 0);
    orb_sprite_draw(&fb, &as, &cam, ORB_SPRITE(1), 4, 0, 0, nullptr); // camera subtracts 2 -> x 2
    CHECK_EQ(fb.px[0 * 8 + 2], 3);
    orb_sprite_draw(&fb, &as, nullptr, ORB_SPRITE(99), 0, 0, 0, nullptr); // invalid handle: nothing

    orb_animation_state st;

    orb_animation_start(&st, ORB_ANIMATION(0));
    CHECK_EQ(orb_animation_step(&as, &st).v, 0);
    CHECK_EQ(orb_animation_step(&as, &st).v, 0);
    CHECK_EQ(orb_animation_step(&as, &st).v, 1);
    CHECK_EQ(orb_animation_step(&as, &st).v, 0);

    st.frame = 5;
    st.ticks = 0;

    CHECK_EQ(orb_animation_step(&as, &st).v, 1);
    CHECK_EQ(st.frame, 0); // frame 1 lasts one tick, so it advanced

    orb_api_init(&a, 8, 4);
    orb_api_set_assets(&as);

    const orb_api* api = orb_api_table();

    api->clear(0);
    api->sprite_draw(nullptr, ORB_SPRITE(0), 1, 0, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[1 * 8 + 2], 1);
    api->palette_set(1, 9, 8, 7);
    CHECK_EQ(api->palette_get(1), 0x090807);
    orb_api_resolve(rgb);
    CHECK_EQ(rgb[1 * 8 + 2], 0x090807);
    api->palette_reset();
    CHECK_EQ(api->palette_get(1), 0);

    // games find assets by name: file stem plus frame number or tag
    uint64_t sprite_ids[2] = {orb_asset_id("player", "0"), orb_asset_id("player", "1")};
    uint64_t animation_ids[1] = {orb_asset_id("player", "walk")};
    as.sprite_ids = sprite_ids;
    as.animation_ids = animation_ids;

    orb_api_set_assets(&as);

    orb_sprite frame1 = api->sprite_find("player", 1);

    CHECK_EQ(frame1.v, 1);
    CHECK_EQ(api->animation_find("player", "walk").v, 0);
    CHECK_EQ(api->sprite_find("nobody", 0).v, ORB_NO_SPRITE.v);
    CHECK_EQ(api->animation_find("player", "run").v, ORB_NO_ANIMATION.v);
    api->clear(0);
    api->sprite_draw(nullptr, ORB_NO_SPRITE, 0, 0, 0, nullptr); // a miss draws nothing
    CHECK_EQ(orb_api_framebuffer()->px[0], 0);
    api->sprite_draw(nullptr, frame1, 0, 0, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[0], 3);

    // a recast that puts a different source at an index bumps that index's
    // generation: the handle the running code still holds fails closed, and
    // finding again in reload yields a handle at the new generation
    uint64_t recast_sprite_ids[2] = {orb_asset_id("player", "0"), orb_asset_id("hero", "0")};
    uint64_t recast_animation_ids[1] = {orb_asset_id("hero", "walk")}; // a new half
    as.sprite_ids = recast_sprite_ids;
    as.animation_ids = recast_animation_ids;

    orb_api_set_assets(&as);

    api->clear(0);
    api->sprite_draw(nullptr, frame1, 0, 0, 0, nullptr); // index 1 changed: nothing
    CHECK_EQ(orb_api_framebuffer()->px[0], 0);
    api->sprite_draw(nullptr, ORB_SPRITE(0), 0, 0, 0, nullptr); // index 0 did not
    CHECK_EQ(orb_api_framebuffer()->px[1 * 8 + 1], 1);
    api->animation_start(&st, ORB_ANIMATION(0));
    CHECK_EQ(api->animation_step(&st).v, 0xffffffu);
    CHECK_EQ(api->sprite_find("player", 1).v, ORB_NO_SPRITE.v);

    orb_sprite hero = api->sprite_find("hero", 0);

    CHECK_EQ(ORB_HANDLE_INDEX(hero), 1);
    CHECK_EQ(ORB_HANDLE_GENERATION(hero), 1);
    api->clear(0);
    api->sprite_draw(nullptr, hero, 0, 0, 0, nullptr);
    CHECK_EQ(orb_api_framebuffer()->px[0], 3);
    api->animation_start(&st, api->animation_find("hero", "walk"));
    CHECK_EQ(api->animation_step(&st).v, 0);

    return 0;
}

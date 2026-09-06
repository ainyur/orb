#include "api.h"
#include "../graphics/palette.h"
#include "../graphics/sprite.h"
#include "input.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static orb_assets api_assets;
static orb_framebuffer api_framebuffer;
static orb_palette api_palette;
static uint8_t api_sprite_generations[ORB_MAX_SPRITES];
static uint8_t api_animation_generations[ORB_MAX_ANIMATIONS];

// A recast that lands a different source at an index bumps that index, so a
// handle found before it fails closed until reload finds the name again.
static void api_bump_changed(
    uint8_t* generations, const uint64_t* old, uint32_t old_count, const uint64_t* new,
    uint32_t new_count
) {
    if (!old || !new) return;

    uint32_t n = old_count < new_count ? old_count : new_count;

    for (uint32_t i = 0; i < n; i++)
        if (old[i] != new[i]) generations[i]++;
}

// The handle at the index whose id matches, carrying that index's current
// generation; 0xffffff (the null handle) on a miss.
static uint32_t
api_find(const uint64_t* ids, uint32_t count, const uint8_t* generations, uint64_t id) {
    for (uint32_t i = 0; i < count; i++)
        if (ids[i] == id) return i | (uint32_t)generations[i] << 24;

    return 0xffffffu;
}

static orb_animation api_animation_find(const char* stem, const char* tag) {
    uint32_t v = api_find(
        api_assets.animation_ids, api_assets.animation_count, api_animation_generations,
        orb_asset_id(stem, tag)
    );

    if (v == 0xffffffu) orb_log("no animation \"%s\" in %s", tag, stem);

    return ORB_ANIMATION(v);
}

static void api_animation_start(orb_animation_state* st, orb_animation a) {
    orb_animation_start(st, a);
}

static orb_sprite api_animation_step(orb_animation_state* st) {
    return orb_animation_step(&api_assets, st);
}

static void api_clear(uint8_t index) {
    orb_framebuffer_clear(&api_framebuffer, index);
}

static uint32_t api_palette_get(int i) {
    return orb_palette_get(&api_palette, i);
}

static void api_palette_reset(void) {
    orb_palette_reset(&api_palette);
}

static void api_palette_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    orb_palette_set(&api_palette, i, r, g, b);
}

static void api_sprite_draw(
    const orb_camera* cam, orb_sprite s, int x, int y, uint32_t flags, const uint8_t* remap
) {
    orb_sprite_draw(&api_framebuffer, &api_assets, cam, s, x, y, flags, remap);
}

static orb_sprite api_sprite_find(const char* stem, int frame) {
    char suffix[16];

    snprintf(suffix, sizeof suffix, "%d", frame);

    uint32_t v = api_find(
        api_assets.sprite_ids, api_assets.sprite_count, api_sprite_generations,
        orb_asset_id(stem, suffix)
    );

    if (v == 0xffffffu) orb_log("no frame %d in %s", frame, stem);

    return ORB_SPRITE(v);
}

static const orb_api api_table = {
    .animation_find = api_animation_find,
    .animation_start = api_animation_start,
    .animation_step = api_animation_step,
    .button_down = orb_button_down,
    .button_pressed = orb_button_pressed,
    .button_released = orb_button_released,
    .clear = api_clear,
    .log = orb_log,
    .palette_get = api_palette_get,
    .palette_reset = api_palette_reset,
    .palette_set = api_palette_set,
    .sprite_draw = api_sprite_draw,
    .sprite_find = api_sprite_find,
};

const orb_framebuffer* orb_api_framebuffer(void) {
    return &api_framebuffer;
}

void orb_api_init(orb_arena* a, int w, int h) {
    orb_framebuffer_init(&api_framebuffer, a, w, h);
}

void orb_api_resolve(uint32_t* rgb) {
    orb_framebuffer_resolve(&api_framebuffer, api_palette.live, rgb);
}

void orb_api_set_assets(const orb_assets* assets) {
    api_bump_changed(
        api_sprite_generations, api_assets.sprite_ids, api_assets.sprite_count, assets->sprite_ids,
        assets->sprite_count
    );
    api_bump_changed(
        api_animation_generations, api_assets.animation_ids, api_assets.animation_count,
        assets->animation_ids, assets->animation_count
    );
    api_assets = *assets;
    api_assets.sprite_generations = api_sprite_generations;
    api_assets.animation_generations = api_animation_generations;
    orb_palette_load(&api_palette, assets->palette);
}

const orb_api* orb_api_table(void) {
    return &api_table;
}

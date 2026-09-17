#include "api.h"
#include "../audio/mixer.h"
#include "../graphics/camera.h"
#include "../graphics/palette.h"
#include "../graphics/sprite.h"
#include "../graphics/text.h"
#include "../graphics/tilemap.h"
#include "../os/os.h"
#include "asset.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>

static orb_asset_table api_assets;
static orb_framebuffer api_framebuffer;
static orb_vec2f api_camera;
static orb_palette api_palette;
static orb_mixer api_mixer = ORB_MIXER_INIT;
static orb_assets api_views[2]; // the mixer reads one; a publish fills the other and swaps
static int api_view;

// Hand the mixer a stable copy of the assets, then wait out any render still
// reading the previous copy so the arena half it came from can be reused. No
// render in flight means no wait; the bound only guards a thread that died.
static void api_publish(void) {
    api_view = 1 - api_view;
    api_views[api_view] = api_assets.assets;

    uint32_t pending_render = orb_mixer_set_assets(&api_mixer, &api_views[api_view]);
    uint64_t start = orb_os_ticks();

    while (pending_render && !orb_mixer_rendered(&api_mixer, pending_render) &&
           orb_os_ticks() - start < 200000000u)
        orb_os_sleep(1000000);
}

static orb_animation api_animation_find(const char* stem, const char* tag) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_ANIMATION, orb_asset_id(stem, tag));

    if (v == ORB_NO_INDEX) orb_log("no animation \"%s\" in %s", tag, stem);

    return ORB_ANIMATION(v);
}

static orb_sprite api_animation_step(orb_animation_state* st) {
    return orb_animation_step(&api_assets.assets, st);
}

static void api_camera_set(orb_vec2f at) {
    api_camera = at;
}

static void api_camera_update(orb_camera* camera) {
    api_camera =
        orb_camera_update(camera, (orb_size) {api_framebuffer.width, api_framebuffer.height});
}

static int api_cell_get(orb_level level, int layer, orb_vec2 at) {
    return orb_tilemap_cell(&api_assets.assets, level, layer, at);
}

static void api_clear(uint8_t index) {
    orb_framebuffer_clear(&api_framebuffer, index);
}

static orb_font api_font_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_FONT, orb_asset_id(stem, "font"));

    if (v == ORB_NO_INDEX) orb_log("no font \"%s\"", stem);

    return ORB_FONT(v);
}

static void api_layer_draw(orb_level level, int layer) {
    orb_tilemap_draw(&api_framebuffer, &api_assets.assets, api_camera, level, layer);
}

// The level desc behind a handle, or nullptr when it is stale.
static const orb_level_desc* api_level(orb_level level) {
    uint32_t index = orb_asset_index_of(&api_assets.assets, level);

    return index == ORB_NO_INDEX ? nullptr : &api_assets.assets.levels[index];
}

static int api_layer_find(orb_level level, const char* name) {
    const orb_level_desc* d = api_level(level);
    uint64_t id = orb_asset_id(name, "");

    for (int i = 0; d && i < d->layer_count; i++)
        if (api_assets.assets.layer_ids[d->first_layer + i] == id) return i;

    if (d) orb_log("no layer \"%s\" in the level", name);

    return (int)ORB_NO_INDEX;
}

static orb_layer_info api_layer_info(orb_level level, int layer) {
    const orb_level_desc* d = api_level(level);

    if (!d || layer < 0 || layer >= d->layer_count) return (orb_layer_info) {};

    const orb_layer_desc* l = &api_assets.assets.layers[d->first_layer + layer];

    return (orb_layer_info) {
        .grid = l->grid,
        .columns = l->columns,
        .rows = l->rows,
        .has_tiles = l->sublayers > 0,
        .has_cells = l->cells != ORB_NO_INDEX
    };
}

static orb_rect api_level_bounds(orb_level level) {
    const orb_level_desc* d = api_level(level);

    if (!d) return (orb_rect) {};

    return (orb_rect) {{d->world_x, d->world_y}, {d->width, d->height}};
}

static orb_level api_level_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_LEVEL, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no level \"%s\"", stem);

    return ORB_LEVEL(v);
}

static int api_level_neighbors(orb_level level, orb_neighbor* out, int max) {
    const orb_level_desc* d = api_level(level);
    int n = 0;

    for (int i = 0; d && i < d->neighbor_count && n < max; i++) {
        const orb_neighbor_desc* link = &api_assets.assets.neighbors[d->first_neighbor + i];
        uint32_t generation = api_assets.assets.level_generations[link->level];

        out[n++] = (orb_neighbor) {ORB_LEVEL(link->level | generation << 24), link->dir};
    }

    return n;
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

static orb_sample api_sample_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SAMPLE, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no sound \"%s\"", stem);

    return ORB_SAMPLE(v);
}

static orb_song api_song_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SONG, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no song \"%s\"", stem);

    return ORB_SONG(v);
}

static void api_song_pause(void) {
    orb_mixer_song_pause(&api_mixer, true);
}

static void api_song_play(orb_song s, bool loop) {
    orb_mixer_song_play(&api_mixer, s, loop);
}

static orb_song_position api_song_position(void) {
    return orb_mixer_song_position(&api_mixer);
}

static void api_song_resume(void) {
    orb_mixer_song_pause(&api_mixer, false);
}

static void api_song_stop(int fade_ms) {
    orb_mixer_song_stop(&api_mixer, fade_ms);
}

static orb_voice api_sound_play(orb_sample s, orb_sound_params p, int priority) {
    return orb_mixer_sound_play(&api_mixer, s, p, priority);
}

static void api_sound_set(orb_voice v, orb_sound_params p) {
    orb_mixer_sound_set(&api_mixer, v, p);
}

static void api_sound_stop(orb_voice v) {
    orb_mixer_sound_stop(&api_mixer, v);
}

static void api_sprite_draw(orb_sprite s, orb_vec2 at, uint32_t flags, const uint8_t* remap) {
    orb_sprite_draw(&api_framebuffer, &api_assets.assets, api_camera, s, at, flags, remap);
}

static orb_sprite api_sprite_find(const char* stem, int frame) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SPRITE, orb_sprite_id(stem, frame));

    if (v == ORB_NO_INDEX) orb_log("no frame %d in %s", frame, stem);

    return ORB_SPRITE(v);
}

static void api_text_draw(orb_font f, const char* s, orb_vec2 at, const uint8_t* remap) {
    orb_text_draw(&api_framebuffer, &api_assets.assets, f, s, at, remap);
}

static orb_size api_text_measure(orb_font f, const char* s) {
    return orb_text_measure(&api_assets.assets, f, s);
}

static void api_volume_set(orb_volumes v) {
    orb_mixer_volume_set(&api_mixer, v);
}

static const orb_api api_table = {
    .animation_find = api_animation_find,
    .animation_start = orb_animation_start,
    .animation_step = api_animation_step,
    .button_down = orb_button_down,
    .button_pressed = orb_button_pressed,
    .button_released = orb_button_released,
    .camera_set = api_camera_set,
    .camera_update = api_camera_update,
    .cell_get = api_cell_get,
    .clear = api_clear,
    .font_find = api_font_find,
    .layer_draw = api_layer_draw,
    .layer_find = api_layer_find,
    .layer_info = api_layer_info,
    .level_bounds = api_level_bounds,
    .level_find = api_level_find,
    .level_neighbors = api_level_neighbors,
    .log = orb_log,
    .palette_get = api_palette_get,
    .palette_reset = api_palette_reset,
    .palette_set = api_palette_set,
    .sample_find = api_sample_find,
    .song_find = api_song_find,
    .song_pause = api_song_pause,
    .song_play = api_song_play,
    .song_position = api_song_position,
    .song_resume = api_song_resume,
    .song_stop = api_song_stop,
    .sound_play = api_sound_play,
    .sound_set = api_sound_set,
    .sound_stop = api_sound_stop,
    .sprite_draw = api_sprite_draw,
    .sprite_find = api_sprite_find,
    .text_draw = api_text_draw,
    .text_measure = api_text_measure,
    .volume_set = api_volume_set,
};

const orb_framebuffer* orb_api_framebuffer(void) {
    return &api_framebuffer;
}

void orb_api_init(orb_arena* a, orb_size size) {
    orb_framebuffer_init(&api_framebuffer, a, size);
}

void orb_api_poll(void) {
    static uint32_t reported, quiet_ticks;

    if (quiet_ticks) quiet_ticks--;
    if (api_mixer.dropped == reported || quiet_ticks) return;

    orb_log("audio: %u commands dropped, the ring is full", api_mixer.dropped - reported);
    reported = api_mixer.dropped;
    quiet_ticks = 60;
}

void orb_api_resolve(uint32_t* rgb) {
    orb_framebuffer_resolve(&api_framebuffer, api_palette.live, rgb);
}

void orb_api_set_assets(const orb_assets* assets) {
    orb_asset_set(&api_assets, assets);
    orb_palette_load(&api_palette, assets->palette);
    api_publish();
}

const orb_api* orb_api_table(void) {
    return &api_table;
}

bool orb_audio_idle(uint64_t elapsed_ns, uint64_t* rendered) {
    return orb_mixer_idle(&api_mixer, elapsed_ns, rendered);
}

void orb_audio_render(int16_t* out, int frames) {
    orb_mixer_render(&api_mixer, out, frames);
}

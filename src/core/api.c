#include "api.h"
#include "../audio/mixer.h"
#include "../graphics/camera.h"
#include "../graphics/pal.h"
#include "../graphics/sprite.h"
#include "../graphics/text.h"
#include "../graphics/tilemap.h"
#include "../os/os.h"
#include "asset.h"
#include "console.h"
#include "entity.h"
#include "input.h"
#include "log.h"
#include "macros.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

static orb_asset_table api_assets;
static orb_fb api_fb;
static orb_vec2f api_camera;
static orb_pal api_pal;
static orb_mixer api_mixer = ORB_MIXER_INIT;
static orb_assets api_views[2]; // the mixer reads one; a publish fills the other and swaps
static int api_view;

static void api_pal_reset(void) {
    orb_pal_reset(&api_pal);
}

static void api_pal_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    orb_pal_set(&api_pal, i, r, g, b);
}

static uint32_t api_pal_get(int i) {
    return orb_pal_get(&api_pal, i);
}

static void api_clear(uint8_t index) {
    orb_fb_clear(&api_fb, index);
}

static void api_camera_set(orb_vec2f at) {
    api_camera = at;
}

static void api_camera_update(orb_camera* camera) {
    api_camera = orb_camera_update(camera, (orb_size) {api_fb.width, api_fb.height});
}

static orb_sprite api_sprite_find(const char* stem, int frame) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SPRITE, orb_sprite_id(stem, frame));

    if (v == ORB_NO_INDEX) orb_log("no frame %d in %s", frame, stem);

    return ORB_SPRITE(v);
}

static void api_sprite_draw(orb_sprite s, orb_vec2 at, uint32_t flags, const uint8_t* remap) {
    orb_sprite_draw(&api_fb, &api_assets.assets, api_camera, s, at, flags, remap);
}

static orb_anim api_anim_find(const char* stem, const char* tag) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_ANIM, orb_asset_id(stem, tag));

    if (v == ORB_NO_INDEX) orb_log("no animation \"%s\" in %s", tag, stem);

    return ORB_ANIM(v);
}

static orb_sprite api_anim_step(orb_anim_state* st) {
    return orb_anim_step(&api_assets.assets, st);
}

static int api_layer_find(orb_level level, const char* name) {
    int layer = orb_tilemap_layer_find(&api_assets.assets, level, name);

    if (layer == (int)ORB_NO_INDEX && orb_asset_index_of(&api_assets.assets, level) != ORB_NO_INDEX)
        orb_log("no layer \"%s\" in the level", name);

    return layer;
}

static orb_layer_info api_layer_info(orb_level level, int layer) {
    return orb_tilemap_layer_info(&api_assets.assets, level, layer);
}

static void api_layer_draw(orb_level level, int layer) {
    orb_tilemap_draw(&api_fb, &api_assets.assets, api_camera, level, layer);
}

static orb_level api_level_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_LEVEL, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no level \"%s\"", stem);

    return ORB_LEVEL(v);
}

static orb_rect api_level_bounds(orb_level level) {
    return orb_tilemap_level_bounds(&api_assets.assets, level);
}

static int api_level_neighbors(orb_level level, orb_level_neighbor* out, int max) {
    return orb_tilemap_level_neighbors(&api_assets.assets, level, out, max);
}

static int api_cell_get(orb_level level, int layer, orb_vec2 at) {
    return orb_tilemap_cell(&api_assets.assets, level, layer, at);
}

static orb_type api_type_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_TYPE, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no entity type \"%s\"", stem);

    return ORB_TYPE(v);
}

static void api_world_draw(int layer) {
    orb_world_draw(&api_fb, api_camera, layer);
}

static orb_font api_font_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_FONT, orb_asset_id(stem, "font"));

    if (v == ORB_NO_INDEX) orb_log("no font \"%s\"", stem);

    return ORB_FONT(v);
}

static orb_size api_text_measure(orb_font f, const char* s) {
    return orb_text_measure(&api_assets.assets, f, s);
}

static void api_text_draw(orb_font f, const char* s, orb_vec2 at, const uint8_t* remap) {
    orb_text_draw(&api_fb, &api_assets.assets, f, s, at, remap);
}

static orb_sample api_sample_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SAMPLE, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no sound \"%s\"", stem);

    return ORB_SAMPLE(v);
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

static orb_song api_song_find(const char* stem) {
    uint32_t v = orb_asset_find(&api_assets, ORB_ASSET_SONG, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no song \"%s\"", stem);

    return ORB_SONG(v);
}

static void api_song_play(orb_song s, bool loop) {
    orb_mixer_song_play(&api_mixer, s, loop);
}

static orb_song_position api_song_position(void) {
    return orb_mixer_song_position(&api_mixer);
}

static void api_song_pause(void) {
    orb_mixer_song_pause(&api_mixer, true);
}

static void api_song_resume(void) {
    orb_mixer_song_pause(&api_mixer, false);
}

static void api_song_stop(int fade_ms) {
    orb_mixer_song_stop(&api_mixer, fade_ms);
}

static void api_volume_set(orb_volumes v) {
    orb_mixer_volume_set(&api_mixer, v);
}

static const orb_api api_table = {
    .pal_reset = api_pal_reset,
    .pal_set = api_pal_set,
    .pal_get = api_pal_get,
    .clear = api_clear,
    .camera_set = api_camera_set,
    .camera_update = api_camera_update,
    .sprite_find = api_sprite_find,
    .sprite_draw = api_sprite_draw,
    .anim_find = api_anim_find,
    .anim_start = orb_anim_start,
    .anim_step = api_anim_step,
    .layer_find = api_layer_find,
    .layer_info = api_layer_info,
    .layer_draw = api_layer_draw,
    .level_find = api_level_find,
    .level_bounds = api_level_bounds,
    .level_neighbors = api_level_neighbors,
    .cell_get = api_cell_get,
    .type_find = api_type_find,
    .type_bind = orb_type_bind,
    .entity_spawn = orb_entity_spawn,
    .entity_despawn = orb_entity_despawn,
    .entity_get = orb_entity_get,
    .entity_add = orb_entity_add,
    .entity_remove = orb_entity_remove,
    .entity_component = orb_entity_component,
    .entity_world_at = orb_entity_world_at,
    .entity_all = orb_entity_all,
    .entity_of_type = orb_entity_of_type,
    .entity_field_count = orb_entity_field_count,
    .entity_field_int = orb_entity_field_int,
    .entity_field_float = orb_entity_field_float,
    .entity_field_bool = orb_entity_field_bool,
    .entity_field_string = orb_entity_field_string,
    .entity_field_point = orb_entity_field_point,
    .entity_field_ref = orb_entity_field_ref,
    .level_spawn = orb_level_spawn,
    .level_despawn = orb_level_despawn,
    .world_collision = orb_world_collision,
    .cell_kind = orb_world_cell_kind,
    .world_gravity = orb_world_gravity,
    .world_update = orb_world_update,
    .world_draw = api_world_draw,
    .remap_set = orb_world_remap_set,
    .query_rect = orb_query_rect,
    .query_circle = orb_query_circle,
    .query_point = orb_query_point,
    .query_ray = orb_query_ray,
    .font_find = api_font_find,
    .text_measure = api_text_measure,
    .text_draw = api_text_draw,
    .sample_find = api_sample_find,
    .sound_play = api_sound_play,
    .sound_set = api_sound_set,
    .sound_stop = api_sound_stop,
    .song_find = api_song_find,
    .song_play = api_song_play,
    .song_position = api_song_position,
    .song_pause = api_song_pause,
    .song_resume = api_song_resume,
    .song_stop = api_song_stop,
    .volume_set = api_volume_set,
    .button_bind = orb_button_bind,
    .button_down = orb_button_down,
    .button_pressed = orb_button_pressed,
    .button_released = orb_button_released,
    .button_source = orb_button_source,
    .key_down = orb_key_down,
    .key_pressed = orb_key_pressed,
    .key_released = orb_key_released,
    .key_pressed_any = orb_key_pressed_any,
    .key_name = orb_key_name,
    .log = orb_log,

    .var_int = orb_console_var_int,
    .var_float = orb_console_var_float,
    .var_bool = orb_console_var_bool,
    .command = orb_console_command,
    .console_open = orb_console_open,
};

const orb_api* orb_api_table(void) {
    return &api_table;
}

const orb_assets* orb_api_assets(void) {
    return &api_assets.assets;
}

const orb_fb* orb_api_fb(void) {
    return &api_fb;
}

const uint32_t* orb_api_pal_base(void) {
    return api_pal.base;
}

orb_vec2f orb_api_camera(void) {
    return api_camera;
}

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

// The mixer needs no step here: it is a static built from ORB_MIXER_INIT, and the
// audio thread it serves is the OS layer's.
void orb_api_boot(orb_arena* a, orb_size size, const orb_assets* assets) {
    orb_fb_init(&api_fb, a, size);
    orb_api_set_assets(assets);
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
    orb_fb_resolve(&api_fb, api_pal.live, rgb);
}

void orb_api_set_assets(const orb_assets* assets) {
    orb_asset_set(&api_assets, assets);
    orb_pal_load(&api_pal, assets->pal);
    api_publish();
}

// Drop the views into the arena the host is about to free, so the next boot's
// asset set compares against nothing.
void orb_api_quit(void) {
    api_assets = (orb_asset_table) {};
    api_fb = (orb_fb) {};
}

bool orb_audio_idle(uint64_t elapsed_ns, uint64_t* rendered) {
    return orb_mixer_idle(&api_mixer, elapsed_ns, rendered);
}

void orb_audio_render(int16_t* out, int frames) {
    orb_mixer_render(&api_mixer, out, frames);
}

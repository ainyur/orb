#include "api.h"
#include "../audio/mixer.h"
#include "../graphics/palette.h"
#include "../graphics/sprite.h"
#include "../os/os.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>

static orb_assets api_assets;
static orb_framebuffer api_framebuffer;
static orb_vec2f api_camera;
static orb_palette api_palette;
static uint8_t api_sprite_generations[ORB_MAX_SPRITES];
static uint8_t api_animation_generations[ORB_MAX_ANIMATIONS];
static uint8_t api_sample_generations[ORB_MAX_SAMPLES];
static uint8_t api_song_generations[ORB_MAX_SONGS];
static orb_mixer api_mixer = ORB_MIXER_INIT;
static orb_assets api_views[2]; // the mixer reads one; a publish fills the other and swaps
static int api_view;

// A recast that lands a different source at an index bumps that index, so a
// handle found before it fails closed until reload finds the name again.
typedef struct api_ids {
    const uint64_t* ids;
    uint32_t count;
} api_ids;

static void api_bump_changed(uint8_t* generations, api_ids old, api_ids new) {
    if (!old.ids || !new.ids) return;

    uint32_t n = orb_min(old.count, new.count);

    for (uint32_t i = 0; i < n; i++)
        if (old.ids[i] != new.ids[i]) generations[i]++;
}

// The handle at the index whose id matches, carrying that index's current
// generation; 0xffffff (the null handle) on a miss.
static uint32_t
api_find(const uint64_t* ids, uint32_t count, const uint8_t* generations, uint64_t id) {
    for (uint32_t i = 0; i < count; i++)
        if (ids[i] == id) return i | (uint32_t)generations[i] << 24;

    return 0xffffffu;
}

// Hand the mixer a stable copy of the assets, then wait out any render still
// reading the previous copy so the arena half it came from can be reused. No
// render in flight means no wait; the bound only guards a thread that died.
static void api_publish(void) {
    api_view = 1 - api_view;
    api_views[api_view] = api_assets;

    uint32_t render = orb_mixer_set_assets(&api_mixer, &api_views[api_view]);
    uint64_t start = orb_os_ticks();

    while (render && !orb_mixer_rendered(&api_mixer, render) && orb_os_ticks() - start < 200000000u)
        orb_os_sleep(1000000);
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

static void api_camera_set(orb_vec2f at) {
    api_camera = at;
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

static orb_sample api_sample_find(const char* stem) {
    uint32_t v = api_find(
        api_assets.sample_ids, api_assets.sample_count, api_sample_generations,
        orb_asset_id(stem, "")
    );

    if (v == 0xffffffu) orb_log("no sound \"%s\"", stem);

    return ORB_SAMPLE(v);
}

static orb_song api_song_find(const char* stem) {
    uint32_t v = api_find(
        api_assets.song_ids, api_assets.song_count, api_song_generations, orb_asset_id(stem, "")
    );

    if (v == 0xffffffu) orb_log("no song \"%s\"", stem);

    return ORB_SONG(v);
}

static void api_song_pause(void) {
    orb_mixer_song_pause(&api_mixer);
}

static void api_song_play(orb_song s, bool loop) {
    orb_mixer_song_play(&api_mixer, s, loop);
}

static orb_song_position api_song_position(void) {
    return orb_mixer_song_position(&api_mixer);
}

static void api_song_resume(void) {
    orb_mixer_song_resume(&api_mixer);
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
    orb_sprite_draw(&api_framebuffer, &api_assets, api_camera, s, at, flags, remap);
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

static void api_volume_set(orb_volumes v) {
    orb_mixer_volume_set(&api_mixer, v);
}

static const orb_api api_table = {
    .animation_find = api_animation_find,
    .animation_start = api_animation_start,
    .animation_step = api_animation_step,
    .button_down = orb_button_down,
    .button_pressed = orb_button_pressed,
    .button_released = orb_button_released,
    .camera_set = api_camera_set,
    .clear = api_clear,
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
    .volume_set = api_volume_set,
};

const orb_framebuffer* orb_api_framebuffer(void) {
    return &api_framebuffer;
}

void orb_api_init(orb_arena* a, orb_size size) {
    orb_framebuffer_init(&api_framebuffer, a, size);
}

void orb_api_poll(void) {
    static uint32_t reported, quiet;

    if (quiet) quiet--;
    if (api_mixer.dropped == reported || quiet) return;

    orb_log("audio: %u commands dropped, the ring is full", api_mixer.dropped - reported);
    reported = api_mixer.dropped;
    quiet = 60;
}

void orb_api_resolve(uint32_t* rgb) {
    orb_framebuffer_resolve(&api_framebuffer, api_palette.live, rgb);
}

void orb_api_set_assets(const orb_assets* assets) {
    api_bump_changed(
        api_sprite_generations, (api_ids) {api_assets.sprite_ids, api_assets.sprite_count},
        (api_ids) {assets->sprite_ids, assets->sprite_count}
    );
    api_bump_changed(
        api_animation_generations, (api_ids) {api_assets.animation_ids, api_assets.animation_count},
        (api_ids) {assets->animation_ids, assets->animation_count}
    );
    api_bump_changed(
        api_sample_generations, (api_ids) {api_assets.sample_ids, api_assets.sample_count},
        (api_ids) {assets->sample_ids, assets->sample_count}
    );
    api_bump_changed(
        api_song_generations, (api_ids) {api_assets.song_ids, api_assets.song_count},
        (api_ids) {assets->song_ids, assets->song_count}
    );
    api_assets = *assets;
    api_assets.sprite_generations = api_sprite_generations;
    api_assets.animation_generations = api_animation_generations;
    api_assets.sample_generations = api_sample_generations;
    api_assets.song_generations = api_song_generations;
    orb_palette_load(&api_palette, assets->palette);
    api_publish();
}

const orb_api* orb_api_table(void) {
    return &api_table;
}

void orb_audio_idle(uint64_t elapsed_ns, uint64_t* rendered) {
    static int16_t silence[ORB_MIXER_CHUNK * ORB_AUDIO_CHANNELS];
    uint64_t owed = elapsed_ns / 1000000000u * ORB_AUDIO_RATE + // split so it never wraps
                    elapsed_ns % 1000000000u * ORB_AUDIO_RATE / 1000000000u;

    for (; *rendered + ORB_MIXER_CHUNK <= owed; *rendered += ORB_MIXER_CHUNK)
        orb_mixer_render(&api_mixer, silence, ORB_MIXER_CHUNK);
}

void orb_audio_render(int16_t* out, int frames) {
    orb_mixer_render(&api_mixer, out, frames);
}

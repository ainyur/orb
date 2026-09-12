#include "api.h"
#include "../audio/mixer.h"
#include "../graphics/palette.h"
#include "../graphics/sprite.h"
#include "../os/os.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static orb_assets api_assets;
static orb_framebuffer api_framebuffer;
static orb_vec2f api_camera;
static orb_palette api_palette;
static uint8_t
    api_generations[ORB_MAX_SPRITES + ORB_MAX_ANIMATIONS + ORB_MAX_SAMPLES + ORB_MAX_SONGS];
static orb_mixer api_mixer = ORB_MIXER_INIT;
static orb_assets api_views[2]; // the mixer reads one; a publish fills the other and swaps
static int api_view;

// One row per handle kind: where its ids, count, and generation table live in
// orb_assets, and this runtime's generations for it.
typedef struct api_kind {
    uint16_t ids, count, generations; // offsetof
    uint8_t* table;
} api_kind;

enum { API_SPRITE, API_ANIMATION, API_SAMPLE, API_SONG };

#define API_KIND(kind, at)                                                                         \
    {offsetof(orb_assets, kind##_ids), offsetof(orb_assets, kind##_count),                         \
     offsetof(orb_assets, kind##_generations), api_generations + (at)}

static const api_kind api_kinds[] = {
    API_KIND(sprite, 0),
    API_KIND(animation, ORB_MAX_SPRITES),
    API_KIND(sample, ORB_MAX_SPRITES + ORB_MAX_ANIMATIONS),
    API_KIND(song, ORB_MAX_SPRITES + ORB_MAX_ANIMATIONS + ORB_MAX_SAMPLES),
};

constexpr int API_KIND_COUNT = sizeof api_kinds / sizeof *api_kinds;

static const uint64_t* api_ids_of(const orb_assets* as, const api_kind* k) {
    return *(const uint64_t* const*)((const char*)as + k->ids);
}

static uint32_t api_count_of(const orb_assets* as, const api_kind* k) {
    return *(const uint32_t*)((const char*)as + k->count);
}

// The handle at the index whose id matches, carrying that index's current
// generation; ORB_NO_INDEX (the null handle) on a miss.
static uint32_t api_find(int kind, uint64_t id) {
    const api_kind* k = &api_kinds[kind];
    const uint64_t* ids = api_ids_of(&api_assets, k);
    uint32_t count = api_count_of(&api_assets, k);

    for (uint32_t i = 0; i < count; i++)
        if (ids[i] == id) return i | (uint32_t)k->table[i] << 24;

    return ORB_NO_INDEX;
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
    uint32_t v = api_find(API_ANIMATION, orb_asset_id(stem, tag));

    if (v == ORB_NO_INDEX) orb_log("no animation \"%s\" in %s", tag, stem);

    return ORB_ANIMATION(v);
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
    uint32_t v = api_find(API_SAMPLE, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no sound \"%s\"", stem);

    return ORB_SAMPLE(v);
}

static orb_song api_song_find(const char* stem) {
    uint32_t v = api_find(API_SONG, orb_asset_id(stem, ""));

    if (v == ORB_NO_INDEX) orb_log("no song \"%s\"", stem);

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
    uint32_t v = api_find(API_SPRITE, orb_sprite_id(stem, frame));

    if (v == ORB_NO_INDEX) orb_log("no frame %d in %s", frame, stem);

    return ORB_SPRITE(v);
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

// A recast that lands a different source at an index bumps that index, so a
// handle found before it fails closed until reload finds the name again.
void orb_api_set_assets(const orb_assets* assets) {
    for (int kind = 0; kind < API_KIND_COUNT; kind++) {
        const api_kind* k = &api_kinds[kind];
        const uint64_t *old = api_ids_of(&api_assets, k), *new = api_ids_of(assets, k);
        uint32_t n = orb_min(api_count_of(&api_assets, k), api_count_of(assets, k));

        for (uint32_t i = 0; old && new && i < n; i++)
            if (old[i] != new[i]) k->table[i]++;
    }

    api_assets = *assets;

    for (int kind = 0; kind < API_KIND_COUNT; kind++)
        *(const uint8_t**)((char*)&api_assets + api_kinds[kind].generations) =
            api_kinds[kind].table;

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

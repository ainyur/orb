#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct orb_size {
    int w, h;
} orb_size;

typedef struct orb_vec2 {
    int x, y;
} orb_vec2;

typedef struct orb_vec2f {
    float x, y;
} orb_vec2f;

typedef struct orb_sound_params {
    float volume, pan; // 0..1, -1..1
    int pitch_cents;   // -2400..2400; 0 is the sample's own rate
} orb_sound_params;

typedef struct orb_volumes {
    float master, song, sound; // 0..1 each, 1 by default
} orb_volumes;

typedef struct orb_song_position {
    float seconds, beats; // into the song's file; both -1 when no song plays
} orb_song_position;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} orb_span;

enum {
    ORB_BTN_UP,
    ORB_BTN_DOWN,
    ORB_BTN_LEFT,
    ORB_BTN_RIGHT,
    ORB_BTN_A,
    ORB_BTN_B,
    ORB_BTN_X,
    ORB_BTN_Y,
    ORB_BTN_L,
    ORB_BTN_R,
    ORB_BTN_START,
    ORB_BTN_SELECT,
    ORB_BTN_COUNT
};

typedef struct {
    uint32_t v;
} orb_animation;

typedef struct orb_animation_state {
    orb_animation animation;
    uint16_t frame, ticks;
} orb_animation_state;

typedef struct {
    uint32_t v;
} orb_sprite;

#define ORB_ANIMATION(i) ((orb_animation) {(uint32_t)(i)})
#define ORB_SPRITE(i) ((orb_sprite) {(uint32_t)(i)})
#define ORB_NO_ANIMATION ORB_ANIMATION(0xffffffu) // what a find that misses returns; plays nothing
#define ORB_NO_SPRITE ORB_SPRITE(0xffffffu)       // draws nothing
#define ORB_HANDLE_INDEX(h) ((h).v & 0xffffffu)   // v = handle index (24 bits) | generation << 24
#define ORB_HANDLE_GENERATION(h) ((h).v >> 24)

typedef struct {
    uint32_t v;
} orb_sample;

typedef struct {
    uint32_t v;
} orb_song;

// Opaque: a game only passes it back. Not an ORB_HANDLE_INDEX handle.
typedef struct {
    uint32_t v;
} orb_voice;

#define ORB_SAMPLE(i) ((orb_sample) {(uint32_t)(i)})
#define ORB_SONG(i) ((orb_song) {(uint32_t)(i)})
#define ORB_NO_SAMPLE ORB_SAMPLE(0xffffffu) // what a find that misses returns; plays nothing
#define ORB_NO_SONG ORB_SONG(0xffffffu)
#define ORB_NO_VOICE                                                                               \
    ((orb_voice) {0xffffu}) // what a play that fails returns; set and stop ignore it

#define ORB_FLIP_X 1u
#define ORB_FLIP_Y 2u
#define ORB_TICK_SECONDS (1.0f / 60)

constexpr int ORB_AUDIO_RATE = 48000;
constexpr int ORB_AUDIO_CHANNELS = 2;

// Reload rules. orb reloads game code and recasts art while the game runs, and
// three rules keep that safe:
//  1. State lives in the struct orb hands you. config() is read once at boot;
//     if state_version or state_size differs after a code reload, orb zeroes
//     the state and calls init instead of continuing on stale bytes.
//  2. No pointer into your library survives a reload: not function pointers,
//     not string literals, not static const tables. Store handles and indices,
//     and re-bind behavior in reload.
//  3. Assets are found by name: sprite_find("player", 0) is frame 0 of
//     player.aseprite, animation_find("player", "walk") its "walk" tag. reload
//     runs at boot after init, after every code reload, and after every art
//     recast, so it is the one place to find things and store the handles.
// A handle is an index plus a generation. A recast that puts different art at
// an index bumps its generation, so a handle found before it draws and plays
// nothing until reload finds it again. A find that misses logs the name and
// returns ORB_NO_SPRITE, ORB_NO_ANIMATION, ORB_NO_SAMPLE, or ORB_NO_SONG.
typedef struct orb_config {
    size_t arena_size;
    size_t state_size;
    uint32_t state_version;
    uint32_t save_version;
    uint32_t max_entities;
} orb_config;

// Audio. sample_find("jump") is jump.wav from the manifest's sounds; song_find("title")
// is title.wav from its songs. sound_play returns a voice handle; when all 16 game voices
// are busy it steals the oldest voice whose priority is at or below the new sound's, or
// returns ORB_NO_VOICE. sound_set and sound_stop on a voice that ended or was stolen do
// nothing. song_play loops on the WAV's own loop points when it has them, else over the
// whole file. song_position is where the mixer's render head is in the song's file, as
// seconds and as beats from the manifest's bpm; it wraps with the loop and freezes on pause.
// The render head runs ahead of the speaker by one device buffer, a few tens of
// milliseconds. Volumes are 0..1 and start at 1. orb_sound_params.volume 0 is silence, so
// a zero-initialized orb_sound_params plays nothing; set .volume explicitly.
typedef struct orb_api {
    orb_animation (*animation_find)(const char* stem, const char* tag);
    void (*animation_start)(orb_animation_state* st, orb_animation a);
    orb_sprite (*animation_step)(orb_animation_state* st);
    bool (*button_down)(int button);
    bool (*button_pressed)(int button);
    bool (*button_released)(int button);
    void (*camera_set)(orb_vec2f at);
    void (*clear)(uint8_t index);
    void (*log)(const char* fmt, ...);
    uint32_t (*palette_get)(int i);
    void (*palette_reset)(void);
    void (*palette_set)(int i, uint8_t r, uint8_t g, uint8_t b);
    orb_sample (*sample_find)(const char* stem);
    orb_song (*song_find)(const char* stem);
    void (*song_pause)(void);
    void (*song_play)(orb_song s, bool loop);
    orb_song_position (*song_position)(void);
    void (*song_resume)(void);
    void (*song_stop)(int fade_ms);
    orb_voice (*sound_play)(orb_sample s, orb_sound_params p, int priority);
    void (*sound_set)(orb_voice v, orb_sound_params p);
    void (*sound_stop)(orb_voice v);
    void (*sprite_draw)(orb_sprite s, orb_vec2 at, uint32_t flags, const uint8_t* remap);
    orb_sprite (*sprite_find)(const char* stem, int frame);
    void (*volume_set)(orb_volumes v);
} orb_api;

typedef struct orb_game {
    orb_config (*config)(void);
    void (*init)(void* state, const orb_api* orb);
    void (*reload)(void* state, const orb_api* orb);
    void (*update)(void* state, const orb_api* orb);
    void (*draw)(void* state, const orb_api* orb);
} orb_game;

const orb_game* orb_game_main(void);

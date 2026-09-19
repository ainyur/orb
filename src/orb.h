#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t v;
} orb_anim;

typedef struct orb_anim_state {
    orb_anim anim;
    uint16_t frame, ticks;
} orb_anim_state;

typedef struct {
    uint32_t v;
} orb_font;

typedef struct orb_layer_info {
    int grid, columns, rows;
    bool has_tiles, has_cells;
} orb_layer_info;

typedef struct {
    uint32_t v;
} orb_level;

typedef struct {
    uint32_t v;
} orb_sample;

typedef struct orb_size {
    int width, height;
} orb_size;

typedef struct {
    uint32_t v;
} orb_song;

typedef struct orb_song_position {
    int32_t ms;         // into the song's file; both -1 when no song plays
    int32_t millibeats; // beats * 1000, from the manifest's bpm
} orb_song_position;

typedef struct orb_sound_params {
    float volume, pan; // 0..1, -1..1
    int pitch_cents;   // -2400..2400; 0 is the sample's own rate
} orb_sound_params;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} orb_span;

typedef struct {
    uint32_t v;
} orb_sprite;

typedef struct orb_vec2 {
    int x, y;
} orb_vec2;

typedef struct orb_vec2f {
    float x, y;
} orb_vec2f;

typedef struct orb_rect {
    orb_vec2 at;
    orb_size size;
} orb_rect;

typedef struct orb_camera {
    orb_vec2f at;     // world position of the framebuffer's top-left
    orb_vec2f target; // world point to center
    float lerp;       // 0 snaps to the target, 1 never moves
    orb_rect bounds;  // zero size means no clamp
    orb_vec2f shake;  // added after the clamp
} orb_camera;

// Opaque: a game only passes it back. Not an ORB_HANDLE_INDEX handle.
typedef struct {
    uint32_t v;
} orb_voice;

typedef struct orb_volumes {
    float master, song, sound; // 0..1 each, 1 by default
} orb_volumes;

typedef enum orb_button {
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
} orb_button;

// Physical positions, as USB HID keyboard usage IDs. A position without an
// enumerator is still a position.
typedef enum orb_key {
    ORB_KEY_NONE = 0,
    ORB_KEY_A = 4,
    ORB_KEY_B,
    ORB_KEY_C,
    ORB_KEY_D,
    ORB_KEY_E,
    ORB_KEY_F,
    ORB_KEY_G,
    ORB_KEY_H,
    ORB_KEY_I,
    ORB_KEY_J,
    ORB_KEY_K,
    ORB_KEY_L,
    ORB_KEY_M,
    ORB_KEY_N,
    ORB_KEY_O,
    ORB_KEY_P,
    ORB_KEY_Q,
    ORB_KEY_R,
    ORB_KEY_S,
    ORB_KEY_T,
    ORB_KEY_U,
    ORB_KEY_V,
    ORB_KEY_W,
    ORB_KEY_X,
    ORB_KEY_Y,
    ORB_KEY_Z,
    ORB_KEY_1 = 30,
    ORB_KEY_2,
    ORB_KEY_3,
    ORB_KEY_4,
    ORB_KEY_5,
    ORB_KEY_6,
    ORB_KEY_7,
    ORB_KEY_8,
    ORB_KEY_9,
    ORB_KEY_0,
    ORB_KEY_RETURN = 40,
    ORB_KEY_ESCAPE,
    ORB_KEY_BACKSPACE,
    ORB_KEY_TAB,
    ORB_KEY_SPACE,
    ORB_KEY_MINUS = 45,
    ORB_KEY_EQUALS,
    ORB_KEY_LEFT_BRACKET,
    ORB_KEY_RIGHT_BRACKET,
    ORB_KEY_BACKSLASH,
    ORB_KEY_NON_US_HASH,
    ORB_KEY_SEMICOLON,
    ORB_KEY_APOSTROPHE,
    ORB_KEY_GRAVE,
    ORB_KEY_COMMA,
    ORB_KEY_PERIOD,
    ORB_KEY_SLASH,
    ORB_KEY_CAPS_LOCK = 57,
    ORB_KEY_F1 = 58,
    ORB_KEY_F2,
    ORB_KEY_F3,
    ORB_KEY_F4,
    ORB_KEY_F5,
    ORB_KEY_F6,
    ORB_KEY_F7,
    ORB_KEY_F8,
    ORB_KEY_F9,
    ORB_KEY_F10,
    ORB_KEY_F11,
    ORB_KEY_F12,
    ORB_KEY_PRINT_SCREEN = 70,
    ORB_KEY_SCROLL_LOCK,
    ORB_KEY_PAUSE,
    ORB_KEY_INSERT,
    ORB_KEY_HOME,
    ORB_KEY_PAGE_UP,
    ORB_KEY_DELETE,
    ORB_KEY_END,
    ORB_KEY_PAGE_DOWN,
    ORB_KEY_RIGHT,
    ORB_KEY_LEFT,
    ORB_KEY_DOWN,
    ORB_KEY_UP,
    ORB_KEY_NUM_LOCK = 83,
    ORB_KEY_KP_DIVIDE,
    ORB_KEY_KP_MULTIPLY,
    ORB_KEY_KP_MINUS,
    ORB_KEY_KP_PLUS,
    ORB_KEY_KP_ENTER,
    ORB_KEY_KP_1,
    ORB_KEY_KP_2,
    ORB_KEY_KP_3,
    ORB_KEY_KP_4,
    ORB_KEY_KP_5,
    ORB_KEY_KP_6,
    ORB_KEY_KP_7,
    ORB_KEY_KP_8,
    ORB_KEY_KP_9,
    ORB_KEY_KP_0,
    ORB_KEY_KP_PERIOD,
    ORB_KEY_NON_US_BACKSLASH = 100,
    ORB_KEY_APPLICATION,
    ORB_KEY_LEFT_CTRL = 224,
    ORB_KEY_LEFT_SHIFT,
    ORB_KEY_LEFT_ALT,
    ORB_KEY_LEFT_GUI,
    ORB_KEY_RIGHT_CTRL,
    ORB_KEY_RIGHT_SHIFT,
    ORB_KEY_RIGHT_ALT,
    ORB_KEY_RIGHT_GUI,
    ORB_KEY_COUNT = 256
} orb_key;

// The keyboard as the OS layer reports it each tick.
typedef struct orb_input {
    bool keys[ORB_KEY_COUNT]; // by position
} orb_input;

// A button's source: a key position, or a pad button once the gamepad lands.
constexpr int ORB_SOURCE_NONE = 0;
constexpr int ORB_SOURCE_PAD = 512;

typedef char orb_key_symbol[24]; // a key's name or one UTF-8 codepoint, NUL-terminated

typedef enum orb_level_dir : uint8_t {
    ORB_LEVEL_N,
    ORB_LEVEL_S,
    ORB_LEVEL_E,
    ORB_LEVEL_W,
    ORB_LEVEL_NE,
    ORB_LEVEL_NW,
    ORB_LEVEL_SE,
    ORB_LEVEL_SW,
    ORB_LEVEL_LOWER,
    ORB_LEVEL_HIGHER,
    ORB_LEVEL_OVERLAP
} orb_level_dir;

typedef struct orb_level_neighbor {
    orb_level level;
    orb_level_dir dir;
} orb_level_neighbor;

#define ORB_ANIM(i) ((orb_anim) {(uint32_t)(i)})
#define ORB_FONT(i) ((orb_font) {(uint32_t)(i)})
#define ORB_LEVEL(i) ((orb_level) {(uint32_t)(i)})
#define ORB_SAMPLE(i) ((orb_sample) {(uint32_t)(i)})
#define ORB_SONG(i) ((orb_song) {(uint32_t)(i)})
#define ORB_SPRITE(i) ((orb_sprite) {(uint32_t)(i)})
#define ORB_HANDLE_INDEX(h) ((h).v & ORB_NO_INDEX) // v = handle index (24 bits) | generation << 24
#define ORB_HANDLE_GEN(h) ((h).v >> 24)

constexpr uint32_t ORB_NO_INDEX = 0xffffffu;
constexpr orb_anim ORB_NO_ANIM = {ORB_NO_INDEX};
constexpr orb_font ORB_NO_FONT = {ORB_NO_INDEX};
constexpr orb_level ORB_NO_LEVEL = {ORB_NO_INDEX};
constexpr orb_sample ORB_NO_SAMPLE = {ORB_NO_INDEX};
constexpr orb_song ORB_NO_SONG = {ORB_NO_INDEX};
constexpr orb_sprite ORB_NO_SPRITE = {ORB_NO_INDEX};
constexpr orb_voice ORB_NO_VOICE = {0xffffu};

constexpr uint32_t ORB_FLIP_X = 1;
constexpr uint32_t ORB_FLIP_Y = 2;

constexpr uint64_t ORB_NS_PER_SECOND = 1000000000;
constexpr int ORB_TICK_RATE = 60; // update calls per second
constexpr float ORB_TICK_SECONDS = 1.0f / ORB_TICK_RATE;

constexpr int ORB_AUDIO_CHANNELS = 2;
constexpr int ORB_AUDIO_RATE = 48000;

// Audio. sample_find("jump") is jump.wav from the manifest's sounds; song_find("title")
// is title.wav from its songs. sound_play returns a voice handle; when all 16 game voices
// are busy it steals the oldest voice whose priority is at or below the new sound's, or
// returns ORB_NO_VOICE. sound_set and sound_stop on a voice that ended or was stolen do
// nothing. song_play loops on the WAV's own loop points when it has them, else over the
// whole file. song_position is where the mixer's render head is in the song's file, as
// milliseconds and as thousandths of a beat; it wraps with the loop and freezes on pause.
// The render head runs ahead of the speaker by one device buffer, a few tens of
// milliseconds. Volumes are 0..1 and start at 1. orb_sound_params.volume 0 is silence, so
// a zero-initialized orb_sound_params plays nothing; set .volume explicitly.
//
// Levels. level_find("cave") is the LDtk level Cave; layer_find(level, "floor") is a layer
// index within it, ORB_NO_INDEX when missing. layer_draw draws one layer in world space under
// the camera; cell_get reads an IntGrid value at a world pixel, 0 outside. camera_update moves
// an orb_camera toward its target, clamps it to its bounds, and calls camera_set with the result.
//
// Text. font_find("body") is body.aseprite from the manifest's fonts. text_draw puts a
// string on the framebuffer in screen space, ignoring the camera; bytes outside 32 to 126
// draw nothing and do not advance. text_measure is the string's pixel width and the font's
// line height, for centring.
//
// Input. A key is a physical position (ORB_KEY_*, HID usage IDs) and a button is a
// binding to one: the manifest's buttons map sets it, button_bind changes it in memory,
// button_source reads it. key_down, key_pressed and key_released read any position, exact
// per tick; key_pressed_any is the lowest position that went down this tick, ORB_KEY_NONE
// when none did, for capturing a binding. key_name is a named key's name ("tab",
// "page_up") or the layout's symbol for a printable one ("m", ","), "" for a position with
// neither; the string is valid until the next call. A recast resets bindings to the
// manifest's, so re-apply your own in reload.
typedef struct orb_api {
    void (*pal_reset)(void);
    void (*pal_set)(int i, uint8_t r, uint8_t g, uint8_t b);
    uint32_t (*pal_get)(int i);

    void (*clear)(uint8_t index);
    void (*camera_set)(orb_vec2f at);
    void (*camera_update)(orb_camera* camera);

    orb_sprite (*sprite_find)(const char* stem, int frame);
    void (*sprite_draw)(orb_sprite s, orb_vec2 at, uint32_t flags, const uint8_t* remap);
    orb_anim (*anim_find)(const char* stem, const char* tag);
    void (*anim_start)(orb_anim_state* st, orb_anim a);
    orb_sprite (*anim_step)(orb_anim_state* st);

    int (*layer_find)(orb_level level, const char* name);
    orb_layer_info (*layer_info)(orb_level level, int layer);
    void (*layer_draw)(orb_level level, int layer);
    orb_level (*level_find)(const char* stem);
    orb_rect (*level_bounds)(orb_level level);
    int (*level_neighbors)(orb_level level, orb_level_neighbor* out, int max);
    int (*cell_get)(orb_level level, int layer, orb_vec2 at);

    orb_font (*font_find)(const char* stem);
    orb_size (*text_measure)(orb_font f, const char* s);
    void (*text_draw)(orb_font f, const char* s, orb_vec2 at, const uint8_t* remap);

    orb_sample (*sample_find)(const char* stem);
    orb_voice (*sound_play)(orb_sample s, orb_sound_params p, int priority);
    void (*sound_set)(orb_voice v, orb_sound_params p);
    void (*sound_stop)(orb_voice v);
    orb_song (*song_find)(const char* stem);
    void (*song_play)(orb_song s, bool loop);
    orb_song_position (*song_position)(void);
    void (*song_pause)(void);
    void (*song_resume)(void);
    void (*song_stop)(int fade_ms);
    void (*volume_set)(orb_volumes v);

    void (*button_bind)(orb_button button, int source);
    bool (*button_down)(orb_button button);
    bool (*button_pressed)(orb_button button);
    bool (*button_released)(orb_button button);
    int (*button_source)(orb_button button);
    bool (*key_down)(int key);
    bool (*key_pressed)(int key);
    bool (*key_released)(int key);
    int (*key_pressed_any)(void);
    const char* (*key_name)(int key);

    void (*log)(const char* fmt, ...);
} orb_api;

// Reload rules. orb reloads game code and recasts art while the game runs, and
// three rules keep that safe:
//  1. State lives in the struct orb hands you. config() is read once at boot;
//     if state_version or state_size differs after a code reload, orb zeroes
//     the state and calls init instead of continuing on stale bytes.
//  2. No pointer into your library survives a reload: not function pointers,
//     not string literals, not static const tables. Store handles and indices,
//     and re-bind behavior in reload.
//  3. Assets are found by name: sprite_find("player", 0) is frame 0 of
//     player.aseprite, anim_find("player", "walk") its "walk" tag. reload
//     runs at boot after init, after every code reload, and after every art
//     recast, so it is the one place to find things and store the handles.
// A handle is an index plus a generation. A recast that puts different art at
// an index bumps its generation, so a handle found before it draws and plays
// nothing until reload finds it again. A find that misses logs the name and
// returns ORB_NO_SPRITE, ORB_NO_ANIM, ORB_NO_SAMPLE, or ORB_NO_SONG.
typedef struct orb_config {
    size_t arena_size;
    size_t state_size;
    uint32_t state_version;
    uint32_t save_version;
    uint32_t max_entities;
} orb_config;

typedef struct orb_game {
    orb_config (*config)(void);
    void (*init)(void* state, const orb_api* orb);
    void (*reload)(void* state, const orb_api* orb);
    void (*update)(void* state, const orb_api* orb);
    void (*draw)(void* state, const orb_api* orb);
} orb_game;

const orb_game* orb_game_main(void);

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct orb_camera {
    float x, y;
} orb_camera;

typedef struct {
    uint8_t* ptr;
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
#define ORB_HANDLE_INDEX(h) ((h).v & 0xffffffu) // v = handle index (24 bits) | generation << 24
#define ORB_HANDLE_GENERATION(h) ((h).v >> 24)

#define ORB_FLIP_X 1u
#define ORB_FLIP_Y 2u
#define ORB_TICK_SECONDS (1.0f / 60)

// Reload rules. orb reloads game code and recasts art while the game runs, and
// three rules keep that safe:
//  1. State lives in the struct orb hands you. config() is read once at boot;
//     if state_version or state_size differs after a code reload, orb zeroes
//     the state and calls init instead of continuing on stale bytes.
//  2. No pointer into your library survives a reload: not function pointers,
//     not string literals, not static const tables. Store handles and indices,
//     and re-bind behavior in reload.
//  3. reload also runs after every asset recast, so it is the one place to
//     re-resolve anything by name.
// A handle is an index plus a generation. A recast that puts different art at
// an index bumps its generation, so a handle from the previous header draws
// and plays nothing until the code is rebuilt against the regenerated header.
typedef struct orb_config {
    size_t arena_size;
    size_t state_size;
    uint32_t state_version;
    uint32_t save_version;
    uint32_t max_entities;
} orb_config;

typedef struct orb_api {
    void (*animation_start)(orb_animation_state* st, orb_animation a);
    orb_sprite (*animation_step)(orb_animation_state* st);
    bool (*button_down)(int button);
    bool (*button_pressed)(int button);
    bool (*button_released)(int button);
    void (*clear)(uint8_t index);
    void (*log)(const char* fmt, ...);
    uint32_t (*palette_get)(int i);
    void (*palette_reset)(void);
    void (*palette_set)(int i, uint8_t r, uint8_t g, uint8_t b);
    void (*sprite_draw)(
        const orb_camera* cam, orb_sprite s, int x, int y, uint32_t flags, const uint8_t* remap
    );
} orb_api;

typedef struct orb_game {
    orb_config (*config)(void);
    void (*init)(void* state, const orb_api* orb);
    void (*reload)(void* state, const orb_api* orb);
    void (*update)(void* state, const orb_api* orb);
    void (*draw)(void* state, const orb_api* orb);
} orb_game;

const orb_game* orb_game_main(void);

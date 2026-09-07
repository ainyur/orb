#include "orb.h"

#define SCREEN_W 320
#define SCREEN_H 180
#define SPRITE 16

#define ACCEL 0.15f
#define MAX_SPEED 3.0f
#define FRICTION 0.9f
#define BOUNCE 0.5f
#define FLASH_SPEED 2.0f
#define FLASH_TICKS 8

#define IDLE_TICKS (3 * 60)
#define IDLE_SPEED_X 2.0f
#define IDLE_SPEED_Y 1.5f

#define BG 1
#define RED 2
#define WHITE 3

typedef struct {
    float x, y, vx, vy;
    bool flip;
    int flash; // ticks left of the red flash after hitting a wall
    int idle;  // ticks since the last input
    orb_animation_state animation;
    orb_sprite sprite;
} game_state;

static orb_config config(void) {
    return (orb_config) {
        .arena_size = 64 << 20,
        .state_size = sizeof(game_state),
        .state_version = 1,
        .save_version = 1,
        .max_entities = 256
    };
}

static void init(void* state, const orb_api* orb) {
    game_state* g = state;

    (void)orb;

    g->x = (SCREEN_W - SPRITE) / 2;
    g->y = (SCREEN_H - SPRITE) / 2;
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;

    g->animation.animation = orb->animation_find("player", "walk");
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float bounce(float* pos, float* vel, float max, float keep) {
    float speed = *vel < 0 ? -*vel : *vel;

    if (*pos < 0) {
        *pos = 0;
        *vel = speed * keep;
    } else if (*pos > max) {
        *pos = max;
        *vel = -speed * keep;
    } else
        return 0;

    return speed;
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;
    int dx = orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT);
    int dy = orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP);
    bool wandering = g->idle >= IDLE_TICKS;

    if (dx || dy) {
        g->idle = 0;
        wandering = false;
    } else if (g->idle < IDLE_TICKS)
        g->idle++;

    if (dx) g->flip = dx < 0;

    if (wandering) {
        if (g->vx == 0 && g->vy == 0) {
            g->vx = g->flip ? -IDLE_SPEED_X : IDLE_SPEED_X;
            g->vy = IDLE_SPEED_Y;
        }
    } else if (dx || dy) {
        g->vx = clampf(g->vx + ACCEL * (float)dx, -MAX_SPEED, MAX_SPEED);
        g->vy = clampf(g->vy + ACCEL * (float)dy, -MAX_SPEED, MAX_SPEED);
    } else {
        g->vx *= FRICTION;
        g->vy *= FRICTION;

        if (g->vx > -0.05f && g->vx < 0.05f) g->vx = 0;
        if (g->vy > -0.05f && g->vy < 0.05f) g->vy = 0;
    }

    g->x += g->vx;
    g->y += g->vy;

    float keep = wandering ? 1.0f : BOUNCE;
    float hit_x = bounce(&g->x, &g->vx, SCREEN_W - SPRITE, keep);
    float hit_y = bounce(&g->y, &g->vy, SCREEN_H - SPRITE, keep);
    float hit = hit_x > hit_y ? hit_x : hit_y;

    if (hit > 0 && (wandering || hit >= FLASH_SPEED)) g->flash = FLASH_TICKS;
    if (g->flash > 0) g->flash--;

    g->sprite = orb->animation_step(&g->animation);
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    uint8_t remap[256];

    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;

    remap[WHITE] = RED;

    orb->clear(BG);
    orb->sprite_draw(
        nullptr, g->sprite, (int)g->x, (int)g->y, g->flip ? ORB_FLIP_X : 0,
        g->flash > 0 ? remap : nullptr
    );
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

#include "orb.h"

typedef struct {
    int x, y;
    bool flip;
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

    g->x = 20;
    g->y = 8;
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;

    g->animation.animation = orb->animation_find("player", "walk");
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;

    if (orb->button_down(ORB_BTN_LEFT)) {
        g->x--;
        g->flip = true;
    }

    if (orb->button_down(ORB_BTN_RIGHT)) {
        g->x++;
        g->flip = false;
    }

    if (orb->button_down(ORB_BTN_UP)) g->y--;
    if (orb->button_down(ORB_BTN_DOWN)) g->y++;

    g->sprite = orb->animation_step(&g->animation);
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    orb->clear(1);
    orb->sprite_draw(NULL, g->sprite, g->x, g->y, g->flip ? ORB_FLIP_X : 0, NULL);
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

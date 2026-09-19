#include "orb.h"

#include <stdlib.h>

typedef struct {
    int x, y;
    bool flip;
    orb_anim_state anim;
    orb_sprite sprite;
    orb_font font;
    orb_level room;
    int floor, collision;
    int32_t speed, ticks;
    float scale;
    bool god;
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
    g->speed = 1;

    orb->var_int("early", &g->ticks, nullptr);
}

static void teleport(void* state, const orb_api* orb, int argc, const char* const* argv) {
    game_state* g = state;

    (void)orb;

    if (argc != 3) return;

    g->x = atoi(argv[1]);
    g->y = atoi(argv[2]);
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;

    g->anim.anim = orb->anim_find("player", "walk");
    g->font = orb->font_find("body");
    g->room = orb->level_find("room");
    g->floor = orb->layer_find(g->room, "floor");
    g->collision = orb->layer_find(g->room, "collision");

    orb->var_int("speed", &g->speed, "walk speed");
    orb->var_float("scale", &g->scale, nullptr);
    orb->var_bool("god", &g->god, "no collision");
    orb->var_int("ticks", &g->ticks, "updates so far");
    orb->command("teleport", teleport, "teleport <x> <y>");
}

static bool blocked(const orb_api* orb, const game_state* g, int x, int y) {
    orb_vec2 corners[4] = {{x + 4, y + 4}, {x + 11, y + 4}, {x + 4, y + 11}, {x + 11, y + 11}};

    for (int i = 0; i < 4; i++)
        if (orb->cell_get(g->room, g->collision, corners[i]) == 1) return true;

    return false;
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;

    g->ticks++;

    if (orb->button_down(ORB_BTN_LEFT) && !blocked(orb, g, g->x - 1, g->y)) {
        g->x--;
        g->flip = true;
    }

    if (orb->button_down(ORB_BTN_RIGHT) && !blocked(orb, g, g->x + 1, g->y)) {
        g->x++;
        g->flip = false;
    }

    if (orb->button_down(ORB_BTN_UP) && !blocked(orb, g, g->x, g->y - 1)) g->y--;
    if (orb->button_down(ORB_BTN_DOWN) && !blocked(orb, g, g->x, g->y + 1)) g->y++;

    g->sprite = orb->anim_step(&g->anim);
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    orb->clear(1);
    orb->layer_draw(g->room, g->floor);
    orb->sprite_draw(g->sprite, (orb_vec2) {g->x, g->y}, g->flip ? ORB_FLIP_X : 0, nullptr);
    orb->text_draw(g->font, "AB", (orb_vec2) {0, 0}, nullptr);
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

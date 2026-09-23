#include "orb.h"

#include <stdlib.h>

typedef struct {
    orb_entity_id player;
    orb_type player_type;
    orb_anim walk;
    bool flip;
    orb_font font;
    orb_level room;
    int floor;
    int32_t speed, ticks;
    float scale;
    bool god;
} game_state;

static orb_config config(void) {
    return (orb_config) {
        .state_size = sizeof(game_state), .state_version = 1, .save_version = 1, .max_entities = 256
    };
}

static void init(void* state, const orb_api* orb) {
    game_state* g = state;

    g->speed = 1;

    orb->var_int("early", &g->ticks, nullptr);
}

static void teleport(void* state, const orb_api* orb, int argc, const char* const* argv) {
    game_state* g = state;
    orb_entity* e = orb->entity_get(g->player);

    if (argc != 3 || !e) return;

    e->at = (orb_vec2f) {(float)atoi(argv[1]), (float)atoi(argv[2])};
}

// The player's sprite is the walk animation drawn 4 pixels up and left of its 8x8 body.
static void player_init(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* g = state;
    orb_sprite_component* sc = orb->entity_add(id, ORB_COMPONENT_SPRITE);

    orb->anim_start(&sc->anim, g->walk);
    sc->offset = (orb_vec2) {-4, -4};
    orb->entity_add(id, ORB_COMPONENT_BODY);
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;
    uint8_t kinds[256] = {0};

    kinds[1] = ORB_CELL_SOLID;
    g->walk = orb->anim_find("player", "walk");
    g->font = orb->font_find("body");
    g->room = orb->level_find("room");
    g->floor = orb->layer_find(g->room, "floor");
    g->player_type = orb->type_find("player");
    orb->type_bind(g->player_type, player_init, nullptr);
    orb->world_collision("collision", kinds);

    if (!orb->entity_get(g->player)) {
        orb->level_spawn(g->room);
        orb->entity_of_type(g->player_type, &g->player, 1);
    }

    orb->var_int("speed", &g->speed, "walk speed");
    orb->var_float("scale", &g->scale, nullptr);
    orb->var_bool("god", &g->god, "no collision");
    orb->var_int("ticks", &g->ticks, "updates so far");
    orb->command("teleport", teleport, "teleport <x> <y>");
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;
    orb_entity* e = orb->entity_get(g->player);
    orb_body* b = orb->entity_component(g->player, ORB_COMPONENT_BODY);
    orb_sprite_component* sc = orb->entity_component(g->player, ORB_COMPONENT_SPRITE);
    int dx = orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT);
    int dy = orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP);

    g->ticks++;

    if (dx) g->flip = dx < 0;

    b->velocity = (orb_vec2f) {(float)(g->speed * dx), (float)(g->speed * dy)};
    b->box.size = g->god ? (orb_size) {} : e->size; // an empty box collides with nothing
    sc->flags = g->flip ? ORB_FLIP_X : 0;
    orb->world_update();
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    orb->clear(1);
    orb->layer_draw(g->room, g->floor);
    orb->world_draw(0);
    orb->text_draw(g->font, "AB", (orb_vec2) {0, 0}, nullptr);
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

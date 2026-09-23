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
    game_state* self = state;

    self->speed = 1;

    orb->var_int("early", &self->ticks, nullptr);
}

static void teleport(void* state, const orb_api* orb, int argc, const char* const* argv) {
    game_state* self = state;
    orb_entity* entity = orb->entity_get(self->player);

    if (argc != 3 || !entity) return;

    entity->at = (orb_vec2f) {(float)atoi(argv[1]), (float)atoi(argv[2])};
}

// The player's sprite is the walk animation drawn 4 pixels up and left of its 8x8 body.
static void player_init(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* self = state;
    orb_sprite_component* sprite = orb->entity_add(id, ORB_COMPONENT_SPRITE);

    orb->anim_start(&sprite->anim, self->walk);
    sprite->offset = (orb_vec2) {-4, -4};
    orb->entity_add(id, ORB_COMPONENT_BODY);
}

static void reload(void* state, const orb_api* orb) {
    game_state* self = state;
    uint8_t kinds[256] = {0};

    kinds[1] = ORB_CELL_SOLID;
    self->walk = orb->anim_find("player", "walk");
    self->font = orb->font_find("body");
    self->room = orb->level_find("room");
    self->floor = orb->layer_find(self->room, "floor");
    self->player_type = orb->type_find("player");
    orb->type_bind(self->player_type, player_init, nullptr);
    orb->world_collision("collision", kinds);

    if (!orb->entity_get(self->player)) {
        orb->level_spawn(self->room);
        orb->entity_of_type(self->player_type, &self->player, 1);
    }

    orb->var_int("speed", &self->speed, "walk speed");
    orb->var_float("scale", &self->scale, nullptr);
    orb->var_bool("god", &self->god, "no collision");
    orb->var_int("ticks", &self->ticks, "updates so far");
    orb->command("teleport", teleport, "teleport <x> <y>");
}

static void update(void* state, const orb_api* orb) {
    game_state* self = state;
    orb_entity* entity = orb->entity_get(self->player);
    orb_body* body = orb->entity_component(self->player, ORB_COMPONENT_BODY);
    orb_sprite_component* sprite = orb->entity_component(self->player, ORB_COMPONENT_SPRITE);
    int dx = orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT);
    int dy = orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP);

    self->ticks++;

    if (dx) self->flip = dx < 0;

    body->velocity = (orb_vec2f) {(float)(self->speed * dx), (float)(self->speed * dy)};
    body->box.size = self->god ? (orb_size) {} : entity->size; // an empty box collides with nothing
    sprite->flags = self->flip ? ORB_FLIP_X : 0;
    orb->world_update();
}

static void draw(void* state, const orb_api* orb) {
    game_state* self = state;

    orb->clear(1);
    orb->layer_draw(self->room, self->floor);
    orb->world_draw(0);
    orb->text_draw(self->font, "AB", (orb_vec2) {0, 0}, nullptr);
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

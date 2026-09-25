#include "orb.h"

#define SCREEN_W 320
#define BODY 8 // the player's body; its 16x16 frame is drawn 4 pixels up and left of it

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
    orb_entity_id player;
    orb_type player_type;
    orb_anim walk;
    bool flip;
    int flash; // ticks left of the red flash after hitting a wall
    int idle;  // ticks since the last input
    orb_sample bounce;
    orb_level room;
    int floor, walls, shadow;
    orb_camera camera;
    orb_font font;
    bool paused; // no call reports whether the song is playing
    bool hint;
} game_state;

static orb_config config(void) {
    return (orb_config) {.state_size = sizeof(game_state), .state_version = 1, .save_version = 1};
}

static void init(void* state, const orb_api* orb) {
    game_state* demo = state;

    demo->camera.lerp = 0.85f;
    demo->hint = true;
    orb->song_play(orb->song_find("song"), true);

    int music_key = orb->key_find("m");

    if (music_key != ORB_KEY_NONE) orb->button_bind(ORB_BTN_SELECT, music_key);
}

static float clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static float absf(float value) {
    return value < 0 ? -value : value;
}

static void player_init(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* demo = state;
    orb_sprite_component* sprite = orb->entity_add(id, ORB_COMPONENT_SPRITE);

    orb->anim_start(&sprite->anim, demo->walk);
    sprite->offset = (orb_vec2) {-4, -4};
    orb->entity_add(id, ORB_COMPONENT_BODY);
}

// Input, the idle wander, and the bounce off whatever the last update hit: the sweep keeps
// the pre-hit speed in impact, so a hard hit reverses and a soft one rests flush.
static void player_update(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* demo = state;
    orb_body* body = orb_body_of(orb, id);
    orb_sprite_component* sprite = orb_sprite_component_of(orb, id);
    orb_vec2f stick = orb->pad_stick(ORB_PAD_LEFT_STICK);
    float dx =
        clampf(stick.x + orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT), -1, 1);
    float dy =
        clampf(stick.y + orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP), -1, 1);
    bool wandering = demo->idle >= IDLE_TICKS;
    float keep = wandering ? 1.0f : BOUNCE;
    float hit =
        absf(body->impact.x) > absf(body->impact.y) ? absf(body->impact.x) : absf(body->impact.y);

    if (body->impact.x != 0)
        body->velocity.x =
            absf(body->impact.x) >= FLASH_SPEED || wandering ? -body->impact.x * keep : 0;
    if (body->impact.y != 0)
        body->velocity.y =
            absf(body->impact.y) >= FLASH_SPEED || wandering ? -body->impact.y * keep : 0;

    if (dx || dy) {
        demo->idle = 0;
        demo->hint = false;
        wandering = false;
    } else if (demo->idle < IDLE_TICKS)
        demo->idle++;

    if (dx) demo->flip = dx < 0;

    if (wandering) {
        if (body->velocity.x == 0 && body->velocity.y == 0) {
            body->velocity.x = demo->flip ? -IDLE_SPEED_X : IDLE_SPEED_X;
            body->velocity.y = IDLE_SPEED_Y;
        }
    } else if (dx || dy) {
        body->velocity.x = clampf(body->velocity.x + ACCEL * dx, -MAX_SPEED, MAX_SPEED);
        body->velocity.y = clampf(body->velocity.y + ACCEL * dy, -MAX_SPEED, MAX_SPEED);
    } else {
        body->velocity.x *= FRICTION;
        body->velocity.y *= FRICTION;

        if (absf(body->velocity.x) < 0.05f) body->velocity.x = 0;
        if (absf(body->velocity.y) < 0.05f) body->velocity.y = 0;
    }

    if (hit > 0 && (wandering || hit >= FLASH_SPEED)) {
        orb_vec2f at = orb->entity_world_at(id);
        float pan = (at.x - demo->camera.at.x + BODY / 2) / (SCREEN_W / 2.0f) - 1;

        demo->flash = FLASH_TICKS;
        orb->sound_play(demo->bounce, (orb_sound_params) {.volume = 0.8f, .pan = pan}, 0);
    }

    if (demo->flash > 0) demo->flash--;

    sprite->flags = demo->flip ? ORB_FLIP_X : 0;
    sprite->remap = demo->flash > 0 ? 0 : -1;
}

static void reload(void* state, const orb_api* orb) {
    game_state* demo = state;
    uint8_t kinds[256] = {0};
    uint8_t remap[256];
    const char* room = "room";

    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;

    remap[WHITE] = RED;
    kinds[1] = ORB_CELL_SOLID;
    demo->walk = orb->anim_find("player", "walk");
    demo->bounce = orb->sample_find("bounce");
    demo->room = orb->level_find(room);
    demo->floor = orb->layer_find(demo->room, "floor");
    demo->walls = orb->layer_find(demo->room, "walls");
    demo->shadow = orb->layer_find(demo->room, "shadow");
    demo->camera.bounds = orb->level_bounds(demo->room);
    demo->font = orb->font_find("body");
    demo->player_type = orb->type_find("player");
    orb->type_bind(demo->player_type, player_init, player_update);
    orb->world_collision("walls", kinds);
    orb->remap_set(0, remap);

    if (!orb->entity_get(demo->player)) {
        orb->level_spawn(demo->room);

        if (orb->entity_of_type(demo->player_type, &demo->player, 1) == 0)
            orb->log("no player placed in %s", room);
    }
}

static void update(void* state, const orb_api* orb) {
    game_state* demo = state;

    if (orb->button_pressed(ORB_BTN_SELECT)) {
        demo->hint = false;
        demo->paused = !demo->paused;

        if (demo->paused)
            orb->song_pause();
        else
            orb->song_resume();
    }

    orb->world_update();

    orb_vec2f at = orb->entity_world_at(demo->player);

    demo->camera.target = (orb_vec2f) {at.x + BODY / 2, at.y + BODY / 2};
    demo->camera = orb->camera_update(demo->camera);
}

static void draw(void* state, const orb_api* orb) {
    game_state* demo = state;

    orb->clear(BG);
    orb->layer_draw(demo->room, demo->floor);
    orb->layer_draw(demo->room, demo->walls);
    orb->layer_draw(demo->room, demo->shadow);
    orb->world_draw(0);

    if (demo->hint) {
        const char* hint = "Arrows to move and 'M' toggles music";

        orb->text_draw(
            demo->font, hint,
            (orb_vec2) {(SCREEN_W - orb->text_measure(demo->font, hint).width) / 2, 16}, nullptr
        );
    }
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

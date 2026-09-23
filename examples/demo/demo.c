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
    game_state* g = state;

    g->camera.lerp = 0.85f;
    g->hint = true;
    orb->song_play(orb->song_find("song"), true);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float absf(float v) {
    return v < 0 ? -v : v;
}

static void player_init(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* g = state;
    orb_sprite_component* sc = orb->entity_add(id, ORB_COMPONENT_SPRITE);

    orb->anim_start(&sc->anim, g->walk);
    sc->offset = (orb_vec2) {-4, -4};
    orb->entity_add(id, ORB_COMPONENT_BODY);
}

// Input, the idle wander, and the bounce off whatever the last update hit: the sweep keeps
// the pre-hit speed in impact, so a hard hit reverses and a soft one rests flush.
static void player_update(void* state, const orb_api* orb, orb_entity_id id) {
    game_state* g = state;
    orb_body* b = orb_body_of(orb, id);
    orb_sprite_component* sc = orb_sprite_component_of(orb, id);
    int dx = orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT);
    int dy = orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP);
    bool wandering = g->idle >= IDLE_TICKS;
    float keep = wandering ? 1.0f : BOUNCE;
    float hit = absf(b->impact.x) > absf(b->impact.y) ? absf(b->impact.x) : absf(b->impact.y);

    if (b->impact.x != 0)
        b->velocity.x = absf(b->impact.x) >= FLASH_SPEED || wandering ? -b->impact.x * keep : 0;
    if (b->impact.y != 0)
        b->velocity.y = absf(b->impact.y) >= FLASH_SPEED || wandering ? -b->impact.y * keep : 0;

    if (dx || dy) {
        g->idle = 0;
        g->hint = false;
        wandering = false;
    } else if (g->idle < IDLE_TICKS)
        g->idle++;

    if (dx) g->flip = dx < 0;

    if (wandering) {
        if (b->velocity.x == 0 && b->velocity.y == 0) {
            b->velocity.x = g->flip ? -IDLE_SPEED_X : IDLE_SPEED_X;
            b->velocity.y = IDLE_SPEED_Y;
        }
    } else if (dx || dy) {
        b->velocity.x = clampf(b->velocity.x + ACCEL * (float)dx, -MAX_SPEED, MAX_SPEED);
        b->velocity.y = clampf(b->velocity.y + ACCEL * (float)dy, -MAX_SPEED, MAX_SPEED);
    } else {
        b->velocity.x *= FRICTION;
        b->velocity.y *= FRICTION;

        if (absf(b->velocity.x) < 0.05f) b->velocity.x = 0;
        if (absf(b->velocity.y) < 0.05f) b->velocity.y = 0;
    }

    if (hit > 0 && (wandering || hit >= FLASH_SPEED)) {
        orb_vec2f at = orb->entity_world_at(id);
        float pan = (at.x - g->camera.at.x + BODY / 2) / (SCREEN_W / 2.0f) - 1;

        g->flash = FLASH_TICKS;
        orb->sound_play(g->bounce, (orb_sound_params) {.volume = 0.8f, .pan = pan}, 0);
    }

    if (g->flash > 0) g->flash--;

    sc->flags = g->flip ? ORB_FLIP_X : 0;
    sc->remap = g->flash > 0 ? 0 : -1;
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;
    uint8_t kinds[256] = {0};
    uint8_t remap[256];
    const char* room = "room";

    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;

    remap[WHITE] = RED;
    kinds[1] = ORB_CELL_SOLID;
    g->walk = orb->anim_find("player", "walk");
    g->bounce = orb->sample_find("bounce");
    g->room = orb->level_find(room);
    g->floor = orb->layer_find(g->room, "floor");
    g->walls = orb->layer_find(g->room, "walls");
    g->shadow = orb->layer_find(g->room, "shadow");
    g->camera.bounds = orb->level_bounds(g->room);
    g->font = orb->font_find("body");
    g->player_type = orb->type_find("player");
    orb->type_bind(g->player_type, player_init, player_update);
    orb->world_collision("walls", kinds);
    orb->remap_set(0, remap);

    if (!orb->entity_get(g->player)) {
        orb->level_spawn(g->room);

        if (orb->entity_of_type(g->player_type, &g->player, 1) == 0)
            orb->log("no player placed in %s", room);
    }
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;

    if (orb->button_pressed(ORB_BTN_SELECT)) {
        g->hint = false;
        g->paused = !g->paused;

        if (g->paused)
            orb->song_pause();
        else
            orb->song_resume();
    }

    orb->world_update();

    orb_vec2f at = orb->entity_world_at(g->player);

    g->camera.target = (orb_vec2f) {at.x + BODY / 2, at.y + BODY / 2};
    g->camera = orb->camera_update(g->camera);
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    orb->clear(BG);
    orb->layer_draw(g->room, g->floor);
    orb->layer_draw(g->room, g->walls);
    orb->layer_draw(g->room, g->shadow);
    orb->world_draw(0);

    if (g->hint) {
        const char* hint = "Arrows to move and 'M' toggles music";

        orb->text_draw(
            g->font, hint, (orb_vec2) {(SCREEN_W - orb->text_measure(g->font, hint).width) / 2, 16},
            nullptr
        );
    }
}

static const orb_game game = {config, init, reload, update, draw};

const orb_game* orb_game_main(void) {
    return &game;
}

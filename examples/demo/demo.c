#include "orb.h"

#define SCREEN_W 320
#define FRAME 16  // the player sprite's frame, drawn at (x, y)
#define BODY_AT 4 // its 8x8 body sits at (4,4) in that frame
#define BODY 8

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
    orb_anim_state anim;
    orb_sprite sprite;
    orb_sample bounce;
    orb_level room;
    int floor, walls, shadow;
    int grid; // the walls layer's cell size, what a hit snaps to
    orb_camera camera;
    orb_font font;
    bool paused; // no call reports whether the song is playing
    bool hint;
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

    g->x = 48;
    g->y = 48;
    g->camera.lerp = 0.85f;
    g->hint = true;
    orb->song_play(orb->song_find("song"), true);
}

static void reload(void* state, const orb_api* orb) {
    game_state* g = state;

    g->anim.anim = orb->anim_find("player", "walk");
    g->bounce = orb->sample_find("bounce");
    g->room = orb->level_find("room");
    g->floor = orb->layer_find(g->room, "floor");
    g->walls = orb->layer_find(g->room, "walls");
    g->shadow = orb->layer_find(g->room, "shadow");
    g->grid = orb->layer_info(g->room, g->walls).grid;
    g->camera.bounds = orb->level_bounds(g->room);
    g->font = orb->font_find("body");
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static bool blocked(const orb_api* orb, const game_state* g, float x, float y) {
    int left = (int)x + BODY_AT, right = left + BODY - 1;
    int top = (int)y + BODY_AT, bottom = top + BODY - 1;
    orb_vec2 corners[4] = {{left, top}, {right, top}, {left, bottom}, {right, bottom}};

    for (int i = 0; i < 4; i++)
        if (orb->cell_get(g->room, g->walls, corners[i]) == 1) return true;

    return false;
}

// Integrates one axis; on a wall hit moves the body flush against the cell it entered
// and bounces or stops. Returns the pre-collision speed, or 0 when nothing was hit.
static float move_axis(
    const orb_api* orb,
    const game_state* g,
    float* pos,
    float* vel,
    float other,
    bool along_x,
    float keep
) {
    *pos += *vel;

    float x = along_x ? *pos : other;
    float y = along_x ? other : *pos;
    if (!blocked(orb, g, x, y)) return 0;

    if (*vel > 0) {
        int edge = ((int)*pos + BODY_AT + BODY - 1) / g->grid * g->grid;

        *pos = (float)(edge - BODY_AT - BODY);
    } else {
        int edge = ((int)*pos + BODY_AT) / g->grid * g->grid + g->grid;

        *pos = (float)(edge - BODY_AT);
    }

    // A hard hit bounces and a soft one rests flush, so a held key settles against
    // the wall; the wander (keep 1) always bounces, or it would stall.
    float speed = *vel < 0 ? -*vel : *vel;
    *vel = speed >= FLASH_SPEED || keep == 1.0f ? -*vel * keep : 0;
    return speed;
}

static void update(void* state, const orb_api* orb) {
    game_state* g = state;
    int dx = orb->button_down(ORB_BTN_RIGHT) - orb->button_down(ORB_BTN_LEFT);
    int dy = orb->button_down(ORB_BTN_DOWN) - orb->button_down(ORB_BTN_UP);
    bool wandering = g->idle >= IDLE_TICKS;

    if (dx || dy) {
        g->idle = 0;
        g->hint = false;
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

    float keep = wandering ? 1.0f : BOUNCE;
    float hit_x = move_axis(orb, g, &g->x, &g->vx, g->y, true, keep);
    float hit_y = move_axis(orb, g, &g->y, &g->vy, g->x, false, keep);
    float hit = hit_x > hit_y ? hit_x : hit_y;

    if (orb->button_pressed(ORB_BTN_SELECT)) {
        g->hint = false;
        g->paused = !g->paused;

        if (g->paused)
            orb->song_pause();
        else
            orb->song_resume();
    }

    if (hit > 0 && (wandering || hit >= FLASH_SPEED)) {
        float pan = (g->x - g->camera.at.x + FRAME / 2) / (SCREEN_W / 2.0f) - 1;

        g->flash = FLASH_TICKS;
        orb->sound_play(g->bounce, (orb_sound_params) {.volume = 0.8f, .pan = pan}, 0);
    }
    if (g->flash > 0) g->flash--;

    g->camera.target = (orb_vec2f) {g->x + FRAME / 2, g->y + FRAME / 2};
    orb->camera_update(&g->camera);

    g->sprite = orb->anim_step(&g->anim);
}

static void draw(void* state, const orb_api* orb) {
    game_state* g = state;

    uint8_t remap[256];

    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;

    remap[WHITE] = RED;

    orb->clear(BG);
    orb->layer_draw(g->room, g->floor);
    orb->layer_draw(g->room, g->walls);
    orb->layer_draw(g->room, g->shadow);
    orb->sprite_draw(
        g->sprite, (orb_vec2) {(int)g->x, (int)g->y}, g->flip ? ORB_FLIP_X : 0,
        g->flash > 0 ? remap : nullptr
    );

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

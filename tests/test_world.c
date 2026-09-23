#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// Room (0,0) and Annex (64,0), 8x4 cells of 8 pixels. 1 solid; 2 N, 3 S, 4 E, 5 W one-ways.
static uint8_t cells[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, //
    1, 0, 0, 0, 0, 0, 0, 0, // the seam is open on this row
    1, 0, 0, 2, 2, 0, 0, 1, // a one-way platform at x 24..39, y 16..23
    1, 1, 1, 1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, 1, 1, 1, //
    1, 0, 3, 0, 4, 0, 5, 1, // S at x 80..87, E at 96..103, W at 112..119, all y 8..15
    1, 0, 0, 0, 0, 0, 0, 1, //
    1, 1, 1, 1, 1, 1, 1, 1,
};
static orb_layer_desc layers[2] = {
    {.tiles = 0, .cells = 0, .grid = 8, .columns = 8, .rows = 4},
    {.tiles = 0, .cells = 32, .grid = 8, .columns = 8, .rows = 4},
};
static orb_level_desc levels[2] = {
    {.world_x = 0, .world_y = 0, .width = 64, .height = 32, .first_layer = 0, .layer_count = 1},
    {.world_x = 64, .world_y = 0, .width = 64, .height = 32, .first_layer = 1, .layer_count = 1},
};
static uint64_t level_ids[2], layer_ids[2], type_ids[2], sprite_ids[2] = {1, 2};
static orb_anim_desc anims[1] = {{.first_sprite = 1, .first_duration = 0, .count = 1}};
static uint16_t durations[1] = {1};
static uint64_t anim_ids[1] = {3};
static orb_type_desc types[2] = {{.width = 8, .height = 8}, {.width = 8, .height = 8}};
static uint8_t pixels[8] = {1, 1, 2, 2, 1, 1, 2, 2}; // sprite 0 is index 1, sprite 1 index 2
static orb_sheet_desc sheets[1] = {{.width = 4, .height = 2, .pixels = 0}};
static orb_sprite_desc sprites[2] = {
    {.sheet = 0, .x = 0, .y = 0, .width = 2, .height = 2, .frame_width = 2, .frame_height = 2},
    {.sheet = 0, .x = 2, .y = 0, .width = 2, .height = 2, .frame_width = 2, .frame_height = 2},
};
static orb_assets assets;
static orb_asset_table table;
static alignas(16) uint8_t region_mem[1 << 20];
static orb_arena region;
static int mover_updates;

static void fixture(void) {
    level_ids[0] = orb_asset_id("room", "");
    level_ids[1] = orb_asset_id("annex", "");
    layer_ids[0] = layer_ids[1] = orb_asset_id("collision", "");
    type_ids[0] = orb_asset_id("block", "");
    type_ids[1] = orb_asset_id("mover", "");
    assets = (orb_assets) {
        .sheets = sheets,
        .sheet_count = 1,
        .pixels = pixels,
        .pixel_count = 8,
        .sprites = sprites,
        .sprite_count = 2,
        .sprite_ids = sprite_ids,
        .anims = anims,
        .anim_count = 1,
        .durations = durations,
        .duration_count = 1,
        .anim_ids = anim_ids,
        .levels = levels,
        .level_count = 2,
        .layers = layers,
        .layer_count = 2,
        .cells = cells,
        .cell_count = 64,
        .level_ids = level_ids,
        .layer_ids = layer_ids,
        .types = types,
        .type_count = 2,
        .type_ids = type_ids
    };
    table = (orb_asset_table) {};
    orb_asset_set(&table, &assets);
}

static void mover_update(void* state, const orb_api* orb, orb_entity_id id) {
    (void)state, (void)orb;

    mover_updates++;
    ((orb_body*)orb_entity_component(id, ORB_COMPONENT_BODY))->velocity.x = 3;
}

// A body of the block type at (x, y) with the flags, gravity 0.
static orb_entity_id body_at(float x, float y, uint16_t flags) {
    orb_entity_id id = orb_entity_spawn(ORB_TYPE(0), (orb_vec2f) {x, y});
    orb_body* added_body = orb_entity_add(id, ORB_COMPONENT_BODY);

    added_body->flags = flags;
    return id;
}

static orb_body* body(orb_entity_id id) {
    return orb_entity_component(id, ORB_COMPONENT_BODY);
}

static orb_entity* entity(orb_entity_id id) {
    return orb_entity_get(id);
}

// Empties the pool between scenarios.
static void clear(void) {
    orb_entity_id ids[32];
    int n = orb_entity_all(ids, 32);

    for (int i = 0; i < n; i++)
        orb_entity_despawn(ids[i]);

    orb_entity_free_despawning();
    orb_world_gravity((orb_vec2f) {});
}

static int test_cells_and_sweep(void) {
    // the collision layer by name, the kinds by value, open outside every level
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {0, 0}), ORB_CELL_SOLID);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {63, 8}), ORB_CELL_OPEN);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {64, 8}), ORB_CELL_SOLID);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {24, 16}), ORB_CELL_ONEWAY_N);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {80, 8}), ORB_CELL_ONEWAY_S);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {-1, 0}), ORB_CELL_OPEN);
    CHECK_EQ(orb_world_cell_kind((orb_vec2) {200, 0}), ORB_CELL_OPEN);

    // +x across the seam into Annex's wall at 64: flush at 56, the fraction dropped, impact
    // kept, velocity zeroed, WALL_RIGHT
    orb_entity_id walker = body_at(16.5f, 8, 0);
    body(walker)->velocity.x = 50;
    orb_world_update();
    CHECK(entity(walker)->at.x == 56);
    CHECK(body(walker)->velocity.x == 0);
    CHECK(body(walker)->impact.x == 50);
    CHECK(body(walker)->flags & ORB_BODY_WALL_RIGHT);
    CHECK(!(body(walker)->flags & ORB_BODY_CRUSHED));
    // flush against the wall, a sub-pixel push is still blocked
    body(walker)->velocity.x = 0.4f;
    orb_world_update();
    CHECK(entity(walker)->at.x == 56);
    CHECK(body(walker)->flags & ORB_BODY_WALL_RIGHT);
    // away from the wall, the same sub-pixel push keeps its fraction and blocks nothing
    entity(walker)->at.x = 30;
    body(walker)->velocity.x = 0.4f;
    orb_world_update();
    CHECK(entity(walker)->at.x > 30.39f && entity(walker)->at.x < 30.41f);
    CHECK(!(body(walker)->flags & ORB_BODY_WALL_RIGHT));
    // -x into Room's west wall: flush at 8, WALL_LEFT
    entity(walker)->at.x = 12.3f;
    body(walker)->velocity.x = -10;
    orb_world_update();
    CHECK(entity(walker)->at.x == 8);
    CHECK(body(walker)->flags & ORB_BODY_WALL_LEFT);
    CHECK(body(walker)->impact.x == -10 && body(walker)->impact.y == 0);
    clear();

    // gravity: falls to the floor at y 24, GROUNDED, velocity.y zeroed; a ceiling hit
    orb_world_gravity((orb_vec2f) {0, 1});
    orb_entity_id faller = body_at(16, 8, 0);
    body(faller)->gravity = 1;

    for (int i = 0; i < 10; i++)
        orb_world_update();

    CHECK(entity(faller)->at.y == 16);
    CHECK(body(faller)->flags & ORB_BODY_GROUNDED);
    CHECK(body(faller)->velocity.y == 0);
    // flush against the floor, a sub-pixel gravity push still reports GROUNDED every tick
    orb_world_gravity((orb_vec2f) {0, 0.4f});

    for (int i = 0; i < 3; i++) {
        orb_world_update();
        CHECK(entity(faller)->at.y == 16);
        CHECK(body(faller)->flags & ORB_BODY_GROUNDED);
        CHECK(body(faller)->velocity.y == 0);
    }

    orb_world_gravity((orb_vec2f) {0, 1});
    body(faller)->velocity.y = -20;
    orb_world_update();
    CHECK(entity(faller)->at.y == 8);
    CHECK(body(faller)->flags & ORB_BODY_CEILING);
    clear();
    return 0;
}

static int test_oneway(void) {
    orb_world_gravity((orb_vec2f) {0, 1});

    // the N platform at y 16: land on it from above, stay on it, drop through with DROP
    orb_entity_id lander = body_at(28, 4, 0);
    body(lander)->gravity = 1;

    for (int i = 0; i < 8; i++)
        orb_world_update();

    CHECK(entity(lander)->at.y == 8);
    CHECK(body(lander)->flags & ORB_BODY_GROUNDED);
    orb_world_update();
    CHECK(entity(lander)->at.y == 8);
    body(lander)->flags |= ORB_BODY_DROP;
    orb_world_update();
    CHECK(entity(lander)->at.y > 8);
    CHECK(!(body(lander)->flags & ORB_BODY_DROP));

    for (int i = 0; i < 8; i++)
        orb_world_update();

    CHECK(entity(lander)->at.y == 16);
    // from below it passes through and hits the ceiling
    entity(lander)->at.y = 17;
    body(lander)->velocity.y = -20;
    orb_world_update();
    CHECK(entity(lander)->at.y == 8);
    clear();
    orb_world_gravity((orb_vec2f) {});

    // S: passes moving down, blocks moving up
    orb_entity_id south_body = body_at(80, 0, 0);
    body(south_body)->velocity.y = 4;
    orb_world_update();
    CHECK(entity(south_body)->at.y == 4);
    entity(south_body)->at.y = 16;
    body(south_body)->velocity.y = -4;
    orb_world_update();
    CHECK(entity(south_body)->at.y == 16);
    CHECK(body(south_body)->flags & ORB_BODY_CEILING);
    // E blocks a body arriving from the east; W blocks one arriving from the west; the E
    // cell lets a +x move through
    orb_entity_id east_body = body_at(104, 8, 0);
    body(east_body)->velocity.x = -4;
    orb_world_update();
    CHECK(entity(east_body)->at.x == 104);
    CHECK(body(east_body)->flags & ORB_BODY_WALL_LEFT);
    entity(east_body)->at.x = 88;
    body(east_body)->velocity.x = 20;
    orb_world_update();
    CHECK(entity(east_body)->at.x == 104);
    CHECK(body(east_body)->flags & ORB_BODY_WALL_RIGHT);
    clear();
    return 0;
}

static int test_order_and_solids(void) {
    // y before x: moving (-8, +8) from (52, 8) lands on the solid cell at (56..63, 16..23)
    // before sliding left off it
    orb_entity_id slider = body_at(52, 8, 0);
    body(slider)->velocity = (orb_vec2f) {-8, 8};
    orb_world_update();
    CHECK(entity(slider)->at.x == 44 && entity(slider)->at.y == 8);
    CHECK(body(slider)->flags & ORB_BODY_GROUNDED);
    clear();

    // a solid stops at a cell and never at a body
    orb_entity_id wall = body_at(40, 8, ORB_BODY_SOLID);
    body(wall)->velocity.x = 30;
    orb_world_update();
    CHECK(entity(wall)->at.x == 56);
    CHECK(body(wall)->moved.x == 16);
    clear();

    // push without a crush: a full solid moving +x 20 from (16,8) ends at 36 and shoves a body
    // at 30 to 44
    orb_entity_id shover = body_at(16, 8, ORB_BODY_SOLID);
    body(shover)->velocity.x = 20;
    orb_entity_id shoved = body_at(30, 8, 0);
    orb_world_update();
    CHECK(entity(shover)->at.x == 36);
    CHECK(entity(shoved)->at.x == 44);
    CHECK(!(body(shoved)->flags & ORB_BODY_CRUSHED));
    clear();

    // push into a wall: the solid from (32,8) reaches 56 and shoves a body at 52 into
    // Annex's wall at 64; the body stops at 56 still overlapping and is crushed
    orb_entity_id ram = body_at(32, 8, ORB_BODY_SOLID);
    body(ram)->velocity.x = 30;
    orb_entity_id victim = body_at(52, 8, 0);
    orb_world_update();
    CHECK(entity(ram)->at.x == 56);
    CHECK(entity(victim)->at.x == 56);
    CHECK(body(victim)->flags & ORB_BODY_CRUSHED);
    body(ram)->velocity.x = -8;
    orb_world_update();
    CHECK(!(body(victim)->flags & ORB_BODY_CRUSHED)); // the solid left, the overlap is gone
    clear();

    // a body resting on a solid: standing_on after an update with nothing moving. The rider
    // is 4 tall, since the room's interior is 16 and a platform plus an 8-tall rider fill it.
    orb_entity_id platform = body_at(16, 16, ORB_BODY_SOLID);
    orb_entity_id rider = body_at(16, 12, 0);
    body(rider)->box.size.height = 4;
    orb_world_update();
    CHECK(body(rider)->standing_on.v == platform.v);
    // carry on both axes: the platform moves (2, -1); the rider follows to (18, 11) and stays on
    body(platform)->velocity = (orb_vec2f) {2, -1};
    orb_world_update();
    CHECK(entity(platform)->at.x == 18 && entity(platform)->at.y == 15);
    CHECK(entity(rider)->at.x == 18 && entity(rider)->at.y == 11);
    CHECK(body(rider)->standing_on.v == platform.v);
    CHECK(!(body(rider)->flags & ORB_BODY_CRUSHED));
    // the platform is moved by writing at: the rider still follows
    body(platform)->velocity = (orb_vec2f) {};
    entity(platform)->at.x += 3;
    orb_world_update();
    CHECK(entity(rider)->at.x == 21);
    // the rider walks off and is released
    body(rider)->velocity.x = 20;
    orb_world_update();
    CHECK(entity(rider)->at.x == 41);
    CHECK(body(rider)->standing_on.v == ORB_NO_ENTITY.v);
    clear();

    // carrier: a body with no gravity above a solid, carried by naming it
    orb_entity_id raft = body_at(16, 16, ORB_BODY_SOLID);
    orb_entity_id passenger = body_at(30, 8, 0);
    body(passenger)->carrier = raft;
    body(raft)->velocity.x = 1;
    orb_world_update();
    CHECK(entity(passenger)->at.x == 31);
    clear();

    // a carried body stopped by a wall stays put while the platform goes on; not crushed,
    // since nothing overlaps it
    orb_entity_id lift = body_at(52, 16, ORB_BODY_SOLID);
    orb_entity_id stuck = body_at(56, 8, 0);
    orb_world_update();
    CHECK(body(stuck)->standing_on.v == lift.v);
    body(lift)->velocity.x = 30;
    orb_world_update();
    CHECK(entity(lift)->at.x == 56); // Annex's wall at 64 on row 2
    CHECK(entity(stuck)->at.x == 56);
    CHECK(!(body(stuck)->flags & ORB_BODY_CRUSHED));
    clear();

    // a parented body is not moved but blocks; the update order runs the type update first
    orb_entity_id anchor = body_at(16, 8, 0);
    orb_entity_id blocker = body_at(0, 0, ORB_BODY_SOLID);
    entity(blocker)->parent = anchor;
    entity(blocker)->at = (orb_vec2f) {16, 0}; // world (32, 8)
    body(blocker)->velocity.x = 5;
    orb_entity_id walker = body_at(20, 8, 0);
    body(walker)->velocity.x = 10;
    orb_world_update();
    CHECK(entity(blocker)->at.x == 16);
    CHECK(entity(walker)->at.x == 24);
    clear();

    // a parented body inside a solid cell is crushed; a paused one keeps its last bits
    orb_entity_id holder = body_at(16, 8, 0);
    orb_entity_id hitbox = body_at(0, 0, 0);
    entity(hitbox)->parent = holder;
    entity(hitbox)->at = (orb_vec2f) {0, -8}; // world (16, 0): inside the ceiling row
    orb_world_update();
    CHECK(body(hitbox)->flags & ORB_BODY_CRUSHED);
    entity(hitbox)->flags |= ORB_ENTITY_PAUSED;
    body(hitbox)->flags &= (uint16_t)~ORB_BODY_CRUSHED;
    orb_world_update();
    CHECK(!(body(hitbox)->flags & ORB_BODY_CRUSHED));
    clear();

    orb_type_bind(ORB_TYPE(1), nullptr, mover_update);
    orb_entity_id mover = orb_entity_spawn(ORB_TYPE(1), (orb_vec2f) {16, 8});
    orb_entity_add(mover, ORB_COMPONENT_BODY);
    orb_world_update();
    CHECK_EQ(mover_updates, 1);
    CHECK(entity(mover)->at.x == 19);
    // a paused entity is neither updated nor moved; a despawning one is freed at the end
    entity(mover)->flags |= ORB_ENTITY_PAUSED;
    orb_world_update();
    CHECK_EQ(mover_updates, 1);
    CHECK(entity(mover)->at.x == 19);
    entity(mover)->flags &= ~ORB_ENTITY_PAUSED;
    orb_entity_despawn(mover);
    orb_world_update();
    CHECK_EQ(mover_updates, 1);
    CHECK(entity(mover) == nullptr);
    orb_type_bind(ORB_TYPE(1), nullptr, nullptr);
    clear();
    return 0;
}

static int test_queries(void) {
    orb_entity_id list[8];
    // A at 16..23 tagged 1, B at 30..37 tagged 2, C at 40..47 untagged, all on row 1
    orb_entity_id entity_a = body_at(16, 8, 0), entity_b = body_at(30, 8, 0),
                  entity_c = body_at(40, 8, 0);

    ((orb_tag*)orb_entity_add(entity_a, ORB_COMPONENT_TAG))->bits = 1;
    ((orb_tag*)orb_entity_add(entity_b, ORB_COMPONENT_TAG))->bits = 2;

    // rect: overlap, exclusive edges, mask, except, max
    orb_rect rect = {{20, 8}, {12, 8}};
    CHECK_EQ(orb_query_rect(rect, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 2);
    CHECK(list[0].v == entity_a.v && list[1].v == entity_b.v);
    CHECK_EQ(orb_query_rect(rect, 2, ORB_NO_ENTITY, list, 8), 1);
    CHECK(list[0].v == entity_b.v);
    CHECK_EQ(orb_query_rect(rect, 0, ORB_NO_ENTITY, list, 8), 0);
    CHECK_EQ(orb_query_rect(rect, ORB_TAG_ANY, entity_a, list, 8), 1);
    CHECK(list[0].v == entity_b.v);
    CHECK_EQ(orb_query_rect(rect, ORB_TAG_ANY, ORB_NO_ENTITY, list, 1), 1);
    CHECK_EQ(orb_query_rect((orb_rect) {{24, 8}, {6, 8}}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 0);
    CHECK_EQ(
        orb_query_rect((orb_rect) {{40, 8}, {2, 2}}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 0
    ); // C has no tag

    // point: containment with exclusive far edges
    CHECK_EQ(orb_query_point((orb_vec2) {17, 9}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 1);
    CHECK(list[0].v == entity_a.v);
    CHECK_EQ(orb_query_point((orb_vec2) {24, 9}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 0);
    CHECK_EQ(orb_query_point((orb_vec2) {30, 15}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 1);
    CHECK_EQ(orb_query_point((orb_vec2) {30, 16}, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 0);

    // circle: squared distance from the box's nearest pixel, inclusive
    CHECK_EQ(orb_query_circle((orb_vec2) {28, 12}, 2, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 1);
    CHECK(list[0].v == entity_b.v);
    CHECK_EQ(orb_query_circle((orb_vec2) {28, 12}, 1, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 0);
    CHECK_EQ(orb_query_circle((orb_vec2) {18, 10}, 0, ORB_TAG_ANY, ORB_NO_ENTITY, list, 8), 1);
    CHECK(list[0].v == entity_a.v);

    // ray: the nearest body along x, at the entry point with the face normal
    orb_hit hit;
    CHECK(
        orb_query_ray((orb_vec2) {10, 12}, (orb_vec2) {60, 12}, ORB_TAG_ANY, 0, ORB_NO_ENTITY, &hit)
    );
    CHECK(hit.entity.v == entity_a.v);
    CHECK(hit.at.x == 16 && hit.at.y == 12);
    CHECK(hit.normal.x == -1 && hit.normal.y == 0);
    CHECK(hit.fraction > 0.11f && hit.fraction < 0.13f);
    // except and mask skip A; a body containing from is skipped
    CHECK(orb_query_ray((orb_vec2) {10, 12}, (orb_vec2) {60, 12}, 2, 0, entity_a, &hit));
    CHECK(hit.entity.v == entity_b.v && hit.at.x == 30);
    CHECK(
        orb_query_ray((orb_vec2) {16, 12}, (orb_vec2) {60, 12}, ORB_TAG_ANY, 0, ORB_NO_ENTITY, &hit)
    );
    CHECK(hit.entity.v == entity_b.v);
    CHECK(
        orb_query_ray((orb_vec2) {18, 12}, (orb_vec2) {60, 12}, ORB_TAG_ANY, 0, ORB_NO_ENTITY, &hit)
    );
    CHECK(hit.entity.v == entity_b.v);
    // a miss leaves hit alone
    hit.entity = entity_c;
    CHECK(!orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {14, 12}, ORB_TAG_ANY, 0, ORB_NO_ENTITY, &hit
    ));
    CHECK(hit.entity.v == entity_c.v);
    // cells: the seam wall at 64 on row 1, hit from the open side, at the last open pixel
    CHECK(orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {70, 12}, 0, ORB_RAY_CELLS, ORB_NO_ENTITY, &hit
    ));
    CHECK(hit.entity.v == ORB_NO_ENTITY.v);
    CHECK(hit.at.x == 63 && hit.at.y == 12);
    CHECK(hit.normal.x == -1);
    CHECK(hit.fraction == 0.9f);
    CHECK(!orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {60, 12}, 0, ORB_RAY_CELLS, ORB_NO_ENTITY, &hit
    ));
    // solids only through the flag; a one-way solid blocks only from its solid side and only
    // with ORB_RAY_ONEWAY
    body(entity_c)->flags = ORB_BODY_SOLID;
    CHECK(orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {60, 12}, 0, ORB_RAY_SOLIDS, ORB_NO_ENTITY, &hit
    ));
    CHECK(hit.entity.v == entity_c.v && hit.at.x == 40);
    body(entity_c)->flags = ORB_BODY_SOLID | ORB_BODY_ONEWAY_W;
    CHECK(!orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {60, 12}, 0, ORB_RAY_SOLIDS, ORB_NO_ENTITY, &hit
    ));
    CHECK(orb_query_ray(
        (orb_vec2) {10, 12}, (orb_vec2) {60, 12}, 0, ORB_RAY_SOLIDS | ORB_RAY_ONEWAY, ORB_NO_ENTITY,
        &hit
    ));
    CHECK(hit.entity.v == entity_c.v);
    CHECK(!orb_query_ray(
        (orb_vec2) {60, 12}, (orb_vec2) {10, 12}, 0, ORB_RAY_SOLIDS | ORB_RAY_ONEWAY, ORB_NO_ENTITY,
        &hit
    ));
    clear();

    // one-way cells: the N platform blocks a downward ray with the flag, passes without, and
    // never blocks an upward or a sideways one
    CHECK(orb_query_ray(
        (orb_vec2) {28, 10}, (orb_vec2) {28, 30}, 0, ORB_RAY_CELLS | ORB_RAY_ONEWAY, ORB_NO_ENTITY,
        &hit
    ));
    CHECK(hit.at.y == 15 && hit.normal.y == -1);
    CHECK(hit.fraction == 0.3f);
    CHECK(orb_query_ray(
        (orb_vec2) {28, 10}, (orb_vec2) {28, 30}, 0, ORB_RAY_CELLS, ORB_NO_ENTITY, &hit
    ));
    CHECK(hit.at.y == 23);
    CHECK(!orb_query_ray(
        (orb_vec2) {28, 22}, (orb_vec2) {28, 10}, 0, ORB_RAY_CELLS | ORB_RAY_ONEWAY, ORB_NO_ENTITY,
        &hit
    ));
    CHECK(!orb_query_ray(
        (orb_vec2) {20, 20}, (orb_vec2) {44, 20}, 0, ORB_RAY_CELLS | ORB_RAY_ONEWAY, ORB_NO_ENTITY,
        &hit
    ));
    return 0;
}

static int test_draw(void) {
    static alignas(16) uint8_t fb_mem[4096];
    orb_arena arena;
    orb_fb fb;
    uint32_t rgb[16 * 16];
    orb_vec2f cam = {0, 0};

    orb_arena_init(&arena, "fb", fb_mem, sizeof fb_mem);
    orb_fb_init(&fb, &arena, (orb_size) {16, 16});

    // two 2x2 sprites: entity1 (index 1) at (2,2), entity2 (index 2) at (2,3); the lower one draws
    // last
    orb_entity_id entity1 = orb_entity_spawn(ORB_TYPE(0), (orb_vec2f) {2, 2});
    orb_entity_id entity2 = orb_entity_spawn(ORB_TYPE(0), (orb_vec2f) {2, 3});
    orb_sprite_component* sprite1 = orb_entity_add(entity1, ORB_COMPONENT_SPRITE);
    orb_sprite_component* sprite2 = orb_entity_add(entity2, ORB_COMPONENT_SPRITE);

    sprite1->sprite = ORB_SPRITE(0);
    sprite2->sprite = ORB_SPRITE(1);
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 2], 1);
    CHECK_EQ(fb.px[3 * 16 + 2], 2);
    CHECK_EQ(fb.px[4 * 16 + 3], 2);
    // a bias lifts entity1 above entity2; a tie draws the higher slot last
    sprite1->sort_bias = 5;
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[3 * 16 + 2], 1);
    sprite1->sort_bias = 1;
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[3 * 16 + 2], 2);
    sprite1->sort_bias = 0;
    // layers: entity2 on layer 1 is not drawn by layer 0; a hidden entity1 is not drawn either
    sprite2->layer = 1;
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[3 * 16 + 2], 1);
    CHECK_EQ(fb.px[4 * 16 + 2], 0);
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 1);
    CHECK_EQ(fb.px[2 * 16 + 2], 0);
    CHECK_EQ(fb.px[3 * 16 + 2], 2);
    sprite2->layer = 0;
    orb_entity_get(entity1)->flags &= (uint16_t)~ORB_ENTITY_VISIBLE;
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 2], 0);
    orb_entity_get(entity1)->flags |= ORB_ENTITY_VISIBLE;
    // the camera, the draw offset, an animation's frame, and a remap table
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, (orb_vec2f) {2, 2}, 0);
    CHECK_EQ(fb.px[0], 1);
    sprite1->offset = (orb_vec2) {3, 0};
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 5], 1);
    sprite1->offset = (orb_vec2) {0, 0};
    sprite1->anim.anim = ORB_ANIM(0);
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 2], 2); // the animation's only frame is sprite 1
    sprite1->anim.anim = ORB_NO_ANIM;
    uint8_t remap[256];

    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)i;

    remap[1] = 3;
    orb_world_remap_set(0, remap);
    sprite1->remap = 0;
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 2], 3);
    sprite1->remap = 5; // an unset slot draws plain
    orb_fb_clear(&fb, 0);
    orb_world_draw(&fb, cam, 0);
    CHECK_EQ(fb.px[2 * 16 + 2], 1);
    orb_log_clear();
    orb_world_remap_set(ORB_MAX_REMAPS, remap);
    CHECK(strstr(orb_log_line(0), "remap_set"));

    // the debug outline: every body's box on the RGB frame, only while the variable is set
    orb_entity_add(entity1, ORB_COMPONENT_BODY);

    for (int i = 0; i < 256; i++)
        rgb[i] = 0xffffffffu;

    orb_world_debug_draw(rgb, (orb_size) {16, 16}, cam);
    CHECK_EQ(rgb[2 * 16 + 2], 0xffffffffu);
    orb_entity_debug = true;
    orb_world_debug_draw(rgb, (orb_size) {16, 16}, cam);
    CHECK(rgb[2 * 16 + 2] != 0xffffffffu);
    CHECK(rgb[9 * 16 + 9] != 0xffffffffu);
    CHECK(rgb[2 * 16 + 9] != 0xffffffffu);
    CHECK_EQ(rgb[5 * 16 + 5], 0xffffffffu);
    CHECK_EQ(rgb[10 * 16 + 10], 0xffffffffu);
    orb_entity_debug = false;
    clear();
    return 0;
}

int main(void) {
    static int state;
    orb_config settings = {.max_entities = 16};
    uint8_t kinds[256] = {0};

    kinds[1] = ORB_CELL_SOLID;
    kinds[2] = ORB_CELL_ONEWAY_N;
    kinds[3] = ORB_CELL_ONEWAY_S;
    kinds[4] = ORB_CELL_ONEWAY_E;
    kinds[5] = ORB_CELL_ONEWAY_W;
    fixture();
    orb_arena_init(&region, "pool", region_mem, sizeof region_mem);
    orb_entity_boot(&region, &settings, &state, orb_api_table(), &table.assets);
    orb_world_collision("collision", kinds);

    if (test_cells_and_sweep()) return 1;
    if (test_oneway()) return 1;
    if (test_order_and_solids()) return 1;
    if (test_queries()) return 1;
    if (test_draw()) return 1;

    return 0;
}

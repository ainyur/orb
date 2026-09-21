#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// One level, two types, three placements. crate (8x8) defaults hp 10 and label "box";
// crate-a carries hp 3, loot [1,2,3], exit (40,16), link -> crate-b; crate-b and the marker
// carry nothing. The field data is laid out by hand as the caster would.
static orb_level_desc levels[1] = {
    {.width = 64, .height = 32, .first_placement = 0, .placement_count = 3}
};
static uint64_t level_ids[1];
static orb_type_desc types[2] = {
    {.width = 8, .height = 8, .first_field = 0, .field_count = 2},
    {.width = 4, .height = 4, .first_field = 2, .field_count = 0},
};
static uint64_t type_ids[2];
static orb_placement_desc placements[3] = {
    {.iid = 0xa,
     .type = 0,
     .level = 0,
     .x = 16,
     .y = 8,
     .width = 8,
     .height = 8,
     .first_field = 2,
     .field_count = 4},
    {.iid = 0xb,
     .type = 0,
     .level = 0,
     .x = 32,
     .y = 8,
     .width = 8,
     .height = 8,
     .first_field = 6,
     .field_count = 0},
    {.iid = 0xc,
     .type = 1,
     .level = 0,
     .x = 48,
     .y = 16,
     .width = 4,
     .height = 4,
     .first_field = 6,
     .field_count = 0},
};
static orb_field_desc fields[6];
static uint8_t data[40];
static orb_assets as;
static orb_asset_table table;
static alignas(16) uint8_t region_mem[1 << 20];
static orb_arena region;
static int inits, updates;
static orb_entity_id last_init;

static void put32(int at, int32_t v) {
    memcpy(data + at, &v, 4);
}

static void fixture(void) {
    level_ids[0] = orb_asset_id("room", "");
    type_ids[0] = orb_asset_id("crate", "");
    type_ids[1] = orb_asset_id("marker", "");
    fields[0] = (orb_field_desc) {
        .name = orb_asset_id("hp", ""), .data = 0, .count = 1, .kind = ORB_FIELD_INT
    };
    fields[1] = (orb_field_desc) {
        .name = orb_asset_id("label", ""), .data = 4, .count = 1, .kind = ORB_FIELD_STRING
    };
    fields[2] = (orb_field_desc) {
        .name = orb_asset_id("hp", ""), .data = 12, .count = 1, .kind = ORB_FIELD_INT
    };
    fields[3] = (orb_field_desc) {
        .name = orb_asset_id("loot", ""), .data = 16, .count = 3, .kind = ORB_FIELD_INT
    };
    fields[4] = (orb_field_desc) {
        .name = orb_asset_id("exit", ""), .data = 28, .count = 1, .kind = ORB_FIELD_POINT
    };
    fields[5] = (orb_field_desc) {
        .name = orb_asset_id("link", ""), .data = 36, .count = 1, .kind = ORB_FIELD_REF
    };
    put32(0, 10);
    put32(4, 8);
    memcpy(data + 8, "box", 4);
    put32(12, 3);
    put32(16, 1);
    put32(20, 2);
    put32(24, 3);
    put32(28, 40);
    put32(32, 16);
    put32(36, 1);
    as = (orb_assets) {
        .levels = levels,
        .level_count = 1,
        .level_ids = level_ids,
        .types = types,
        .type_count = 2,
        .type_ids = type_ids,
        .placements = placements,
        .placement_count = 3,
        .fields = fields,
        .field_count = 6,
        .field_data = data,
        .field_data_count = 40
    };
    table = (orb_asset_table) {};
    orb_asset_set(&table, &as);
}

static void crate_init(void* state, const orb_api* orb, orb_entity_id id) {
    (void)state, (void)orb;

    inits++;
    last_init = id;
}

static void crate_update(void* state, const orb_api* orb, orb_entity_id id) {
    (void)state, (void)orb, (void)id;

    updates++;
}

static orb_config config(void) {
    return (orb_config) {.max_entities = 4, .components = {12}};
}

int main(void) {
    static int state;
    orb_config c = config();

    fixture();
    orb_arena_init(&region, "pool", region_mem, sizeof region_mem);
    orb_entity_boot(&region, &c, &state, orb_api_table(), &table.assets);

    orb_pool* p = orb_entity_pool();
    CHECK_EQ(p->max, 4);
    CHECK_EQ(p->kinds, 4);
    CHECK_EQ(p->sizes[ORB_COMPONENT_GAME], 12);
    CHECK_EQ(p->free_count, 4);

    orb_type crate = ORB_TYPE(0), marker = ORB_TYPE(1);
    orb_level room = ORB_LEVEL(0);

    // a stale type binds nothing and logs; a live one runs init at every spawn
    orb_log_clear();
    orb_type_bind(ORB_NO_TYPE, crate_init, nullptr);
    CHECK(strstr(orb_log_line(0), "type_bind"));
    orb_type_bind(crate, crate_init, crate_update);
    CHECK_EQ(orb_entity_type_fns(0)->update == crate_update, 1);

    // level_spawn: three placements, slots 0..2 in placement order, crate init twice
    orb_level_spawn(room);
    CHECK_EQ(inits, 2);
    CHECK_EQ(p->free_count, 1);
    orb_entity* a = orb_entity_at(0);
    CHECK(a);
    CHECK_EQ(ORB_HANDLE_GEN(a->self), 1);
    CHECK_EQ(a->placement, 0);
    CHECK(a->iid == 0xa);
    CHECK_EQ(ORB_HANDLE_INDEX(a->level), 0);
    CHECK(a->at.x == 16 && a->at.y == 8);
    CHECK_EQ(a->size.width, 8);
    CHECK(a->flags & ORB_ENTITY_VISIBLE);
    CHECK_EQ(orb_entity_at(2)->size.width, 4);
    CHECK_EQ(orb_entity_at(3) == nullptr, 1);

    // a runtime spawn: type size, the level containing it, no placement, defaults read
    orb_entity_id r = orb_entity_spawn(crate, (orb_vec2f) {20.5f, 9});
    CHECK_EQ(ORB_HANDLE_INDEX(r), 3);
    CHECK_EQ(inits, 3);
    CHECK(last_init.v == r.v);
    orb_entity* re = orb_entity_get(r);
    CHECK(re && re->placement == ORB_NO_INDEX && ORB_HANDLE_INDEX(re->level) == 0);
    CHECK_EQ(orb_entity_field_int(r, "hp", 0), 10);
    CHECK(strcmp(orb_entity_field_string(r, "label", 0), "box") == 0);
    CHECK_EQ(orb_entity_field_count(r, "loot"), 0);
    CHECK_EQ(orb_entity_field_point(r, "exit", 0).x, 0);

    // the pool is full: log, ORB_NO_ENTITY; a stale type too
    orb_log_clear();
    CHECK(orb_entity_spawn(crate, (orb_vec2f) {0, 0}).v == ORB_NO_ENTITY.v);
    CHECK(strstr(orb_log_line(0), "full"));
    CHECK(orb_entity_spawn(ORB_TYPE(9), (orb_vec2f) {0, 0}).v == ORB_NO_ENTITY.v);
    CHECK(orb_entity_spawn(ORB_NO_TYPE, (orb_vec2f) {0, 0}).v == ORB_NO_ENTITY.v);

    // a spawn outside every level has no level
    orb_entity_despawn(r);
    orb_entity_free_despawning();
    r = orb_entity_spawn(crate, (orb_vec2f) {-50, -50});
    CHECK(orb_entity_get(r)->level.v == ORB_NO_LEVEL.v);

    // fields on a placed entity: the placement's values first, the type's defaults second,
    // zero for a missing name, a bad index, or a kind mismatch
    orb_entity_id ida = a->self, idb = orb_entity_at(1)->self, idm = orb_entity_at(2)->self;
    CHECK_EQ(orb_entity_field_int(ida, "hp", 0), 3);
    CHECK_EQ(orb_entity_field_int(ida, "HP", 0), 3);
    CHECK(strcmp(orb_entity_field_string(ida, "label", 0), "box") == 0);
    CHECK_EQ(orb_entity_field_count(ida, "loot"), 3);
    CHECK_EQ(orb_entity_field_int(ida, "loot", 2), 3);
    CHECK_EQ(orb_entity_field_int(ida, "loot", 3), 0);
    CHECK_EQ(orb_entity_field_int(ida, "loot", -1), 0);
    CHECK_EQ(orb_entity_field_int(ida, "label", 0), 0);
    CHECK_EQ(orb_entity_field_count(ida, "nope"), 0);
    CHECK(strcmp(orb_entity_field_string(ida, "nope", 0), "") == 0);
    CHECK_EQ(orb_entity_field_point(ida, "exit", 0).x, 40);
    CHECK_EQ(orb_entity_field_point(ida, "exit", 0).y, 16);
    CHECK(orb_entity_field_ref(ida, "link", 0).v == idb.v);
    CHECK_EQ(orb_entity_field_int(idb, "hp", 0), 10);
    CHECK_EQ(orb_entity_field_int(idm, "hp", 0), 0);
    CHECK(!orb_entity_field_bool(ida, "hp", 0));
    CHECK(orb_entity_field_float(ida, "hp", 0) == 0);

    // a ref to a despawning target is ORB_NO_ENTITY; to an unspawned one too
    orb_entity_despawn(idb);
    CHECK(orb_entity_field_ref(ida, "link", 0).v == ORB_NO_ENTITY.v);
    orb_entity_free_despawning();
    CHECK(orb_entity_field_ref(ida, "link", 0).v == ORB_NO_ENTITY.v);
    CHECK(orb_entity_get(idb) == nullptr);
    CHECK(orb_entity_get(ORB_ENTITY(0)) == nullptr); // an all-zero handle is never live
    CHECK(orb_entity_get(ORB_NO_ENTITY) == nullptr);

    // FIFO reuse: slot 1 freed first, then slot 3; spawns take 1 then 3, at generation 2
    orb_entity_despawn(r);
    orb_entity_free_despawning();
    orb_entity_id n1 = orb_entity_spawn(marker, (orb_vec2f) {1, 1});
    orb_entity_id n2 = orb_entity_spawn(marker, (orb_vec2f) {2, 2});
    CHECK_EQ(ORB_HANDLE_INDEX(n1), 1);
    CHECK_EQ(ORB_HANDLE_GEN(n1), 2);
    CHECK_EQ(ORB_HANDLE_INDEX(n2), 3);
    CHECK(orb_entity_get(idb) == nullptr); // the old handle at slot 1 stays stale

    // components: add zeroes and sets the built-in defaults, a second add keeps the slot,
    // remove clears the bit, a game kind works, an unknown kind is refused
    orb_body* body = orb_entity_add(ida, ORB_COMPONENT_BODY);
    CHECK(body);
    CHECK_EQ(body->box.size.width, 8);
    CHECK(body->standing_on.v == ORB_NO_ENTITY.v && body->carrier.v == ORB_NO_ENTITY.v);
    CHECK(body->last_at.x == 16);
    body->velocity.x = 5;
    CHECK(orb_entity_add(ida, ORB_COMPONENT_BODY) == body);
    CHECK(body->velocity.x == 5);
    CHECK(orb_entity_component(ida, ORB_COMPONENT_BODY) == body);
    CHECK(orb_entity_component(ida, ORB_COMPONENT_SPRITE) == nullptr);
    orb_sprite_component* sc = orb_entity_add(ida, ORB_COMPONENT_SPRITE);
    CHECK(sc->sprite.v == ORB_NO_SPRITE.v && sc->anim.anim.v == ORB_NO_ANIM.v && sc->remap == -1);
    int32_t* game = orb_entity_add(ida, ORB_COMPONENT_GAME);
    CHECK(game && game[0] == 0 && game[1] == 0 && game[2] == 0);
    CHECK(orb_entity_add(ida, ORB_COMPONENT_GAME + 1) == nullptr);
    CHECK(orb_entity_add(ida, -1) == nullptr);
    orb_entity_remove(ida, ORB_COMPONENT_BODY);
    CHECK(orb_entity_component(ida, ORB_COMPONENT_BODY) == nullptr);
    orb_entity_remove(ida, ORB_COMPONENT_SPRITE);
    CHECK(orb_entity_component(ida, ORB_COMPONENT_SPRITE) == nullptr);
    orb_entity_remove(ida, ORB_COMPONENT_GAME);
    CHECK(orb_entity_component(ida, ORB_COMPONENT_GAME) == nullptr);
    CHECK(orb_entity_add(ORB_NO_ENTITY, ORB_COMPONENT_BODY) == nullptr);

    // parents: one level of relative position; a freed parent orphans at the world position
    orb_entity* m1 = orb_entity_get(n1);
    m1->parent = ida;
    m1->at = (orb_vec2f) {1, 1};
    CHECK(orb_entity_world_at(n1).x == 17 && orb_entity_world_at(n1).y == 9);
    orb_entity_get(n2)->parent = n1;
    orb_entity_get(n2)->at = (orb_vec2f) {1, 1};
    CHECK(orb_entity_world_at(n2).x == 2); // the parent's own parent is ignored
    orb_entity_despawn(ida);
    orb_entity_free_despawning();
    CHECK(m1->parent.v == ORB_NO_ENTITY.v);
    CHECK(m1->at.x == 17 && m1->at.y == 9);

    // enumeration and level_despawn: persistent entities stay
    orb_entity_id list[8];
    CHECK_EQ(orb_entity_all(list, 8), 3);
    CHECK_EQ(orb_entity_of_type(marker, list, 8), 3);
    CHECK_EQ(orb_entity_of_type(crate, list, 8), 0);
    CHECK_EQ(orb_entity_all(list, 1), 1);
    // level_spawn despawns the room's three first, but the despawns are deferred, so only one
    // slot is free: the first placement spawns and the second finds the pool full
    orb_log_clear();
    orb_level_spawn(room);
    CHECK(strstr(orb_log_line(0), "full"));
    orb_entity_free_despawning();
    CHECK_EQ(orb_entity_all(list, 8), 1);
    orb_level_spawn(room); // the one spawned goes again, and now three slots are free
    orb_entity_free_despawning();
    CHECK_EQ(orb_entity_all(list, 8), 3);
    orb_entity_get(list[0])->flags |= ORB_ENTITY_PERSISTENT;
    orb_level_despawn(room);
    orb_entity_free_despawning();
    CHECK_EQ(orb_entity_all(list, 8), 1);
    CHECK(orb_entity_get(list[0])->flags & ORB_ENTITY_PERSISTENT);
    orb_level_spawn(ORB_NO_LEVEL);
    CHECK_EQ(orb_entity_all(list, 8), 1);

    // ORB_ENTITY_NEW while an update is in progress
    orb_entity_updating = true;
    orb_entity_id fresh = orb_entity_spawn(marker, (orb_vec2f) {0, 0});
    CHECK(orb_entity_get(fresh)->flags & ORB_ENTITY_NEW);
    orb_entity_updating = false;

    // revalidation: a placement kept by iid and type, re-found when it moved, dropped
    // when it is gone
    orb_level_spawn(room);
    orb_entity_free_despawning();
    orb_entity* placed =
        orb_entity_get(orb_entity_of_type(crate, list, 8) ? list[0] : ORB_NO_ENTITY);
    CHECK(placed && placed->placement == 0);
    orb_entity_revalidate();
    CHECK_EQ(placed->placement, 0);
    orb_placement_desc swapped = placements[0];
    placements[0] = placements[1];
    placements[1] = swapped;
    orb_entity_revalidate();
    CHECK_EQ(placed->placement, 1);
    CHECK_EQ(orb_entity_field_int(placed->self, "hp", 0), 3);
    placements[1].iid = 0xdead;
    orb_entity_revalidate();
    CHECK_EQ(placed->placement, ORB_NO_INDEX);
    CHECK_EQ(orb_entity_field_int(placed->self, "hp", 0), 10);

    // F1: a recast that swaps two type definitions bumps both their generations; a crate
    // still linked to its placement picks up its placement's new type index and
    // generation, an override field unaffected
    orb_entity_id live[8];
    int live_n = orb_entity_all(live, 8);

    for (int i = 0; i < live_n; i++)
        orb_entity_despawn(live[i]);

    orb_entity_free_despawning();
    orb_level_spawn(room);
    orb_entity_free_despawning();

    orb_entity_id others[8];
    CHECK_EQ(orb_entity_of_type(crate, others, 8), 2);
    orb_entity* other = orb_entity_get(others[1]);
    orb_type_desc swapped_type = types[0];
    types[0] = types[1];
    types[1] = swapped_type;
    uint64_t swapped_id = type_ids[0];
    type_ids[0] = type_ids[1];
    type_ids[1] = swapped_id;
    placements[0].type = 1;
    placements[1].type = 1;
    placements[2].type = 0;
    orb_asset_set(&table, &as);
    orb_entity_revalidate();
    CHECK_EQ(ORB_HANDLE_INDEX(other->type), 1);
    CHECK_EQ(orb_entity_field_int(other->self, "hp", 0), 3);
    CHECK_EQ(
        orb_entity_of_type(ORB_TYPE(1 | (uint32_t)table.assets.type_gens[1] << 24), list, 8), 2
    );
    CHECK(list[1].v == other->self.v);

    // a reset with a different component list empties the pool; one past the region fails
    c.components[1] = 8;
    CHECK(orb_entity_reset(&c));
    CHECK_EQ(p->kinds, 5);
    CHECK_EQ(orb_entity_all(list, 8), 0);
    CHECK(orb_entity_type_fns(0)->init == nullptr);
    c.max_entities = 1 << 16;
    CHECK(!orb_entity_reset(&c));

    return 0;
}

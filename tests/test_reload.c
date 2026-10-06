#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

typedef struct test_state {
    arena battle;
    u32* numbers;
} test_state;

static_assert(sizeof(test_state) <= 128 - 8);

static usize test_global = 0;
static usize test_state_size = 128;
static int test_init_count, test_reload_count;
static u16 test_component_size = 8;
static u32 test_max_entities;

static orb_config test_config(void) {
    return (orb_config) {
        .state_size = test_state_size,
        .state_version = 1,
        .max_entities = test_max_entities,
        .components = {test_component_size},
        .memory = {.global = test_global},
    };
}

static void test_init(void* state, const orb_api* orb) {
    test_state* self = state;

    test_init_count++;

    if (!arena_new(&self->battle, orb->global_arena, "battle", 4096)) return;

    self->numbers = alloc(&self->battle, u32, 4);
    self->numbers[0] = 42;
}

static void test_reload(void* state, const orb_api* orb) {
    (void)state, (void)orb;

    test_reload_count++;
}

static void test_update(void* state, const orb_api* orb) {
    (void)state, (void)orb;
}

static const orb_game test_game = {test_config, test_init, test_reload, test_update, test_update};

int main(void) {
    orb_error err;

    if (!orb_boot(&test_game, "examples/demo", (u8_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    // boot: init sets up the state, then reload binds names, as after any recast
    CHECK_EQ(test_init_count, 1);
    CHECK_EQ(test_reload_count, 1);

    // after boot: the game arena is recorded and its memory holds what init wrote
    test_state* self = (test_state*)host_state.base;

    CHECK_EQ(self->numbers[0], 42);
    CHECK(orb_arena_recorded(0) == &self->battle);
    CHECK(strcmp(self->battle.name, "battle") == 0);

    // same size and version
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 1);
    CHECK_EQ(test_reload_count, 2);

    // a code reload keeps the arenas and what is in them
    CHECK_EQ(self->numbers[0], 42);
    CHECK(orb_arena_recorded(0) == &self->battle);

    // so does a recast
    orb_error recast_err;

    CHECK(orb_recast(&recast_err));
    CHECK_EQ(self->numbers[0], 42);

    // the state struct grew (a field was added) without a version bump
    // orb treats that as a new version, resets the state, and keeps the session alive
    test_state_size = 128 + 16;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 2);
    CHECK_EQ(test_reload_count, 4);

    // the reset cleared global, so init made battle again at global's start
    CHECK_EQ(orb_api_global()->used, 4096);
    CHECK(orb_arena_recorded(0) == &self->battle);
    CHECK(orb_arena_recorded(1) == nullptr);

    // shrinking is a layout change too
    test_state_size = 128 - 8;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 3);

    // a changed component size resets the pool with the state; so does max_entities
    test_component_size = 12;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 4);
    CHECK_EQ(orb_entity_pool()->sizes[ORB_COMPONENT_GAME], 12);
    CHECK_EQ(orb_entity_pool()->max, 256); // 0 in the config means 256

    // the state and the pool grow far past their boot size without a restart; the
    // state pointer stays put and the game sees zeroed bytes
    u8* before = host_state.base;

    before[sizeof(test_state)] = 7;
    test_state_size = 1 << 20;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 5);
    CHECK(host_state.base == before);
    CHECK_EQ(host_state.used, 1 << 20);
    CHECK_EQ(before[sizeof(test_state)], 0);
    CHECK_EQ(before[(1 << 20) - 1], 0);

    test_max_entities = ORB_MAX_ENTITIES;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 6);
    CHECK_EQ(orb_entity_pool()->max, ORB_MAX_ENTITIES);

    // a changed memory size is a state reset; an over-limit one is refused
    int resets = test_init_count;

    test_global = 2 * MB;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, resets + 1);
    CHECK_EQ(orb_api_global()->size, 2 * MB);
    test_global = ORB_REGION_RESERVE + 1;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, resets + 1);
    CHECK_EQ(orb_api_global()->size, 2 * MB);
    test_global = 2 * MB;

    orb_quit();

    // quit gives every region back, so a second boot starts clean
    CHECK(host_state.base == nullptr && host_pool.base == nullptr);
    CHECK(host_scratch.base == nullptr && host_assets[0].base == nullptr);

    test_global = ORB_REGION_RESERVE + 1;
    CHECK(!orb_boot(&test_game, "examples/demo", (u8_span) {}, &err));
    CHECK(strstr(err.text, "memory.global") != nullptr);
    orb_quit();

    return 0;
}

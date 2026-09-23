#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static size_t test_state_size = 64;
static int test_init_count, test_reload_count;
static uint16_t test_component_size = 8;
static uint32_t test_max_entities;

static orb_config test_config(void) {
    return (orb_config) {
        .state_size = test_state_size,
        .state_version = 1,
        .max_entities = test_max_entities,
        .components = {test_component_size}
    };
}

static void test_init(void* state, const orb_api* orb) {
    (void)state, (void)orb;

    test_init_count++;
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

    if (!orb_boot(&test_game, "examples/demo", (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    // boot: init sets up the state, then reload binds names, as after any recast
    CHECK_EQ(test_init_count, 1);
    CHECK_EQ(test_reload_count, 1);

    // same size and version
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 1);
    CHECK_EQ(test_reload_count, 2);

    // the state struct grew (a field was added) without a version bump
    // orb treats that as a new version, resets the state, and keeps the session alive
    test_state_size = 64 + 16;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 2);
    CHECK_EQ(test_reload_count, 3);

    // shrinking is a layout change too
    test_state_size = 64 - 8;
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
    uint8_t* before = host_state.base;

    before[0] = 7;
    test_state_size = 1 << 20;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 5);
    CHECK(host_state.base == before);
    CHECK_EQ(host_state.used, 1 << 20);
    CHECK_EQ(before[0], 0);
    CHECK_EQ(before[(1 << 20) - 1], 0);

    test_max_entities = ORB_MAX_ENTITIES;
    orb_set_game(&test_game);
    CHECK_EQ(test_init_count, 6);
    CHECK_EQ(orb_entity_pool()->max, ORB_MAX_ENTITIES);

    orb_quit();

    // quit gives every region back, so a second boot starts clean
    CHECK(host_state.base == nullptr && host_pool.base == nullptr);
    CHECK(host_scratch.base == nullptr && host_assets[0].base == nullptr);

    return 0;
}

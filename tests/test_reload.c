#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// A stand-in game whose state size the test can change between reloads.
static size_t test_state_size = 64;
static int test_init_count, test_reload_count;

static orb_config test_config(void) {
    return (orb_config) {.state_size = test_state_size, .state_version = 1};
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

    if (!orb_run_boot(&test_game, "examples/hello", &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    CHECK_EQ(test_init_count, 1);

    // same size and version: a plain reload
    orb_run_set_game(&test_game);
    CHECK_EQ(test_init_count, 1);
    CHECK_EQ(test_reload_count, 1);

    // the state struct grew (a field was added) without a version bump: orb treats
    // that as a new version, resets the state, and keeps the session alive
    test_state_size = 64 + 16;
    orb_run_set_game(&test_game);
    CHECK_EQ(test_init_count, 2);
    CHECK_EQ(test_reload_count, 1);

    // shrinking is a layout change too
    test_state_size = 64 - 8;
    orb_run_set_game(&test_game);
    CHECK_EQ(test_init_count, 3);

    orb_os_close();
    return 0;
}

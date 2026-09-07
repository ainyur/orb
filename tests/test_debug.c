#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static void debug_swallow(const char* line) {
    (void)line;
}

int main(void) {
    uint8_t a[] = "a", b[] = "b";
    const char* path = "build/scratch/watch.txt";

    CHECK(orb_os_write_file(path, (orb_span) {a, 1}));

    orb_watch w;
    orb_watch_init(&w, path);

    CHECK(!orb_watch_poll(&w, 0));

    orb_os_sleep(10000000); // 10 ms so the new mtime differs

    CHECK(orb_os_write_file(path, (orb_span) {b, 1}));
    CHECK(!orb_watch_poll(&w, 1000000000)); // first seen: pending
    CHECK(!orb_watch_poll(&w, 1100000000)); // 100 ms: not settled
    CHECK(orb_watch_poll(&w, 1300000000));  // 300 ms: fires
    CHECK(!orb_watch_poll(&w, 1400000000)); // fires once

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {a, 1}));
    CHECK(!orb_watch_poll(&w, 2000000000));

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {b, 1}));
    CHECK(!orb_watch_poll(&w, 2150000000));
    CHECK(!orb_watch_poll(&w, 2300000000));
    CHECK(orb_watch_poll(&w, 2400000000));

    remove(path);

    CHECK(!orb_watch_poll(&w, 3000000000));
    CHECK(!orb_watch_poll(&w, 3300000000));

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {a, 1}));
    CHECK(!orb_watch_poll(&w, 3400000000));
    CHECK(orb_watch_poll(&w, 3700000000));
    CHECK_EQ(orb_os_run("make -s -C examples/demo build/game" ORB_OS_LIB_SUFFIX, debug_swallow), 0);
    CHECK_EQ(debug_boot("examples/demo"), 0);
    CHECK(strcmp(debug_so_path, "examples/demo/build/game" ORB_OS_LIB_SUFFIX) == 0);
    CHECK(strncmp(debug_copy_path, "examples/demo/build/", 20) == 0);
    CHECK(strstr(debug_copy_path, ORB_OS_LIB_SUFFIX) != nullptr);
    CHECK(orb_os_file_mtime(debug_copy_path) != 0);

    debug_finish();

    CHECK_EQ(orb_os_file_mtime(debug_copy_path), 0);
    CHECK(orb_os_make_dir("build/scratch/with space"));

    const char* makefile = "all:\n\t@true\n";
    CHECK(orb_os_write_file(
        "build/scratch/with space/Makefile",
        (orb_span) {
            (uint8_t*)makefile,
            strlen(makefile)
        })
    );

    snprintf(debug_dir, sizeof debug_dir, "%s", "build/scratch/with space");

    CHECK(debug_build());
    CHECK(debug_is_make_noise("make: *** [Makefile:4: game.so] Error 1"));
    CHECK(debug_is_make_noise("make[1]: Leaving directory '/x'"));
    CHECK(!debug_is_make_noise("game.c:31:17: error: expected ';'"));

    return 0;
}

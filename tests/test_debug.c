#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static void debug_swallow(const char* line) {
    (void)line;
}

static orb_span span(const char* s) {
    return (orb_span) {(uint8_t*)s, strlen(s)};
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

    // the asset watch is what the cast read: the manifest, then every file and
    // directory it opened, so a file added to a directory recasts too
    debug_watch_assets();
    CHECK_EQ(debug_assets.count, 8);
    CHECK(strcmp(debug_assets.at[0].path, "examples/demo/orb.json") == 0);
    CHECK(strcmp(debug_assets.at[1].path, "examples/demo/art/palette.aseprite") == 0);
    CHECK(strcmp(debug_assets.at[2].path, "examples/demo/art") == 0);
    CHECK(strcmp(debug_assets.at[7].path, "examples/demo/music/song.wav") == 0);

    debug_finish();

    CHECK_EQ(orb_os_file_mtime(debug_copy_path), 0);
    CHECK(orb_os_make_dir("build/scratch/with space"));

    const char* makefile = "all:\n\t@false\nbuild/game" ORB_OS_LIB_SUFFIX ":\n\t@true\n";
    CHECK(orb_os_write_file(
        "build/scratch/with space/Makefile", (orb_span) {(uint8_t*)makefile, strlen(makefile)}
    ));

    snprintf(debug_dir, sizeof debug_dir, "%s", "build/scratch/with space");

    CHECK(debug_build());
    CHECK(debug_is_make_noise("make: *** [Makefile:4: game.so] Error 1"));
    CHECK(debug_is_make_noise("make[1]: Leaving directory '/x'"));
    CHECK(!debug_is_make_noise("game.c:31:17: error: expected ';'"));
    CHECK(!debug_is_make_noise("make: *** No rule to make target 'build/game.so'.  Stop."));

    CHECK(orb_os_make_dir("build/scratch/deps"));
    snprintf(debug_dir, sizeof debug_dir, "%s", "build/scratch/deps");
    remove("build/scratch/deps/build/a.d");
    remove("build/scratch/deps/build/b.d");
    CHECK(!debug_watch_sources());
    CHECK(orb_os_make_dir("build/scratch/deps/build"));

    const char *a_d = "build/a.o: a.c ../inc/h.h\n", *b_d = "build/b.o: b.c \\\n ../inc/h.h\n";
    CHECK(orb_os_write_file("build/scratch/deps/build/a.d", span(a_d)));
    CHECK(orb_os_write_file("build/scratch/deps/build/b.d", span(b_d)));
    CHECK(debug_watch_sources());
    CHECK_EQ(debug_sources.count, 3);
    CHECK(strcmp(debug_sources.at[0].path, "build/scratch/deps/a.c") == 0);
    CHECK(strcmp(debug_sources.at[1].path, "build/scratch/deps/../inc/h.h") == 0);
    CHECK(strcmp(debug_sources.at[2].path, "build/scratch/deps/b.c") == 0);

    const char* text = "build/demo.o: demo.c ../../src/orb.h \\\n"
                       " ../../src/core/api.h with\\ space.h\n"
                       "demo.c:\n"
                       "../../src/orb.h:\n"
                       "build/other.o: other.c ../../src/orb.h /abs/x.h\n";

    snprintf(debug_dir, sizeof debug_dir, "%s", "game");
    debug_sources.count = 0;
    debug_watch_depfile(span(text));

    CHECK_EQ(debug_sources.count, 6);
    CHECK(strcmp(debug_sources.at[0].path, "game/demo.c") == 0);
    CHECK(strcmp(debug_sources.at[1].path, "game/../../src/orb.h") == 0);
    CHECK(strcmp(debug_sources.at[2].path, "game/../../src/core/api.h") == 0);
    CHECK(strcmp(debug_sources.at[3].path, "game/with space.h") == 0);
    CHECK(strcmp(debug_sources.at[4].path, "game/other.c") == 0);
    CHECK(strcmp(debug_sources.at[5].path, "/abs/x.h") == 0);

    debug_watch_depfile(span("build/z.o: other.c z.c\n"));
    CHECK_EQ(debug_sources.count, 7);
    CHECK(strcmp(debug_sources.at[6].path, "game/z.c") == 0);

    debug_watch_depfile(span("  \n\n"));
    CHECK_EQ(debug_sources.count, 7);

    return 0;
}

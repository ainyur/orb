#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#ifndef _WIN32
#include <fcntl.h>
#endif

static void ignore_line(const char* line) {
    (void)line;
}

static orb_span span(const char* text) {
    return (orb_span) {(uint8_t*)text, strlen(text)};
}

int main(void) {
    uint8_t byte_a[] = "a", byte_b[] = "b";
    const char* path = "build/scratch/watch.txt";

    CHECK(orb_os_write_file(path, (orb_span) {byte_a, 1}));

    orb_watch watch;
    orb_watch_init(&watch, path);

    CHECK(!orb_watch_poll(&watch, 0));

    orb_os_sleep(10000000); // 10 ms so the new mtime differs

    CHECK(orb_os_write_file(path, (orb_span) {byte_b, 1}));
    CHECK(!orb_watch_poll(&watch, 1000000000)); // first seen: pending
    CHECK(!orb_watch_poll(&watch, 1100000000)); // 100 ms: not settled
    CHECK(orb_watch_poll(&watch, 1300000000));  // 300 ms: fires
    CHECK(!orb_watch_poll(&watch, 1400000000)); // fires once

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {byte_a, 1}));
    CHECK(!orb_watch_poll(&watch, 2000000000));

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {byte_b, 1}));
    CHECK(!orb_watch_poll(&watch, 2150000000));
    CHECK(!orb_watch_poll(&watch, 2300000000));
    CHECK(orb_watch_poll(&watch, 2400000000));

    remove(path);

    CHECK(!orb_watch_poll(&watch, 3000000000));
    CHECK(!orb_watch_poll(&watch, 3300000000));

    orb_os_sleep(10000000);

    CHECK(orb_os_write_file(path, (orb_span) {byte_a, 1}));
    CHECK(!orb_watch_poll(&watch, 3400000000));
    CHECK(orb_watch_poll(&watch, 3700000000));
    const char* build_argv[] = {"make", "-s", "build/game" ORB_OS_LIB_SUFFIX, nullptr};

    CHECK_EQ(orb_os_run("examples/demo", build_argv, ignore_line), 0);

    // the copy is named per process, and a leftover at that name is replaced by a
    // new file rather than truncated, since another orb may have it mapped
    char copy_name[64];
    orb_path leftover;

    snprintf(copy_name, sizeof copy_name, ".orb-game-%u-0" ORB_OS_LIB_SUFFIX, orb_os_pid());
    orb_path_join(leftover, "examples/demo/build", copy_name);
    CHECK(orb_os_write_file(leftover, (orb_span) {byte_a, 1}));
#ifndef _WIN32
    // Held open across the boot. Inode numbers cannot express this: a freed one may be
    // reused, so an equal number proves nothing either way.
    int held = open(leftover, O_RDONLY);

    CHECK(held >= 0);
#endif
    CHECK_EQ(debug_boot("examples/demo"), 0);

    orb_log_clear();
    orb_console_run("watch");
    CHECK(strstr(orb_log_line(0), "sources") != nullptr);
    orb_console_run("stats");
    CHECK(strstr(orb_log_line(0), "state") != nullptr);
    orb_console_run("pause 1");
    CHECK(orb_clock_get()->paused);
    orb_console_run("pause 0");
    orb_console_run("timescale 0.5");
    CHECK(orb_clock_get()->timescale == 0.5f);
    orb_console_run("timescale 1");
    orb_console_run("step");
    CHECK(orb_clock_get()->step);
    orb_clock_get()->step = false;

    CHECK(strcmp(debug_so_path, "examples/demo/build/game" ORB_OS_LIB_SUFFIX) == 0);
    CHECK(strcmp(debug_copy_path, leftover) == 0);
    CHECK(orb_os_file_mtime(debug_copy_path) != 0);
#ifndef _WIN32
    // orb_os_copy_file opens with "wb", so without the loader's remove() this descriptor
    // would read the new library instead of the byte the leftover was written with.
    uint8_t held_byte = 0;

    CHECK_EQ(pread(held, &held_byte, 1, 0), 1);
    CHECK_EQ(held_byte, byte_a[0]);
    CHECK_EQ(close(held), 0);
#endif

    // the asset watch is what the cast read: the manifest, then every file and
    // directory it opened, so a file added to a directory recasts too
    debug_watch_assets();
    CHECK_EQ(debug_assets.count, 12);
    CHECK(strcmp(debug_assets.at[0].path, "examples/demo/orb.json") == 0);
    CHECK(strcmp(debug_assets.at[1].path, "examples/demo/art/palette.aseprite") == 0);
    CHECK(strcmp(debug_assets.at[2].path, "examples/demo/levels/world.ldtk") == 0);
    CHECK(strcmp(debug_assets.at[3].path, "examples/demo/fonts") == 0);
    CHECK(strcmp(debug_assets.at[4].path, "examples/demo/fonts/body.aseprite") == 0);
    CHECK(strcmp(debug_assets.at[5].path, "examples/demo/art") == 0);
    CHECK(strcmp(debug_assets.at[7].path, "examples/demo/levels/tiles.aseprite") == 0);
    CHECK(strcmp(debug_assets.at[11].path, "examples/demo/music/song.wav") == 0);

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

    const char *dep_a = "build/a.o: a.c ../inc/h.h\n", *dep_b = "build/b.o: b.c \\\n ../inc/h.h\n";
    CHECK(orb_os_write_file("build/scratch/deps/build/a.d", span(dep_a)));
    CHECK(orb_os_write_file("build/scratch/deps/build/b.d", span(dep_b)));
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

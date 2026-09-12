#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static char run_lines[8][64];
static int run_line_count;

static void run_line(const char* line) {
    snprintf(run_lines[run_line_count++], 64, "%s", line);
}

int main(int argc, char** argv) {
    static uint8_t mem[1 << 16];

    // test-wine passes a UTF-8 argument
    orb_os_args(&argc, &argv);

    if (argc > 1) CHECK(strcmp(argv[1], "h\xc3\xa9llo") == 0);

    orb_arena a;

    orb_arena_init(&a, "test", mem, sizeof mem);

    // input edges
    orb_input in = {0};

    orb_input_step(&in);
    in.down[ORB_BTN_A] = true;
    orb_input_step(&in);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(orb_button_pressed(ORB_BTN_A));
    CHECK(!orb_button_released(ORB_BTN_A));
    orb_input_step(&in);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(!orb_button_pressed(ORB_BTN_A));
    in.down[ORB_BTN_A] = false;
    orb_input_step(&in);
    CHECK(!orb_button_down(ORB_BTN_A));
    CHECK(orb_button_released(ORB_BTN_A));

    // files
    uint8_t bytes[] = {1, 2, 3};
    CHECK(orb_os_write_file("build/scratch/os.bin", (orb_span) {bytes, 3}));
    orb_span back;

    CHECK(orb_os_read_file("build/scratch/os.bin", &a, &back));
    CHECK_EQ(back.len, 3);
    CHECK_EQ(back.ptr[2], 3);
    CHECK_EQ(back.ptr[3], 0); // NUL after the data
    CHECK(!orb_os_read_file("build/scratch/does-not-exist", &a, &back));
    CHECK(orb_os_file_mtime("build/scratch/os.bin") > 0);
    CHECK_EQ(orb_os_file_mtime("build/scratch/does-not-exist"), 0);

    // copy a file byte for byte: how scry loads a copy of the game library
    CHECK(orb_os_copy_file("build/scratch/os.bin", "build/scratch/os-copy.bin"));
    CHECK(orb_os_read_file("build/scratch/os-copy.bin", &a, &back));
    CHECK_EQ(back.len, 3);
    CHECK_EQ(back.ptr[1], 2);
    CHECK(!orb_os_copy_file("build/scratch/does-not-exist", "build/scratch/nope"));
    CHECK_EQ(orb_os_file_mtime("build/scratch/nope"), 0); // no half-made target

    // UTF-8 paths
    CHECK(orb_os_make_dir("build/scratch/h\xc3\xa9llo"));
    CHECK(orb_os_write_file("build/scratch/h\xc3\xa9llo/\xc3\xbc.bin", (orb_span) {bytes, 3}));
    CHECK(orb_os_read_file("build/scratch/h\xc3\xa9llo/\xc3\xbc.bin", &a, &back));
    CHECK_EQ(back.len, 3);
    CHECK(orb_os_file_mtime("build/scratch/h\xc3\xa9llo/\xc3\xbc.bin") > 0);
    CHECK(orb_os_copy_file(
        "build/scratch/h\xc3\xa9llo/\xc3\xbc.bin", "build/scratch/h\xc3\xa9llo/\xc3\xb6.bin"
    ));

    // directory listing
    orb_path names[8];

    CHECK_EQ(orb_os_list_dir("build/scratch/h\xc3\xa9llo", ".bin", names, 8), 2);
    CHECK(strcmp(names[0], "build/scratch/h\xc3\xa9llo/\xc3\xb6.bin") == 0);
    CHECK(strcmp(names[1], "build/scratch/h\xc3\xa9llo/\xc3\xbc.bin") == 0);

#ifdef _WIN32
    orb_path longname;
    int at = snprintf(longname, sizeof longname, "build/scratch/h\xc3\xa9llo/long/");

    CHECK(orb_os_make_dir("build/scratch/h\xc3\xa9llo/long"));

    for (int i = 0; i < 120; i++)
        at += snprintf(longname + at, sizeof longname - (size_t)at, "\xc3\xa9");

    snprintf(longname + at, sizeof longname - (size_t)at, ".bin");
    CHECK(orb_os_write_file(longname, (orb_span) {bytes, 3}));
    CHECK_EQ(orb_os_list_dir("build/scratch/h\xc3\xa9llo/long", ".bin", names, 8), 1);
#endif

    CHECK_EQ(orb_os_list_dir("tests/fixtures/art", ".aseprite", names, 8), 2);
    CHECK(strcmp(names[0], "tests/fixtures/art/palette.aseprite") == 0);
    CHECK(strcmp(names[1], "tests/fixtures/art/player.aseprite") == 0);
    CHECK_EQ(orb_os_list_dir("build/scratch/does-not-exist", ".c", names, 8), 0);

    // an empty suffix lists every entry but the dot ones, subdirectories included
    CHECK(orb_os_make_dir("build/scratch/walk"));
    CHECK(orb_os_make_dir("build/scratch/walk/dir"));
    CHECK(orb_os_write_file("build/scratch/walk/a.txt", (orb_span) {bytes, 3}));
    CHECK_EQ(orb_os_list_dir("build/scratch/walk", "", names, 8), 2);
    CHECK(strcmp(names[0], "build/scratch/walk/a.txt") == 0);
    CHECK(strcmp(names[1], "build/scratch/walk/dir") == 0);
    CHECK(orb_os_is_dir("build/scratch/walk/dir"));
    CHECK(!orb_os_is_dir("build/scratch/walk/a.txt"));
    CHECK(!orb_os_is_dir("build/scratch/does-not-exist"));

    // clock
    uint64_t t0 = orb_os_ticks();

    orb_os_sleep(2000000);
    CHECK(orb_os_ticks() - t0 >= 2000000);

    // headless window
    orb_os_config cfg = {.title = "test", .size_w = 4, .size_h = 2};
    CHECK(orb_os_open(&cfg));
    orb_input scripted = {0};

    scripted.down[ORB_BTN_RIGHT] = true;
    orb_os_headless_set_input(&scripted);

    orb_input pumped;

    CHECK(orb_os_pump(&pumped));
    CHECK(pumped.down[ORB_BTN_RIGHT]);

    uint32_t rgb[8] = {0xff0000, 1, 2, 3, 4, 5, 6, 0x0000ff};

    orb_os_present(rgb);
    CHECK_EQ(orb_os_headless_frame()[0], 0xff0000);
    CHECK_EQ(orb_os_headless_frame()[7], 0x0000ff);
    orb_os_close();
    // create a directory for outputs; succeeding when it already exists
    CHECK(orb_os_make_dir("build/scratch/made"));
    CHECK(orb_os_make_dir("build/scratch/made"));
    CHECK(orb_os_write_file("build/scratch/made/x", (orb_span) {(uint8_t*)"x", 1}));

    // run a command and receive its combined output one line at a time, plus its status
    run_line_count = 0;
#ifdef _WIN32
    CHECK_EQ(orb_os_run("echo a&echo make: *** boom&echo b&exit 3", run_line), 3);
#else
    CHECK_EQ(orb_os_run("printf 'a\nmake: *** boom\nb\n'; exit 3", run_line), 3);
#endif
    CHECK_EQ(run_line_count, 3);
    CHECK(strcmp(run_lines[1], "make: *** boom") == 0);
    CHECK(strcmp(run_lines[2], "b") == 0);
    orb_path joined;
    orb_path_join(joined, "game", "art/x.aseprite");
    CHECK(strcmp(joined, "game/art/x.aseprite") == 0);
    orb_path_join(joined, "game", "/abs/x.h");
    CHECK(strcmp(joined, "/abs/x.h") == 0);

    return 0;
}

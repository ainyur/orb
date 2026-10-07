#include "test.h"
#define ORB_OS_HEADLESS 1
#define ORB_RELEASE 1

static bool test_trapped;
#define orb_arena_trap(...) (test_trapped = true)

#include "../src/orb.c"
// clang-format off
#include "../src/cast/json.c"
#include "../src/cast/inflate.c"
#include "../src/cast/aseprite.c"
#include "../src/cast/pack.c"
#include "../src/cast/wav.c"
#include "../src/cast/ldtk.c"
#include "../src/cast/cast.c"
// clang-format on
#include "fixtures/game.c"

int main(void) {
    static alignas(16) u8 scratch_mem[4 << 20], out_mem[1 << 20];
    orb_arena scratch, out;
    orb_manifest manifest;
    orb_cast_result result;
    orb_error err;

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    if (!orb_cast_game(&scratch, &out, "tests/fixtures", &manifest, &result, &err)) {
        fprintf(stderr, "cast: %s\n", err.text);
        return 1;
    }

    // the one block orb_host_release_size sizes holds the whole release boot
    if (!orb_boot(orb_game_main(), nullptr, result.file, (orb_size) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    CHECK(orb_frame());

    // filling the exactly sized block is not a warning
    for (int i = 0; i < orb_log_line_count(); i++) {
        const char* line = orb_log_line(i);
        const char* at = strstr(line, " at ");

        CHECK(!(at && strstr(at, "% of")));
    }

    // the release block holds both game arenas at their configured sizes
    orb_arena* global = orb_api_global();
    orb_arena* frame = orb_api_frame();

    CHECK_EQ(global->size, MB);
    CHECK_EQ(frame->size, 256 * KB);
    CHECK(
        global->base >= host_arena.base &&
        global->base + global->size <= host_arena.base + host_arena.size
    );
    CHECK(
        frame->base >= host_arena.base &&
        frame->base + frame->size <= host_arena.base + host_arena.size
    );
    CHECK(host_arena.used <= host_arena.size);

    // a result that does not fit comes back empty, with failed set
    orb_arena none;

    orb_arena_init(&none, "none", nullptr, 0);
    CHECK_EQ(orb_entity_all(&none).len, 0);
    CHECK(none.failed);
    CHECK(!test_trapped);

    orb_quit();

    CHECK(host_arena.base == nullptr);
    return 0;
}

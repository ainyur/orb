#include "test.h"
#define ORB_OS_HEADLESS 1
#define ORB_RELEASE 1
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
    static alignas(16) uint8_t scratch_mem[4 << 20], out_mem[1 << 20];
    orb_arena scratch, out;
    orb_manifest m;
    orb_cast_result r;
    orb_error err;

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    if (!orb_cast_game(&scratch, &out, "tests/fixtures", &m, &r, &err)) {
        fprintf(stderr, "cast: %s\n", err.text);
        return 1;
    }

    // the one block orb_host_release_size sizes holds the whole release boot
    if (!orb_boot(orb_game_main(), nullptr, r.file, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    CHECK(orb_frame());
    CHECK(host_arena.used <= host_arena.size);

    orb_quit();

    CHECK(host_arena.base == nullptr);
    return 0;
}

#include "orb.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ORB_RELEASE
static const alignas(16) uint8_t main_sealed[] = {
#embed "game.orb"
};

int main(void) {
    orb_error err;
    orb_span sealed = {main_sealed, sizeof main_sealed};

    if (!orb_run_boot(orb_game_main(), nullptr, sealed, &err)) {
        orb_log("%s", err.text);
        return 1;
    }

    orb_run_loop(nullptr);
    orb_os_close();

    return 0;
}
#else
#define ORB_VERSION "0.1.0"

static int usage(FILE* to) {
    fprintf(
        to, "usage: orb <verb> [args]\n"
            "\n"
            "  cast [game_dir]         cast the art once and report what it yields\n"
            "  run  [game_dir]         run the game without watching\n"
            "  scry [game_dir]         run the game and rebuild, reload, and recast on save\n"
            "  seal [game_dir] [out]   write the .orb, to bin/<id>.orb by default\n"
            "  help                    print this help\n"
            "  version                 print the version\n"
            "\n"
            "game_dir defaults to the current directory.\n"
    );

    return to == stdout ? 0 : 2;
}

static int cast_once(const char* dir, bool seal, const char* out_path) {
    static uint8_t boot_mem[1 << 18];
    orb_arena boot;
    orb_arena_init(&boot, "boot", boot_mem, sizeof boot_mem);
    orb_error err;
    orb_manifest m;

    if (!orb_manifest_load(&boot, dir, &m, &err)) {
        fprintf(stderr, "orb: %s\n", err.text);
        return 1;
    }

    orb_arena scratch, out;

    orb_arena_init(&scratch, "cast scratch", malloc(m.asset_headroom), m.asset_headroom);
    orb_arena_init(&out, "asset", malloc(m.asset_headroom), m.asset_headroom);

    orb_cast_result result;

    if (!orb_cast_game(&scratch, &out, dir, &m, &result, &err)) {
        fprintf(stderr, "orb: %s\n", err.text);
        return 1;
    }

    if (seal && !out_path) {
        orb_path bin;

        orb_path_join(bin, dir, "bin");

        if (!orb_os_make_dir(bin)) {
            fprintf(stderr, "orb: cannot create %s\n", bin);
            return 1;
        }

        out_path = orb_seal_path(&scratch, dir, &m);
    }

    if (out_path && !orb_os_write_file(out_path, result.file)) {
        fprintf(stderr, "orb: cannot write %s\n", out_path);
        return 1;
    }

    printf(
        "%s %u sprites, %u animations (%zu bytes)\n", out_path ? "sealed" : "cast",
        result.sprite_count, result.animation_count, result.file.len
    );

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage(stderr);

    const char* verb = argv[1];
    const char* dir = argc > 2 ? argv[2] : ".";
    bool help = argc > 2 && strcmp(argv[2], "--help") == 0;

    if (strcmp(verb, "help") == 0 || strcmp(verb, "--help") == 0 || help) return usage(stdout);

    if (strcmp(verb, "version") == 0 || strcmp(verb, "--version") == 0) {
        printf("orb %s\n", ORB_VERSION);
        return 0;
    }

    if (strcmp(verb, "scry") == 0 && argc <= 3) return orb_debug_scry(dir);
    if (strcmp(verb, "run") == 0 && argc <= 3) return orb_debug_run(dir);
    if (strcmp(verb, "cast") == 0 && argc <= 3) return cast_once(dir, false, nullptr);
    if (strcmp(verb, "seal") == 0 && argc <= 4)
        return cast_once(dir, true, argc == 4 ? argv[3] : nullptr);

    fprintf(stderr, "orb: unknown verb or arguments\n");

    return usage(stderr);
}
#endif

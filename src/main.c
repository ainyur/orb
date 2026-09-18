#include "orb.c"

#include <stdio.h>
#include <string.h>

#ifdef ORB_RELEASE
static const alignas(16) uint8_t main_sealed[] = {
#embed "game.orb"
};

int main(void) {
    orb_error err;
    orb_span sealed = {main_sealed, sizeof main_sealed};

    if (!orb_boot(orb_game_main(), nullptr, sealed, &err)) {
        orb_log("%s", err.text);
        return 1;
    }

    while (orb_frame())
        continue;

    orb_quit();

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

int main(int argc, char** argv) {
    orb_os_args(&argc, &argv);

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
    if (strcmp(verb, "cast") == 0 && argc <= 3) return orb_debug_cast(dir, false, nullptr);
    if (strcmp(verb, "seal") == 0 && argc <= 4)
        return orb_debug_cast(dir, true, argc == 4 ? argv[3] : nullptr);

    orb_log("orb: unknown verb or arguments");

    return usage(stderr);
}
#endif

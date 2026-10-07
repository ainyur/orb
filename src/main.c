#include "orb.c"

#include <stdio.h>
#include <string.h>

#ifdef ORB_RELEASE
static const alignas(16) u8 main_sealed[] = {
#embed "game.orb"
};

// A release game takes --window and ignores every other argument.
int main(int argc, char** argv) {
    orb_error err;
    orb_size window = {};
    u8_span sealed = {main_sealed, sizeof main_sealed};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--window") != 0) continue;

        if (i + 1 == argc || !orb_window_parse(argv[++i], &window)) {
            orb_log("orb: --window takes WIDTHxHEIGHT, such as 1280x720");
            return 2;
        }
    }

    if (!orb_boot(orb_game_main(), nullptr, sealed, window, &err)) {
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
        to,
        "usage: orb <verb> [args]\n"
        "\n"
        "  cast [game_dir]         cast the art once and report what it yields\n"
        "  run  [game_dir]         run the game without watching\n"
        "  scry [game_dir]         run the game and rebuild, reload, and recast on save\n"
        "  seal [game_dir] [out]   write the .orb, to bin/<id>.orb by default\n"
        "  help                    print this help\n"
        "  version                 print the version\n"
        "\n"
        "game_dir defaults to the current directory. run and scry take --window WIDTHxHEIGHT to\n"
        "open the window at that size.\n"
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

    if (strcmp(verb, "run") == 0 || strcmp(verb, "scry") == 0) {
        const char* game_dir = nullptr;
        orb_size window = {};
        bool extra = false;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--window") == 0) {
                if (i + 1 == argc || !orb_window_parse(argv[++i], &window)) {
                    orb_log("orb: --window takes WIDTHxHEIGHT, such as 1280x720");
                    return usage(stderr);
                }
            } else if (game_dir) {
                extra = true;
            } else {
                game_dir = argv[i];
            }
        }

        if (!extra) {
            game_dir = game_dir ? game_dir : ".";

            return strcmp(verb, "run") == 0 ? orb_debug_run(game_dir, window)
                                            : orb_debug_scry(game_dir, window);
        }
    }

    bool flag = false;

    for (int i = 2; i < argc; i++)
        if (strncmp(argv[i], "--", 2) == 0) flag = true;

    if (!flag && strcmp(verb, "cast") == 0 && argc <= 3) return orb_debug_cast(dir, false, nullptr);
    if (!flag && strcmp(verb, "seal") == 0 && argc <= 4)
        return orb_debug_cast(dir, true, argc == 4 ? argv[3] : nullptr);

    orb_log("orb: unknown verb or arguments");

    return usage(stderr);
}
#endif

#include "run.h"
#include "../cast/file.h"
#include "../os/os.h"
#include "api.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <stdlib.h>
#include <string.h>

static const orb_game* run_game;
static orb_config run_config;
static orb_info_desc run_info;
static orb_arena run_arena, run_state;
static uint32_t* run_rgb;

static bool run_load(orb_span file, orb_error* err) {
    orb_assets assets;

    if (!orb_file_load(file, &assets, err)) return false;

    orb_api_set_assets(&assets);
    run_info = *assets.info;
    return true;
}

static bool run_open(orb_error* err) {
    orb_size size = {run_info.w, run_info.h};

    orb_api_init(&run_arena, size);
    run_rgb = orb_arena_push_array(&run_arena, uint32_t, (size_t)size.w* size.h);

    orb_os_config cfg = {.title = run_info.name, .size = size};

    if (!orb_os_open(&cfg)) return orb_error_set(err, "cannot open a window");

    run_game->init(run_state.base, orb_api_table());
    run_game->reload(run_state.base, orb_api_table());
    return true;
}

#ifndef ORB_RELEASE
static orb_cast_result run_result; // from the last successful cast; valid until the next one
static const char* run_dir;
static orb_arena run_assets[2], run_scratch;
static int run_live;

// Zero the state and start over: the layout the running code expects changed.
// init sets the state up, then reload binds names, as it does after any recast.
static void run_state_reset(orb_config next) {
    memset(run_state.base, 0, run_state.size);
    run_config = next;
    run_game->init(run_state.base, orb_api_table());
    run_game->reload(run_state.base, orb_api_table());
}

static bool run_cast(int half, orb_error* err) {
    orb_arena_reset(&run_assets[half]);
    orb_arena_reset(&run_scratch);

    orb_manifest m;
    orb_cast_result result;

    if (!orb_cast_game(&run_scratch, &run_assets[half], run_dir, &m, &result, err)) return false;

    // The regions and the window were sized at boot from the first manifest.
    if (m.size.w != run_info.w || m.size.h != run_info.h || m.asset_headroom != run_assets[0].size)
        return orb_error_set(
            err, "orb.json: size or asset_headroom changed; restart orb to apply it"
        );

    if (!run_load(result.file, err)) return false;

    run_live = half;
    run_result = result;
    return true;
}

static bool run_boot_sources(const char* game_dir, orb_error* err) {
    orb_manifest m;

    if (!orb_manifest_load(&run_arena, game_dir, &m, err)) return false;

    run_dir = game_dir;
    run_info = (orb_info_desc) {.w = (uint16_t)m.size.w, .h = (uint16_t)m.size.h};
    run_assets[0] = orb_arena_carve(&run_arena, "asset half A", m.asset_headroom);
    run_assets[1] = orb_arena_carve(&run_arena, "asset half B", m.asset_headroom);
    run_scratch = orb_arena_carve(&run_arena, "cast scratch", m.asset_headroom);

    return run_cast(0, err);
}
#endif

bool orb_run_boot(
    const orb_game* game,
    [[maybe_unused]] const char* game_dir,
    orb_span sealed,
    orb_error* err
) {
    run_game = game;
    run_config = game->config();

    if (run_config.arena_size == 0) run_config.arena_size = 64 << 20;

    uint8_t* mem = malloc(run_config.arena_size);

    if (!mem)
        return orb_error_set(err, "cannot allocate %zu bytes for the arena", run_config.arena_size);

    orb_arena_init(&run_arena, "arena", mem, run_config.arena_size);
    // Twice the struct, so a field added across a code reload does not end the session.
    size_t reserve = orb_max(run_config.state_size * 2, (size_t)256 << 10);

    run_state = orb_arena_carve(&run_arena, "game state", reserve);

#ifndef ORB_RELEASE
    if (!sealed.len) return run_boot_sources(game_dir, err) && run_open(err);
#endif

    return run_load(sealed, err) && run_open(err);
}

bool orb_run_tick(void) {
    orb_input in;

    if (!orb_os_pump(&in)) return false;

    orb_input_step(&in);
    run_game->update(run_state.base, orb_api_table());
    orb_api_poll();
    return true;
}

void orb_run_draw(void) {
    run_game->draw(run_state.base, orb_api_table());
    orb_api_resolve(run_rgb);
    orb_os_present(run_rgb);
}

void orb_run_loop(void (*poll)(void)) {
    const uint64_t step = 1000000000u / ORB_TICK_RATE;
    uint64_t previous = orb_os_ticks(), accumulator = 0;

    for (;;) {
        uint64_t now = orb_os_ticks();

        accumulator += now - previous;
        previous = now;

        if (accumulator > 4 * step) {
            orb_log("dropped %llu ticks", (unsigned long long)((accumulator - 4 * step) / step));
            accumulator = 4 * step;
        }

        while (accumulator >= step) {
            if (poll) poll();
            if (!orb_run_tick()) return;

            accumulator -= step;
        }

        orb_run_draw();
        orb_os_sleep(step - accumulator);
    }
}

#ifndef ORB_RELEASE
bool orb_run_recast(orb_error* err) {
    if (!run_cast(1 - run_live, err)) return false;

    run_game->reload(run_state.base, orb_api_table());
    return true;
}

void orb_run_set_game(const orb_game* game) {
    run_game = game;

    orb_config next = game->config();

    if (next.state_size > run_state.size)
        orb_fatal(
            "the game's state struct grew past its %zu byte region; restart orb", run_state.size
        );

    if (next.state_version != run_config.state_version ||
        next.state_size != run_config.state_size) {
        orb_log(
            "state version %u -> %u, size %zu -> %zu: state reset", run_config.state_version,
            next.state_version, run_config.state_size, next.state_size
        );
        run_state_reset(next);
    } else {
        run_game->reload(run_state.base, orb_api_table());
    }
}

const orb_cast_result* orb_run_cast_result(void) {
    return &run_result;
}
#endif

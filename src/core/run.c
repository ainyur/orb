#include "run.h"
#include "../cast/orbfile.h"
#include "../os/orb_os.h"
#include "api.h"
#include "input.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const orb_game* run_game;
static orb_config run_config;
static orb_manifest run_manifest;      // from the last successful cast; valid until the next one
static orb_manifest run_boot_manifest; // what the regions and window were sized from
static const char* run_dir;
static orb_arena run_arena, run_state, run_assets[2], run_scratch;
static int run_live;
static uint32_t* run_rgb;
static uint8_t run_boot_mem[1 << 18];

// The state region keeps room to grow, so adding a field to the game state struct
// across a code reload does not end the session.
static size_t run_state_reserve(size_t state_size) {
    size_t reserve = state_size * 2;

    return reserve < (256u << 10) ? (256u << 10) : reserve;
}

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

    if (m.size_w != run_boot_manifest.size_w || m.size_h != run_boot_manifest.size_h) {
        orb_error_set(err, "orb.json: size changed; restart orb to apply it");
        return false;
    }

    if (m.asset_headroom != run_boot_manifest.asset_headroom) {
        orb_error_set(err, "orb.json: asset_headroom changed; restart orb to apply it");
        return false;
    }

    orb_assets assets;

    if (!orb_file_load(result.file, &assets, err)) return false;

    orb_api_set_assets(&assets);
    run_live = half;
    run_manifest = m;
    return true;
}

bool orb_run_boot(const orb_game* game, const char* game_dir, orb_error* err) {
    orb_arena boot;

    orb_arena_init(&boot, "boot", run_boot_mem, sizeof run_boot_mem);

    if (!orb_manifest_load(&boot, game_dir, &run_manifest, err)) return false;

    run_boot_manifest = run_manifest;
    run_dir = game_dir;
    run_game = game;
    run_config = game->config();

    if (run_config.arena_size == 0) run_config.arena_size = 64 << 20;

    uint8_t* mem = malloc(run_config.arena_size);

    if (!mem) {
        orb_error_set(err, "cannot allocate %zu bytes for the arena", run_config.arena_size);
        return false;
    }

    orb_arena_init(&run_arena, "arena", mem, run_config.arena_size);
    run_state = orb_arena_carve(&run_arena, "game state", run_state_reserve(run_config.state_size));
    run_assets[0] = orb_arena_carve(&run_arena, "asset half A", run_manifest.asset_headroom);
    run_assets[1] = orb_arena_carve(&run_arena, "asset half B", run_manifest.asset_headroom);
    run_scratch = orb_arena_carve(&run_arena, "cast scratch", run_manifest.asset_headroom);
    orb_api_init(&run_arena, run_manifest.size_w, run_manifest.size_h);
    run_rgb = orb_arena_push(
        &run_arena, sizeof(uint32_t) * run_manifest.size_w * run_manifest.size_h, 16
    );

    if (!run_cast(0, err)) return false;

    orb_os_config cfg = {
        .title = run_manifest.name, .size_w = run_manifest.size_w, .size_h = run_manifest.size_h
    };

    if (!orb_os_open(&cfg)) {
        orb_error_set(err, "cannot open a window");
        return false;
    }

    run_game->init(run_state.base, orb_api_table());
    run_game->reload(run_state.base, orb_api_table());
    return true;
}

bool orb_run_tick(void) {
    orb_input in;

    if (!orb_os_pump(&in)) return false;

    orb_input_step(&in);
    run_game->update(run_state.base, orb_api_table());
    return true;
}

void orb_run_draw(void) {
    run_game->draw(run_state.base, orb_api_table());
    orb_api_resolve(run_rgb);
    orb_os_present(run_rgb);
}

void orb_run_loop(void (*poll)(void)) {
    const uint64_t step = 1000000000u / 60;
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

    if (next.state_version != run_config.state_version) {
        orb_log(
            "state version %u -> %u: state reset", run_config.state_version, next.state_version
        );
        run_state_reset(next);
    } else if (next.state_size != run_config.state_size) {
        orb_log("state size %zu -> %zu: state reset", run_config.state_size, next.state_size);
        run_state_reset(next);
    } else {
        run_game->reload(run_state.base, orb_api_table());
    }
}

const orb_manifest* orb_run_manifest(void) {
    return &run_manifest;
}

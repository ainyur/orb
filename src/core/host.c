#include "host.h"
#include "../cast/file.h"
#include "../os/os.h"
#include "api.h"
#include "asset.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <stdlib.h>
#include <string.h>

static const orb_game* host_game;
static orb_config host_config;
static orb_info_desc host_info;
static orb_arena host_arena, host_state;
static uint32_t* host_rgb;
static uint64_t host_previous, host_accumulator;

static bool host_load(orb_span file, orb_assets* out, orb_error* err) {
    if (!orb_file_load(file, out, err)) return false;

    host_info = *out->info;
    return true;
}

#ifndef ORB_RELEASE
static orb_cast_result host_result; // from the last successful cast; valid until the next one
static const char* host_dir;
static orb_arena host_assets[2], host_scratch;
static int host_live_half;

static bool host_cast(int half, orb_assets* out, orb_error* err) {
    orb_arena_reset(&host_assets[half]);
    orb_arena_reset(&host_scratch);

    orb_manifest m;
    orb_cast_result result;

    if (!orb_cast_game(&host_scratch, &host_assets[half], host_dir, &m, &result, err)) return false;

    // The regions and the window were sized at boot from the first manifest.
    if (m.size.width != host_info.width || m.size.height != host_info.height ||
        m.asset_headroom != host_assets[0].size)
        return orb_error_set(
            err, "orb.json: size or asset_headroom changed; restart orb to apply it"
        );

    if (!host_load(result.file, out, err)) return false;

    host_live_half = half;
    host_result = result;
    return true;
}

// Size the asset halves and the scratch from the manifest, then cast into half A.
static bool host_cast_first(const char* game_dir, orb_assets* out, orb_error* err) {
    orb_manifest m;

    if (!orb_manifest_load(&host_arena, game_dir, &m, err)) return false;

    host_dir = game_dir;
    host_info =
        (orb_info_desc) {.width = (uint16_t)m.size.width, .height = (uint16_t)m.size.height};
    host_assets[0] = orb_arena_carve(&host_arena, "asset half A", m.asset_headroom);
    host_assets[1] = orb_arena_carve(&host_arena, "asset half B", m.asset_headroom);
    host_scratch = orb_arena_carve(&host_arena, "cast scratch", m.asset_headroom);

    return host_cast(0, out, err);
}

// Zero the state and start over: the layout the running code expects changed.
// init sets the state up, then reload binds names, as it does after any recast.
static void host_state_reset(orb_config next) {
    memset(host_state.base, 0, host_state.size);
    host_config = next;
    host_game->init(host_state.base, orb_api_table());
    host_game->reload(host_state.base, orb_api_table());
}
#endif

bool orb_boot(
    const orb_game* game,
    [[maybe_unused]] const char* game_dir,
    orb_span sealed,
    orb_error* err
) {
    host_game = game;
    host_config = game->config();

    if (host_config.arena_size == 0) host_config.arena_size = 64 << 20;

    uint8_t* mem = malloc(host_config.arena_size);

    if (!mem)
        return orb_error_set(
            err, "cannot allocate %zu bytes for the arena", host_config.arena_size
        );

    orb_arena_init(&host_arena, "arena", mem, host_config.arena_size);
    // Twice the struct, so a field added across a code reload does not end the session.
    size_t state_reserve = orb_max(host_config.state_size * 2, (size_t)256 << 10);

    host_state = orb_arena_carve(&host_arena, "game state", state_reserve);

    orb_assets assets;

#ifdef ORB_RELEASE
    if (!host_load(sealed, &assets, err)) return false;
#else
    if (!(sealed.len ? host_load(sealed, &assets, err) : host_cast_first(game_dir, &assets, err)))
        return false;
#endif

    orb_size size = {host_info.width, host_info.height};

    orb_api_boot(&host_arena, size, &assets);
    host_rgb = orb_arena_push_array(&host_arena, uint32_t, (size_t)size.width* size.height);

    orb_os_config cfg = {.title = host_info.name, .size = size};

    if (!orb_os_open(&cfg)) return orb_error_set(err, "cannot open a window");

    orb_input_resolve(orb_api_assets());
    host_game->init(host_state.base, orb_api_table());
    host_game->reload(host_state.base, orb_api_table());
    host_previous = orb_os_ticks();
    host_accumulator = 0;
    return true;
}

static bool host_tick(void) {
    orb_input in;

    if (!orb_os_pump(&in)) return false;

    orb_input_step(&in);
    host_game->update(host_state.base, orb_api_table());
    orb_api_poll();
    return true;
}

static void host_render(void) {
    host_game->draw(host_state.base, orb_api_table());
    orb_api_resolve(host_rgb);
    orb_os_present(host_rgb);
}

bool orb_frame(void) {
    constexpr uint64_t step = ORB_NS_PER_SECOND / ORB_TICK_RATE;
    uint64_t now = orb_os_ticks();

    host_accumulator += now - host_previous;
    host_previous = now;

    if (host_accumulator > 4 * step) {
        orb_log("dropped %llu ticks", (unsigned long long)((host_accumulator - 4 * step) / step));
        host_accumulator = 4 * step;
    }

    while (host_accumulator >= step) {
        if (!host_tick()) return false;

        host_accumulator -= step;
    }

    host_render();
    orb_os_sleep(step - host_accumulator);
    return true;
}

void orb_quit(void) {
    orb_os_close();
    orb_api_quit();
    free(host_arena.base);
    host_arena = (orb_arena) {};
}

#ifndef ORB_RELEASE
bool orb_recast(orb_error* err) {
    orb_assets assets;

    if (!host_cast(1 - host_live_half, &assets, err)) return false;

    orb_api_set_assets(&assets);
    orb_input_resolve(orb_api_assets());
    host_game->reload(host_state.base, orb_api_table());
    return true;
}

void orb_set_game(const orb_game* game) {
    host_game = game;

    orb_config next = game->config();

    if (next.state_size > host_state.size)
        orb_fatal(
            "the game's state struct grew past its %zu byte region; restart orb", host_state.size
        );

    if (next.state_version != host_config.state_version ||
        next.state_size != host_config.state_size) {
        orb_log(
            "state version %u -> %u, size %zu -> %zu: state reset", host_config.state_version,
            next.state_version, host_config.state_size, next.state_size
        );
        host_state_reset(next);
    } else {
        host_game->reload(host_state.base, orb_api_table());
    }
}

const orb_cast_result* orb_last_cast(void) {
    return &host_result;
}
#endif

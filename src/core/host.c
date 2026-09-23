#include "host.h"
#include "../cast/file.h"
#include "../os/os.h"
#include "api.h"
#include "asset.h"
#include "console.h"
#include "entity.h"
#include "input.h"
#include "log.h"
#include "macros.h"
#include "world.h"

#include <stdlib.h>
#include <string.h>

static const orb_game* host_game;
static orb_config host_config;
static orb_info_desc host_info;
static orb_arena host_arena, host_state, host_pool;
static uint32_t* host_rgb;
static uint64_t host_previous, host_accumulator;
static bool host_quitting;

// The config with its defaults filled, so a reload compares like against like.
static orb_config host_normalize(orb_config c) {
    if (c.max_entities == 0) c.max_entities = 256;

    return c;
}

static size_t host_round16(size_t n) {
    return (n + 15) & ~(size_t)15;
}

// Each carve and push starts on a 16-byte boundary.
size_t orb_host_release_size(const orb_config* c, orb_size size) {
    size_t pixels = (size_t)size.width * size.height;

    return host_round16(c->state_size) + host_round16(orb_entity_region_size(c)) +
           host_round16(pixels) + pixels * 4;
}

static bool host_load(orb_span file, orb_assets* out, orb_error* err) {
    if (!orb_file_load(file, out, err)) return false;

    host_info = *out->info;
    return true;
}

static void host_reload(void) {
    orb_console_clear();
    orb_entity_types_clear();
    host_game->reload(host_state.base, orb_api_table());
}

#ifndef ORB_RELEASE
static orb_cast_result host_result; // from the last successful cast; valid until the next one
static const char* host_dir;
static orb_arena host_assets[2], host_scratch;
static int host_live_half;
static orb_clock host_clock = {.timescale = 1};
static int host_frame_ticks;
static uint32_t host_frame_us;

static const struct {
    orb_arena* arena;
    const char* name;
} host_regions[] = {
    {&host_arena, "arena"},
    {&host_state, "game state"},
    {&host_pool, "entity pool"},
    {&host_assets[0], "asset half A"},
    {&host_assets[1], "asset half B"},
    {&host_scratch, "cast scratch"},
};

static bool host_reserve(orb_error* err) {
    for (size_t i = 0; i < sizeof host_regions / sizeof host_regions[0]; i++)
        if (!orb_arena_reserve(host_regions[i].arena, host_regions[i].name, ORB_REGION_RESERVE))
            return orb_error_set(err, "cannot reserve address space for %s", host_regions[i].name);

    return true;
}

static bool host_cast(int half, orb_assets* out, orb_error* err) {
    orb_arena_reset(&host_assets[half]);
    orb_arena_reset(&host_scratch);

    orb_manifest m;
    orb_cast_result result;

    if (!orb_cast_game(&host_scratch, &host_assets[half], host_dir, &m, &result, err)) return false;

    // The window and the framebuffers were sized at boot from the first manifest.
    if (m.size.width != host_info.width || m.size.height != host_info.height)
        return orb_error_set(err, "orb.json: size changed; restart orb to apply it");

    if (!host_load(result.file, out, err)) return false;

    host_live_half = half;
    host_result = result;
    return true;
}

// Read the screen size from the manifest, then cast into half A.
static bool host_cast_first(const char* game_dir, orb_assets* out, orb_error* err) {
    orb_manifest m;

    if (!orb_manifest_load(&host_arena, game_dir, &m, err)) return false;

    host_dir = game_dir;
    host_info =
        (orb_info_desc) {.width = (uint16_t)m.size.width, .height = (uint16_t)m.size.height};
    return host_cast(0, out, err);
}

// Zero the state and start over: the layout the running code expects changed.
// init sets the state up, then reload binds names, as it does after any recast.
static void host_state_reset(orb_config next) {
    orb_arena_reset(&host_state);
    orb_arena_push(&host_state, next.state_size, 16);

    if (!orb_entity_reset(&next)) orb_fatal("the game's entity pool does not fit its region");

    host_config = next;
    host_game->init(host_state.base, orb_api_table());
    host_reload();
}
#endif

bool orb_boot(
    const orb_game* game,
    [[maybe_unused]] const char* game_dir,
    orb_span sealed,
    orb_error* err
) {
    host_game = game;
    host_config = host_normalize(game->config());

    orb_assets assets;

#ifdef ORB_RELEASE
    if (!host_load(sealed, &assets, err)) return false;

    orb_size size = {host_info.width, host_info.height};
    size_t total = orb_host_release_size(&host_config, size);
    uint8_t* mem = malloc(total);

    if (!mem)
        return orb_error_set(err, "cannot allocate %s for the arena", orb_bytes_format(total).text);

    orb_arena_init(&host_arena, "arena", mem, total);
    host_state = orb_arena_carve(&host_arena, "game state", host_config.state_size);
    host_pool = orb_arena_carve(&host_arena, "entity pool", orb_entity_region_size(&host_config));
#else
    if (!host_reserve(err)) return false;
    if (!(sealed.len ? host_load(sealed, &assets, err) : host_cast_first(game_dir, &assets, err)))
        return false;

    orb_size size = {host_info.width, host_info.height};
#endif

    orb_arena_push(&host_state, host_config.state_size, 16);

    orb_api_boot(&host_arena, size, &assets);
    orb_entity_boot(&host_pool, &host_config, host_state.base, orb_api_table(), orb_api_assets());
    orb_console_boot(host_state.base, orb_api_table());
    host_rgb = orb_arena_push_array(&host_arena, uint32_t, (size_t)size.width* size.height);

    orb_os_config cfg = {.title = host_info.name, .size = size};

    if (!orb_os_open(&cfg)) return orb_error_set(err, "cannot open a window");

    orb_input_resolve(orb_api_assets());
    host_game->init(host_state.base, orb_api_table());
    host_reload();
    host_previous = orb_os_ticks();
    host_accumulator = 0;
    host_quitting = false;
#ifndef ORB_RELEASE
    host_clock = (orb_clock) {.timescale = 1};
#endif
    return true;
}

// The console sees every snapshot; the game sees only the ones a tick hands on.
static bool host_poll(orb_input* in) {
    if (!orb_os_pump(in)) return false;

    orb_console_step(in);
    return true;
}

static bool host_tick(void) {
    orb_input in;

    if (!host_poll(&in)) return false;

    orb_input_step(&in);
    host_game->update(host_state.base, orb_api_table());
    orb_api_poll();
    return true;
}

static void host_render(void) {
    host_game->draw(host_state.base, orb_api_table());
    orb_api_resolve(host_rgb);
    orb_world_debug_draw(
        host_rgb, (orb_size) {host_info.width, host_info.height}, orb_api_camera()
    );
    orb_console_draw(host_rgb, (orb_size) {host_info.width, host_info.height});
    orb_os_present(host_rgb);
}

bool orb_frame(void) {
    constexpr uint64_t step = ORB_NS_PER_SECOND / ORB_TICK_RATE;
    uint64_t now = orb_os_ticks(), elapsed = now - host_previous;
    int ticks = 0;

    host_previous = now;
#ifndef ORB_RELEASE
    double scaled = (double)elapsed * orb_max(0.0f, host_clock.timescale);

    elapsed = (uint64_t)orb_min(scaled, (double)(4 * step));
#endif
    host_accumulator += elapsed;

    if (host_accumulator > 4 * step) {
        uint64_t dropped = (host_accumulator - 4 * step) / step;

        if (dropped) orb_log("dropped %llu ticks", (unsigned long long)dropped);

        host_accumulator = 4 * step;
    }

#ifndef ORB_RELEASE
    if (host_clock.paused) {
        host_accumulator = 0;

        if (host_clock.step) {
            host_clock.step = false;

            if (!host_tick()) return false;

            ticks = 1;
        }
    } else
        host_clock.step = false; // a step outside a pause has nothing to run
#endif

    while (host_accumulator >= step) {
        if (!host_tick()) return false;

        host_accumulator -= step;
        ticks++;
    }

    // A frame without a tick still polls, so the console and the window close work while paused.
    if (ticks == 0) {
        orb_input in;

        if (!host_poll(&in)) return false;
    }

    host_render();
#ifndef ORB_RELEASE
    host_frame_ticks = ticks;
    host_frame_us = (uint32_t)((orb_os_ticks() - now) / 1000);
#endif
    orb_os_sleep(step - host_accumulator);
    return !host_quitting;
}

void orb_quit(void) {
    orb_os_close();
    orb_api_quit();
#ifdef ORB_RELEASE
    free(host_arena.base);
    host_arena = (orb_arena) {};
#else
    for (size_t i = 0; i < sizeof host_regions / sizeof host_regions[0]; i++)
        orb_arena_release(host_regions[i].arena);
#endif
}

void orb_quit_request(void) {
    host_quitting = true;
}

#ifndef ORB_RELEASE
bool orb_recast(orb_error* err) {
    orb_assets assets;
    orb_assets previous = *orb_api_assets();

    if (!host_cast(1 - host_live_half, &assets, err)) return false;

    orb_api_set_assets(&assets);
    orb_entity_revalidate(&previous);
    orb_world_revalidate();
    orb_input_resolve(orb_api_assets());
    host_reload();
    return true;
}

void orb_set_game(const orb_game* game) {
    host_game = game;

    orb_config next = host_normalize(game->config());
    bool pool_changed =
        next.max_entities != host_config.max_entities ||
        memcmp(next.components, host_config.components, sizeof next.components) != 0;

    if (next.state_version != host_config.state_version ||
        next.state_size != host_config.state_size || pool_changed) {
        orb_log(
            "state version %u -> %u, size %s -> %s%s: state reset", host_config.state_version,
            next.state_version, orb_bytes_format(host_config.state_size).text,
            orb_bytes_format(next.state_size).text, pool_changed ? ", entity pool changed" : ""
        );
        host_state_reset(next);
    } else
        host_reload();
}

const orb_cast_result* orb_last_cast(void) {
    return &host_result;
}

orb_clock* orb_clock_get(void) {
    return &host_clock;
}

orb_stats orb_stats_get(void) {
    return (orb_stats) {
        .state = host_state.used,
        .pool = host_pool.used,
        .assets = host_result.file.len,
        .cast_peak = host_scratch.peak,
        .release =
            orb_host_release_size(&host_config, (orb_size) {host_info.width, host_info.height}),
        .frame_ticks = host_frame_ticks,
        .frame_us = host_frame_us
    };
}
#endif

#pragma once

#include "../cast/cast.h"
#include "../orb.h"

bool orb_boot(const orb_game* game, const char* game_dir, orb_span sealed, orb_error* err);
bool orb_frame(void);
void orb_quit(void);
void orb_quit_request(void);

// The one block a release build allocates at boot for this config and screen size.
size_t orb_host_release_size(const orb_config* config, orb_size size);

#ifndef ORB_RELEASE
typedef struct orb_clock {
    float timescale;
    bool paused, step;
} orb_clock;

typedef struct orb_stats {
    size_t state, pool; // bytes in use
    size_t assets;      // the last cast's sealed file, what a release build embeds
    size_t cast_peak;
    size_t release; // orb_host_release_size for the running config
    int frame_ticks;
    uint32_t frame_us;
} orb_stats;

bool orb_recast(orb_error* err);
void orb_set_game(const orb_game* game);
const orb_cast_result* orb_last_cast(void);
orb_clock* orb_clock_get(void);
orb_stats orb_stats_get(void);
#endif

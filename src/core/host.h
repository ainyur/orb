#pragma once

#include "../cast/cast.h"
#include "../orb.h"

bool orb_boot(const orb_game* game, const char* game_dir, u8_span sealed, orb_error* err);
bool orb_frame(void);
void orb_quit(void);
void orb_quit_request(void);

// The one block a release build allocates at boot for this config and screen size.
usize orb_host_release_size(const orb_config* config, orb_size size);

#ifndef ORB_RELEASE
typedef struct orb_clock {
    f32 timescale;
    bool paused, step;
} orb_clock;

typedef struct orb_stats {
    usize state, pool; // bytes in use
    usize assets;      // the last cast's sealed file, what a release build embeds
    usize cast_peak;
    usize global_peak, frame_peak;
    usize release; // orb_host_release_size for the running config
    int frame_ticks;
    u32 frame_us;
} orb_stats;

bool orb_recast(orb_error* err);
void orb_set_game(const orb_game* game);
const orb_cast_result* orb_last_cast(void);
orb_clock* orb_clock_get(void);
orb_stats orb_stats_get(void);
#endif

#pragma once

#include "../cast/cast.h"
#include "../orb.h"

bool orb_boot(const orb_game* game, const char* game_dir, orb_span sealed, orb_error* err);
bool orb_frame(void);
void orb_quit(void);
void orb_quit_request(void);

#ifndef ORB_RELEASE
typedef struct orb_clock {
    float timescale;
    bool paused, step;
} orb_clock;

typedef struct orb_stats {
    size_t arena_used, arena_peak, cast_peak, cast_headroom;
    int frame_ticks;
    uint32_t frame_us;
} orb_stats;

bool orb_recast(orb_error* err);
void orb_set_game(const orb_game* game);
const orb_cast_result* orb_last_cast(void);
orb_clock* orb_clock_get(void);
orb_stats orb_stats_get(void);
#endif

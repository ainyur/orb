#pragma once

#include "../cast/cast.h"
#include "../orb.h"

bool orb_run_boot(const orb_game* game, const char* game_dir, orb_span sealed, orb_error* err);
bool orb_run_tick(void);
void orb_run_draw(void);
void orb_run_loop(void (*poll)(void));
bool orb_run_recast(orb_error* err);
void orb_run_set_game(const orb_game* game);
const orb_cast_result* orb_run_cast_result(void);

#pragma once

#include "../cast/cast.h"
#include "../orb.h"

bool orb_boot(const orb_game* game, const char* game_dir, orb_span sealed, orb_error* err);
bool orb_frame(void);
void orb_quit(void);

bool orb_recast(orb_error* err);
void orb_set_game(const orb_game* game);
const orb_cast_result* orb_last_cast(void);

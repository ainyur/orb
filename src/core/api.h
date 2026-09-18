#pragma once

#include "../cast/file.h"
#include "../graphics/fb.h"
#include "../orb.h"

const orb_api* orb_api_table(void);
const orb_assets* orb_api_assets(void);
const orb_fb* orb_api_fb(void);

void orb_api_boot(orb_arena* a, orb_size size, const orb_assets* assets);
void orb_api_poll(void); // once a tick: reports dropped audio commands
void orb_api_resolve(uint32_t* rgb);
void orb_api_set_assets(const orb_assets* assets); // also reloads the palette
void orb_api_quit(void);

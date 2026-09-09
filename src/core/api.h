#pragma once

#include "../cast/file.h"
#include "../graphics/framebuffer.h"
#include "../orb.h"

const orb_framebuffer* orb_api_framebuffer(void);
void orb_api_init(orb_arena* a, orb_size size);
void orb_api_poll(void); // once a tick: reports dropped audio commands
void orb_api_resolve(uint32_t* rgb);
void orb_api_set_assets(const orb_assets* assets); // also reloads the palette
const orb_api* orb_api_table(void);

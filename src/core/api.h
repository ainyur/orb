#pragma once

#include "../cast/file.h"
#include "../graphics/fb.h"
#include "../orb.h"

const orb_api* orb_api_table(void);
orb_arena* orb_api_global(void);
orb_arena* orb_api_frame(void);
const orb_assets* orb_api_assets(void);
const orb_fb* orb_api_fb(void);
const u32* orb_api_pal_base(void);
orb_vec2f orb_api_camera(void);

void orb_api_boot(orb_arena* out, orb_size size, const orb_assets* assets);
void orb_api_poll(void); // once a tick: reports dropped audio commands
void orb_api_resolve(u32* rgb);
void orb_api_set_assets(const orb_assets* assets); // also reloads the palette
void orb_api_quit(void);

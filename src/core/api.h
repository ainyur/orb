#pragma once
#include "../cast/file.h"
#include "../graphics/framebuffer.h"
#include "../orb.h"

// The runtime's one framebuffer, palette, and asset set, and the orb_api table
// that binds the graphics modules to them. This file holds no logic of its own.
const orb_framebuffer* orb_api_framebuffer(void);
void orb_api_init(orb_arena* a, int w, int h);
void orb_api_resolve(uint32_t* rgb);
void orb_api_set_assets(const orb_assets* assets); // also reloads the palette
const orb_api* orb_api_table(void);

#pragma once

#include "../core/asset.h"
#include "framebuffer.h"

void orb_text_draw(
    orb_framebuffer* fb,
    const orb_assets* assets,
    orb_font f,
    const char* s,
    orb_vec2 at,
    const uint8_t* remap
);
orb_size orb_text_measure(const orb_assets* assets, orb_font f, const char* s);

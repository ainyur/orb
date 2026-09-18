#pragma once

#include "../core/asset.h"
#include "fb.h"

orb_size orb_text_measure(const orb_assets* assets, orb_font f, const char* s);
void orb_text_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_font f,
    const char* s,
    orb_vec2 at,
    const uint8_t* remap
);

#pragma once

#include "../core/asset.h"
#include "fb.h"

orb_size orb_text_measure(const orb_assets* assets, orb_font font, const char* text);
void orb_text_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_font font,
    const char* text,
    orb_vec2 at,
    const uint8_t* remap
);

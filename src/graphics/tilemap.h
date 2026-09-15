#pragma once

#include "../core/asset.h"
#include "../orb.h"
#include "framebuffer.h"

int orb_tilemap_cell(const orb_assets* assets, orb_level level, int layer, orb_vec2 at);
void orb_tilemap_draw(
    orb_framebuffer* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_level level,
    int layer
);

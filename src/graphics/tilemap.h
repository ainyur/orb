#pragma once

#include "../core/asset.h"
#include "../orb.h"
#include "fb.h"

int orb_tilemap_cell(const orb_assets* assets, orb_level level, int layer, orb_vec2 at);
uint32_t orb_tilemap_level_at(
    const orb_assets* assets,
    orb_vec2 at
); // the first level containing the pixel, or ORB_NO_INDEX
int orb_tilemap_layer_find(const orb_assets* assets, orb_level level, const char* name);
orb_layer_info orb_tilemap_layer_info(const orb_assets* assets, orb_level level, int layer);
orb_rect orb_tilemap_level_bounds(const orb_assets* assets, orb_level level);
int orb_tilemap_level_neighbors(
    const orb_assets* assets,
    orb_level level,
    orb_level_neighbor* out,
    int max
);
void orb_tilemap_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_level level,
    int layer
);

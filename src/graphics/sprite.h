#pragma once

#include "../core/asset.h"
#include "../orb.h"
#include "fb.h"

void orb_anim_start(orb_anim_state* state, orb_anim anim);
orb_sprite orb_anim_step(const orb_assets* assets, orb_anim_state* state);
orb_sprite orb_anim_frame(
    const orb_assets* assets,
    const orb_anim_state* state
); // the current frame, unstepped
void orb_sprite_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_sprite sprite,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
);
uint64_t orb_sprite_id(const char* stem, int frame); // orb_asset_id with the frame as the suffix

#pragma once

#include "../cast/file.h"
#include "../orb.h"
#include "framebuffer.h"

void orb_animation_start(orb_animation_state* st, orb_animation a);
orb_sprite orb_animation_step(const orb_assets* assets, orb_animation_state* st);
void orb_sprite_draw(
    orb_framebuffer* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_sprite s,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
);

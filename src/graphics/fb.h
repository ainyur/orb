#pragma once

#include "../core/arena.h"
#include "../core/macros.h"
#include "../orb.h"

typedef struct orb_fb {
    int width, height;
    uint8_t* px;
} orb_fb;

// Clamps to fit an int before flooring, since a camera offset can be arbitrarily large.
static inline int orb_floor(float v) {
    constexpr float bound = (float)(1 << 30);

    v = orb_clamp(v, -bound, bound);

    int whole = (int)v; // toward zero

    return whole - (v < (float)whole);
}

void orb_fb_init(orb_fb* fb, orb_arena* a, orb_size size);
void orb_fb_clear(orb_fb* fb, uint8_t index);
void orb_fb_blit(
    orb_fb* fb,
    const uint8_t* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
);
void orb_fb_resolve(const orb_fb* fb, const uint32_t* live, uint32_t* rgb);

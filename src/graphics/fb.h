#pragma once

#include "../core/arena.h"
#include "../core/macros.h"
#include "../orb.h"

typedef struct orb_fb {
    int width, height;
    uint8_t* px;
} orb_fb;

// Clamps to fit an int before flooring, since a camera offset can be arbitrarily large.
static inline int orb_floor(float value) {
    constexpr float bound = (float)(1 << 30);

    value = orb_clamp(value, -bound, bound);

    int whole = (int)value; // toward zero

    return whole - (value < (float)whole);
}

void orb_fb_init(orb_fb* fb, orb_arena* arena, orb_size size);
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

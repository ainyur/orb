#pragma once

#include "../core/arena.h"
#include "../core/macros.h"
#include "../orb.h"

#include <math.h>

typedef struct orb_framebuffer {
    int width, height;
    uint8_t* px;
} orb_framebuffer;

// Clamps to fit an int before flooring, since a camera offset can be arbitrarily large.
static inline int orb_floor(float v) {
    constexpr float bound = (float)(1 << 30);

    v = orb_clamp(v, -bound, bound);
    return (int)floorf(v);
}

void orb_framebuffer_blit(
    orb_framebuffer* fb,
    const uint8_t* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
);
void orb_framebuffer_clear(orb_framebuffer* fb, uint8_t index);
void orb_framebuffer_init(orb_framebuffer* fb, orb_arena* a, orb_size size);
void orb_framebuffer_resolve(const orb_framebuffer* fb, const uint32_t* live, uint32_t* rgb);

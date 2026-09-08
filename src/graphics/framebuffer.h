#pragma once

#include "../core/arena.h"
#include "../orb.h"

#include <stdbool.h>

typedef struct orb_framebuffer {
    int w, h;
    uint8_t* px;
} orb_framebuffer;

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

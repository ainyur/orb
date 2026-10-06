#pragma once

#include "../core/arena.h"
#include "../orb.h"
#include "../orb_math.h"

typedef struct orb_fb {
    int width, height;
    u8* px;
} orb_fb;

void orb_fb_init(orb_fb* fb, arena* out, orb_size size);
void orb_fb_clear(orb_fb* fb, u8 index);
void orb_fb_blit(
    orb_fb* fb,
    const u8* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    u32 flags,
    const u8* remap
);
void orb_fb_resolve(const orb_fb* fb, const u32* live, u32* rgb);

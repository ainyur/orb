#include "framebuffer.h"
#include "../core/macros.h"

#include <string.h>

void orb_framebuffer_blit(
    orb_framebuffer* fb,
    const uint8_t* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
) {
    int x0 = orb_max(0, -at.x), x1 = orb_min(size.w, fb->w - at.x);
    int y0 = orb_max(0, -at.y), y1 = orb_min(size.h, fb->h - at.y);

    for (int y = y0; y < y1; y++) {
        const uint8_t* row = src + (flags & ORB_FLIP_Y ? size.h - 1 - y : y) * stride;
        uint8_t* dst = fb->px + (at.y + y) * fb->w + at.x;

        for (int x = x0; x < x1; x++) {
            uint8_t index = row[flags & ORB_FLIP_X ? size.w - 1 - x : x];

            if (index == 0) continue;

            dst[x] = remap ? remap[index] : index;
        }
    }
}

void orb_framebuffer_clear(orb_framebuffer* fb, uint8_t index) {
    memset(fb->px, index, (size_t)fb->w * fb->h);
}

void orb_framebuffer_init(orb_framebuffer* fb, orb_arena* a, orb_size size) {
    fb->w = size.w;
    fb->h = size.h;
    fb->px = orb_arena_push(a, (size_t)size.w * size.h, 16);
}

void orb_framebuffer_resolve(const orb_framebuffer* fb, const uint32_t* live, uint32_t* rgb) {
    size_t n = (size_t)fb->w * fb->h;

    for (size_t i = 0; i < n; i++)
        rgb[i] = live[fb->px[i]];
}

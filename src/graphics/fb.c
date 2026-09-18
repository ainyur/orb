#include "fb.h"
#include "../core/macros.h"

#include <string.h>

void orb_fb_init(orb_fb* fb, orb_arena* a, orb_size size) {
    fb->width = size.width;
    fb->height = size.height;
    fb->px = orb_arena_push(a, (size_t)size.width * size.height, 16);
}

void orb_fb_clear(orb_fb* fb, uint8_t index) {
    memset(fb->px, index, (size_t)fb->width * fb->height);
}

void orb_fb_blit(
    orb_fb* fb,
    const uint8_t* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
) {
    int x0 = orb_max(0, -at.x), x1 = orb_min(size.width, fb->width - at.x);
    int y0 = orb_max(0, -at.y), y1 = orb_min(size.height, fb->height - at.y);

    for (int y = y0; y < y1; y++) {
        const uint8_t* row = src + (flags & ORB_FLIP_Y ? size.height - 1 - y : y) * stride;
        uint8_t* dst = fb->px + (at.y + y) * fb->width + at.x;

        for (int x = x0; x < x1; x++) {
            uint8_t index = row[flags & ORB_FLIP_X ? size.width - 1 - x : x];

            if (index == 0) continue;

            dst[x] = remap ? remap[index] : index;
        }
    }
}

void orb_fb_resolve(const orb_fb* fb, const uint32_t* live, uint32_t* rgb) {
    size_t n = (size_t)fb->width * fb->height;

    for (size_t i = 0; i < n; i++)
        rgb[i] = live[fb->px[i]];
}

#include "fb.h"
#include "../orb_math.h"

#include <string.h>

void orb_fb_init(orb_fb* fb, orb_arena* out, orb_size size) {
    fb->width = size.width;
    fb->height = size.height;
    fb->px = orb_arena_push(out, (usize)size.width * size.height, 16);
}

void orb_fb_clear(orb_fb* fb, u8 index) {
    memset(fb->px, index, (usize)fb->width * fb->height);
}

void orb_fb_blit(
    orb_fb* fb,
    const u8* src,
    int stride,
    orb_size size,
    orb_vec2 at,
    u32 flags,
    const u8* remap
) {
    int x0 = orb_max(0, -at.x), x1 = orb_min(size.width, fb->width - at.x);
    int y0 = orb_max(0, -at.y), y1 = orb_min(size.height, fb->height - at.y);

    for (int y = y0; y < y1; y++) {
        const u8* row = src + (flags & ORB_FLIP_Y ? size.height - 1 - y : y) * stride;
        u8* dst = fb->px + (at.y + y) * fb->width + at.x;

        for (int x = x0; x < x1; x++) {
            u8 index = row[flags & ORB_FLIP_X ? size.width - 1 - x : x];

            if (index == 0) continue;

            dst[x] = remap ? remap[index] : index;
        }
    }
}

void orb_fb_resolve(const orb_fb* fb, const u32* live, u32* rgb) {
    usize n = (usize)fb->width * fb->height;

    for (usize i = 0; i < n; i++)
        rgb[i] = live[fb->px[i]];
}

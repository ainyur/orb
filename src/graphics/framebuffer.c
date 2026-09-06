#include "framebuffer.h"
#include <string.h>

void orb_framebuffer_blit(
    orb_framebuffer* fb, const uint8_t* src, int stride, int w, int h, int dx, int dy, bool flip_x,
    bool flip_y, const uint8_t* remap
) {
    for (int y = 0; y < h; y++) {
        int fy = dy + y;

        if (fy < 0 || fy >= fb->h) continue;

        int sy = flip_y ? h - 1 - y : y;

        for (int x = 0; x < w; x++) {
            int fx = dx + x;

            if (fx < 0 || fx >= fb->w) continue;

            int sx = flip_x ? w - 1 - x : x;
            uint8_t index = src[sy * stride + sx];

            if (index == 0) continue;

            if (remap) index = remap[index];

            fb->px[fy * fb->w + fx] = index;
        }
    }
}

void orb_framebuffer_clear(orb_framebuffer* fb, uint8_t index) {
    memset(fb->px, index, (size_t)fb->w * fb->h);
}

void orb_framebuffer_init(orb_framebuffer* fb, orb_arena* a, int w, int h) {
    fb->w = w;
    fb->h = h;
    fb->px = orb_arena_push(a, (size_t)w * h, 16);
}

void orb_framebuffer_resolve(const orb_framebuffer* fb, const uint32_t* live, uint32_t* rgb) {
    size_t n = (size_t)fb->w * fb->h;

    for (size_t i = 0; i < n; i++)
        rgb[i] = live[fb->px[i]];
}

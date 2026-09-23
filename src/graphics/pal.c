#include "pal.h"

#include <string.h>

void orb_pal_load(orb_pal* pal, const uint8_t* rgba) {
    for (int i = 0; i < 256; i++) {
        pal->base[i] =
            (uint32_t)rgba[i * 4] << 16 | (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
    }

    memcpy(pal->live, pal->base, sizeof pal->live);
}

void orb_pal_reset(orb_pal* pal) {
    memcpy(pal->live, pal->base, sizeof pal->live);
}

uint32_t orb_pal_get(const orb_pal* pal, int index) {
    return pal->live[index & 255];
}

void orb_pal_set(orb_pal* pal, int index, uint8_t r, uint8_t g, uint8_t b) {
    pal->live[index & 255] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

void orb_pal_extremes(const uint32_t* base, uint32_t* dark, uint32_t* bright) {
    int low = 1 << 30, high = -1;

    for (int i = 0; i < 256; i++) {
        uint32_t color = base[i];
        int lum = 299 * (int)(color >> 16 & 0xff) + 587 * (int)(color >> 8 & 0xff) +
                  114 * (int)(color & 0xff);

        if (lum < low) low = lum, *dark = color;
        if (lum > high) high = lum, *bright = color;
    }
}

#include "pal.h"

#include <string.h>

void orb_pal_load(orb_pal* pal, const u8* rgba) {
    for (int i = 0; i < 256; i++) {
        pal->base[i] = (u32)rgba[i * 4] << 16 | (u32)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
    }

    memcpy(pal->live, pal->base, sizeof pal->live);
}

void orb_pal_reset(orb_pal* pal) {
    memcpy(pal->live, pal->base, sizeof pal->live);
}

u32 orb_pal_get(const orb_pal* pal, int index) {
    return pal->live[index & 255];
}

void orb_pal_set(orb_pal* pal, int index, u8 r, u8 g, u8 b) {
    pal->live[index & 255] = (u32)r << 16 | (u32)g << 8 | b;
}

void orb_pal_extremes(const u32* base, u32* dark, u32* bright) {
    int low = 1 << 30, high = -1;

    for (int i = 0; i < 256; i++) {
        u32 color = base[i];
        int lum = 299 * (int)(color >> 16 & 0xff) + 587 * (int)(color >> 8 & 0xff) +
                  114 * (int)(color & 0xff);

        if (lum < low) low = lum, *dark = color;
        if (lum > high) high = lum, *bright = color;
    }
}

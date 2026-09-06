#include "palette.h"
#include <string.h>

uint32_t orb_palette_get(const orb_palette* p, int i) {
    return p->live[i & 255];
}

void orb_palette_load(orb_palette* p, const uint8_t* rgba) {
    for (int i = 0; i < 256; i++) {
        p->base[i] = (uint32_t)rgba[i * 4] << 16 | (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
    }

    memcpy(p->live, p->base, sizeof p->live);
}

void orb_palette_reset(orb_palette* p) {
    memcpy(p->live, p->base, sizeof p->live);
}

void orb_palette_set(orb_palette* p, int i, uint8_t r, uint8_t g, uint8_t b) {
    p->live[i & 255] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

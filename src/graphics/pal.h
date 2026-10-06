#pragma once

#include <stdint.h>

typedef struct orb_pal {
    u32 base[256], live[256];
} orb_pal;

void orb_pal_load(orb_pal* pal,
                  const u8* rgba); // 256 * 4 bytes; sets base and live
void orb_pal_reset(orb_pal* pal);  // live = base
u32 orb_pal_get(const orb_pal* pal, int index);
void orb_pal_set(orb_pal* pal, int index, u8 r, u8 g, u8 b);
// The darkest and brightest of 256 entries by luminance.
void orb_pal_extremes(const u32* base, u32* dark, u32* bright);

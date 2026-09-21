#pragma once

#include <stdint.h>

typedef struct orb_pal {
    uint32_t base[256], live[256];
} orb_pal;

void orb_pal_load(orb_pal* p,
                  const uint8_t* rgba); // 256 * 4 bytes; sets base and live
void orb_pal_reset(orb_pal* p);         // live = base
uint32_t orb_pal_get(const orb_pal* p, int i);
void orb_pal_set(orb_pal* p, int i, uint8_t r, uint8_t g, uint8_t b);
// The darkest and brightest of 256 entries by luminance.
void orb_pal_extremes(const uint32_t* base, uint32_t* dark, uint32_t* bright);

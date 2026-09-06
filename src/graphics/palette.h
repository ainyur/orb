#pragma once
#include <stdint.h>

typedef struct orb_palette {
    uint32_t base[256], live[256];
} orb_palette;

uint32_t orb_palette_get(const orb_palette* p, int i);
void orb_palette_load(orb_palette* p, const uint8_t* rgba); // 256 * 4 bytes; sets base and live
void orb_palette_reset(orb_palette* p);                     // live = base
void orb_palette_set(orb_palette* p, int i, uint8_t r, uint8_t g, uint8_t b);

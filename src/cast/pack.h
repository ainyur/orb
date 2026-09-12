#pragma once

#include "../core/arena.h"
#include "../orb.h"

typedef struct orb_pack_rect {
    uint16_t x, y, w, h;
} orb_pack_rect;

typedef struct orb_pack_frame {
    uint32_t rect;
    int16_t ox, oy;
} orb_pack_frame;

typedef struct orb_pack {
    uint16_t sheet_w, sheet_h;
    uint8_t* pixels;
    orb_pack_rect* rects;
    uint32_t rect_count;
    orb_pack_frame* frames; // one per frame passed in
} orb_pack;

void orb_pack_frames(
    orb_arena* a,
    const uint8_t* frames,
    uint32_t frame_count,
    orb_size frame,
    orb_pack* out
);

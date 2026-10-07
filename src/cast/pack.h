#pragma once

#include "../core/arena.h"
#include "../orb.h"

typedef struct orb_pack_frame {
    u32 rect;
    i16 ox, oy;
} orb_pack_frame;

typedef struct orb_pack_rect {
    u16 x, y, width, height;
} orb_pack_rect;

orb_slice(orb_pack_rect);

typedef struct orb_pack {
    u16 sheet_width, sheet_height;
    u8* pixels;
    orb_pack_rect_slice rects;
    orb_pack_frame* frames;
} orb_pack;

bool orb_pack_frames(
    orb_arena* scratch,
    const u8* frames,
    u32 frame_count,
    orb_size frame,
    orb_pack* out
);

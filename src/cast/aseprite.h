#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

typedef struct orb_ase_tag {
    const char* name;
    u16 from, to;
    u8 direction; // 0 forward, 1 reverse, 2 ping-pong, 3 ping-pong reverse
} orb_ase_tag;

slice(orb_ase_tag);

typedef struct orb_ase {
    u16 width, height, frame_count, color_count;
    u8 transparent;
    i16 grid_x, grid_y;
    u16 grid_width, grid_height;
    u8 rgb[256][3];
    u8* frames;     // frame_count * width * height composited indices
    u16* durations; // milliseconds
    orb_ase_tag_slice tags;
} orb_ase;

[[nodiscard]] bool orb_ase_parse(arena* scratch, u8_span file, orb_ase* out, orb_error* err);

#pragma once
#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"
#include <stdbool.h>

typedef struct orb_ase_tag {
    const char* name;
    uint16_t from, to;
    uint8_t direction; // 0 forward, 1 reverse, 2 ping-pong, 3 ping-pong reverse
} orb_ase_tag;

typedef struct orb_ase {
    uint16_t w, h, frame_count, tag_count, color_count;
    uint8_t transparent;
    uint8_t rgb[256][3];
    uint8_t* frames;     // frame_count * w * h composited indices
    uint16_t* durations; // milliseconds
    orb_ase_tag* tags;
} orb_ase;

bool orb_ase_parse(orb_arena* a, orb_span file, orb_ase* out, orb_error* err);

#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

#include <stdbool.h>

typedef struct orb_wav {
    const int16_t* pcm;            // interleaved, widened to int16 whatever the file held
    uint32_t count;                // frames
    uint32_t rate;                 // Hz
    uint32_t loop_start, loop_end; // frames, end exclusive and clamped to count; see has_loop
    uint8_t channels;              // 1 or 2
    bool has_loop;                 // the file carried a smpl chunk with a loop
} orb_wav;

bool orb_wav_parse(orb_arena* a, orb_span file, orb_wav* out, orb_error* err);

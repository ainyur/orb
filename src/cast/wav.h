#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

typedef struct orb_wav {
    orb_span data;                 // the file's data chunk, valid while the file bytes are
    uint32_t count;                // frames
    uint32_t rate;                 // Hz
    uint32_t loop_start, loop_end; // frames, end exclusive and clamped to count; see has_loop
    uint8_t channels;              // 1 or 2
    uint8_t bits;                  // 8 or 16
    bool has_loop;                 // the file carried a smpl chunk with a loop
} orb_wav;

bool orb_wav_parse(orb_span file, orb_wav* out, orb_error* err);
void orb_wav_decode(const orb_wav* w, int16_t* out); // count * channels values, widened to int16

#pragma once

#include "../core/arena.h"
#include "../core/log.h"
#include "../orb.h"

typedef struct orb_wav {
    u8_span data;             // the file's data chunk, valid while the file bytes are
    u32 count;                // frames
    u32 rate;                 // Hz
    u32 loop_start, loop_end; // frames, end exclusive and clamped to count; see has_loop
    u8 channels;              // 1 or 2
    u8 bits;                  // 8 or 16
    bool has_loop;            // the file carried a smpl chunk with a loop
} orb_wav;

[[nodiscard]] bool orb_wav_parse(u8_span file, orb_wav* out, orb_error* err);
void orb_wav_decode(const orb_wav* wav, i16* out); // count * channels values, widened to int16

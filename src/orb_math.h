#pragma once

#include "orb_types.h"

typedef struct orb_size {
    int width, height;
} orb_size;

typedef struct orb_vec2 {
    int x, y;
} orb_vec2;

typedef struct orb_vec2f {
    f32 x, y;
} orb_vec2f;

typedef struct orb_rect {
    orb_vec2 at;
    orb_size size;
} orb_rect;

#define orb_min(a, b) ((a) < (b) ? (a) : (b))
#define orb_max(a, b) ((a) > (b) ? (a) : (b))
#define orb_clamp(value, low, high) orb_min(orb_max(value, low), high)

// n rounded up to a multiple of to, a power of two.
static inline usize orb_round_up(usize n, usize to) {
    return (n + to - 1) & ~(to - 1);
}

static inline int orb_floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Clamps to fit an int before flooring, since a camera offset can be arbitrarily large.
static inline int orb_floor(f32 value) {
    constexpr f32 bound = (f32)(1 << 30);

    value = orb_clamp(value, -bound, bound);

    int whole = (int)value; // toward zero

    return whole - (value < (f32)whole);
}

// The integer square root, rounded down.
static inline u32 orb_sqrt(u32 n) {
    u32 root = 0;

    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }

    return root;
}

static inline int orb_length(int x, int y) {
    return (int)orb_sqrt((u32)(x * x) + (u32)(y * y));
}

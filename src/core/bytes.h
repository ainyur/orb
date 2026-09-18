#pragma once

#include <stdint.h>

static inline uint8_t orb_bytes_u8(const uint8_t* ptr) {
    return ptr[0];
}

static inline uint16_t orb_bytes_u16(const uint8_t* ptr) {
    return (uint16_t)(ptr[0] | ptr[1] << 8);
}

static inline uint32_t orb_bytes_u32(const uint8_t* ptr) {
    return (uint32_t)ptr[0] | (uint32_t)ptr[1] << 8 | (uint32_t)ptr[2] << 16 |
           (uint32_t)ptr[3] << 24;
}

static inline uint64_t orb_bytes_u64(const uint8_t* ptr) {
    return orb_bytes_u32(ptr) | (uint64_t)orb_bytes_u32(ptr + 4) << 32;
}

// Writes cp as UTF-8 without a terminator and returns the byte count, 1 to 4.
static inline int orb_bytes_utf8(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }

    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }

    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static inline int8_t orb_bytes_i8(const uint8_t* ptr) {
    return (int8_t)ptr[0];
}

static inline int16_t orb_bytes_i16(const uint8_t* ptr) {
    return (int16_t)orb_bytes_u16(ptr);
}

static inline int32_t orb_bytes_i32(const uint8_t* ptr) {
    return (int32_t)orb_bytes_u32(ptr);
}

static inline int64_t orb_bytes_i64(const uint8_t* ptr) {
    return (int64_t)orb_bytes_u64(ptr);
}

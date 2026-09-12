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

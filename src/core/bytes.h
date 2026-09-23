#pragma once

#include "../orb.h"

#include <stdint.h>
#include <string.h>

// Bytes [at, at + len) of span, or an empty span when they do not fit.
static inline orb_span orb_span_sub(orb_span span, size_t at, size_t len) {
    if (at > span.len || len > span.len - at) return (orb_span) {};

    return (orb_span) {span.ptr + at, len};
}

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

// Writes codepoint as UTF-8 without a terminator and returns the byte count, 1 to 4.
static inline int orb_bytes_utf8(uint32_t codepoint, char* out) {
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }

    if (codepoint < 0x800) {
        out[0] = (char)(0xc0 | codepoint >> 6);
        out[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }

    if (codepoint < 0x10000) {
        out[0] = (char)(0xe0 | codepoint >> 12);
        out[1] = (char)(0x80 | (codepoint >> 6 & 0x3f));
        out[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }

    out[0] = (char)(0xf0 | codepoint >> 18);
    out[1] = (char)(0x80 | (codepoint >> 12 & 0x3f));
    out[2] = (char)(0x80 | (codepoint >> 6 & 0x3f));
    out[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4;
}

// The codepoint at text and its byte length; 0 for a malformed, overlong, or cut-off sequence.
static inline int orb_bytes_utf8_decode(const char* text, uint32_t* out) {
    const unsigned char* bytes = (const unsigned char*)text;
    int n = bytes[0] < 0x80             ? 1
            : (bytes[0] & 0xe0) == 0xc0 ? 2
            : (bytes[0] & 0xf0) == 0xe0 ? 3
            : (bytes[0] & 0xf8) == 0xf0 ? 4
                                        : 0;

    if (n == 0 || bytes[0] == 0) return 0;

    uint32_t codepoint = n == 1 ? bytes[0] : bytes[0] & (0x7f >> n);

    for (int i = 1; i < n; i++) {
        if ((bytes[i] & 0xc0) != 0x80) return 0;

        codepoint = codepoint << 6 | (bytes[i] & 0x3f);
    }

    if (codepoint < (uint32_t[]) {0, 0, 0x80, 0x800, 0x10000}[n]) return 0; // overlong
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;

    *out = codepoint;
    return n;
}

// Appends codepoint to a NUL-terminated buffer of cap bytes as UTF-8; a control character or one
// that does not fit is dropped.
static inline void orb_bytes_utf8_push(char* text, int* n, int cap, uint32_t codepoint) {
    char utf8[4];
    int len = orb_bytes_utf8(codepoint, utf8);

    if (codepoint < 0x20 || codepoint == 0x7f || *n + len >= cap) return;

    memcpy(text + *n, utf8, (size_t)len);
    *n += len;
    text[*n] = 0;
}

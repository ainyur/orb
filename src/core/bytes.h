#pragma once

#include "../orb.h"

#include <stdint.h>
#include <string.h>

orb_span(u8);

// Bytes [at, at + len) of bytes, or an empty span when they do not fit. It reads untrusted file
// bytes, so it never traps.
static inline u8_span orb_bytes_sub(u8_span bytes, usize at, usize len) {
    if (at > bytes.len || len > bytes.len - at) return (u8_span) {};

    return (u8_span) {bytes.elems + at, (u32)len};
}

static inline u8 orb_bytes_u8(const u8* ptr) {
    return ptr[0];
}

static inline u16 orb_bytes_u16(const u8* ptr) {
    return (u16)(ptr[0] | ptr[1] << 8);
}

static inline u32 orb_bytes_u32(const u8* ptr) {
    return (u32)ptr[0] | (u32)ptr[1] << 8 | (u32)ptr[2] << 16 | (u32)ptr[3] << 24;
}

static inline u64 orb_bytes_u64(const u8* ptr) {
    return orb_bytes_u32(ptr) | (u64)orb_bytes_u32(ptr + 4) << 32;
}

static inline i8 orb_bytes_i8(const u8* ptr) {
    return (i8)ptr[0];
}

static inline i16 orb_bytes_i16(const u8* ptr) {
    return (i16)orb_bytes_u16(ptr);
}

static inline i32 orb_bytes_i32(const u8* ptr) {
    return (i32)orb_bytes_u32(ptr);
}

static inline i64 orb_bytes_i64(const u8* ptr) {
    return (i64)orb_bytes_u64(ptr);
}

// Writes codepoint as UTF-8 without a terminator and returns the byte count, 1 to 4.
static inline int orb_bytes_utf8(u32 codepoint, char* out) {
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
static inline int orb_bytes_utf8_decode(const char* text, u32* out) {
    const unsigned char* bytes = (const unsigned char*)text;
    int n = bytes[0] < 0x80             ? 1
            : (bytes[0] & 0xe0) == 0xc0 ? 2
            : (bytes[0] & 0xf0) == 0xe0 ? 3
            : (bytes[0] & 0xf8) == 0xf0 ? 4
                                        : 0;

    if (n == 0 || bytes[0] == 0) return 0;

    u32 codepoint = n == 1 ? bytes[0] : bytes[0] & (0x7f >> n);

    for (int i = 1; i < n; i++) {
        if ((bytes[i] & 0xc0) != 0x80) return 0;

        codepoint = codepoint << 6 | (bytes[i] & 0x3f);
    }

    if (codepoint < (u32[]) {0, 0, 0x80, 0x800, 0x10000}[n]) return 0; // overlong
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;

    *out = codepoint;
    return n;
}

// Appends codepoint to a NUL-terminated buffer of cap bytes as UTF-8; a control character or one
// that does not fit is dropped.
static inline void orb_bytes_utf8_push(char* text, int* n, int cap, u32 codepoint) {
    char utf8[4];
    int len = orb_bytes_utf8(codepoint, utf8);

    if (codepoint < 0x20 || codepoint == 0x7f || *n + len >= cap) return;

    memcpy(text + *n, utf8, (usize)len);
    *n += len;
    text[*n] = 0;
}

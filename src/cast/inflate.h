#pragma once
#include <stddef.h>
#include <stdint.h>

// Decode a zlib stream into out. Returns bytes written, or -1 if the input is
// malformed or the output does not fit. The Adler-32 trailer is not checked.
ptrdiff_t orb_inflate(const uint8_t* in, size_t inlen, uint8_t* out, size_t outlen);

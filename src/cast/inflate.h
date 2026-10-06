#pragma once

#include <stddef.h>
#include <stdint.h>

// Decode a zlib stream into out. Returns bytes written, or -1 if the input is
// malformed or the output does not fit. The Adler-32 trailer is not checked.
[[nodiscard]] isize orb_inflate(const u8* in, usize inlen, u8* out, usize outlen);

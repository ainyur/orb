#include "wav.h"

#include <stdio.h>
#include <string.h>

static uint16_t wav_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t wav_u32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The fmt chunk: format tag, channels, rate, and bit depth, with an extensible
// tag resolved to its subformat, whose GUID starts with the plain format tag.
static bool wav_format(orb_span chunk, orb_wav* out, orb_error* err) {
    if (chunk.len < 16) return orb_error_set(err, "wav: fmt chunk too short");

    uint16_t format = wav_u16(chunk.ptr);
    uint16_t channels = wav_u16(chunk.ptr + 2), bits = wav_u16(chunk.ptr + 14);

    out->rate = wav_u32(chunk.ptr + 4);

    if (format == 0xFFFE) {
        if (chunk.len < 40) return orb_error_set(err, "wav: extensible fmt chunk too short");

        format = wav_u16(chunk.ptr + 24);
    }

    if (format == 3)
        return orb_error_set(err, "wav: float samples are not accepted, use 16-bit PCM");
    if (format != 1) return orb_error_set(err, "wav: format %u is not PCM", format);
    if (bits != 8 && bits != 16)
        return orb_error_set(err, "wav: %u-bit samples, use 8 or 16", bits);
    if (channels != 1 && channels != 2)
        return orb_error_set(err, "wav: %u channels, at most 2", channels);
    if (out->rate < 1 || out->rate > 192000)
        return orb_error_set(err, "wav: rate %u, must be 1 to 192000", out->rate);

    out->channels = (uint8_t)channels;
    out->bits = (uint8_t)bits;
    return true;
}

void orb_wav_decode(const orb_wav* w, int16_t* out) {
    size_t total = (size_t)w->count * w->channels;

    for (size_t i = 0; i < total; i++)
        out[i] = w->bits == 8 ? (int16_t)((w->data.ptr[i] - 128) * 256)
                              : (int16_t)wav_u16(w->data.ptr + i * 2);
}

bool orb_wav_parse(orb_span file, orb_wav* out, orb_error* err) {
    memset(out, 0, sizeof *out);

    if (file.len < 12 || memcmp(file.ptr, "RIFF", 4) != 0 || memcmp(file.ptr + 8, "WAVE", 4) != 0)
        return orb_error_set(err, "wav: not a RIFF WAVE file");

    orb_span data = {};
    bool have_format = false;

    for (size_t at = 12; at + 8 <= file.len;) {
        const uint8_t* head = file.ptr + at;
        uint32_t size = wav_u32(head + 4);
        orb_span chunk = {head + 8, size};

        if (size > file.len - at - 8)
            return orb_error_set(
                err, "wav: chunk \"%.4s\" runs past the end of the file", (const char*)head
            );

        if (memcmp(head, "fmt ", 4) == 0) {
            if (!wav_format(chunk, out, err)) return false;

            have_format = true;
        } else if (memcmp(head, "data", 4) == 0) {
            data = chunk;
        } else if (memcmp(head, "smpl", 4) == 0 && size >= 60 && wav_u32(chunk.ptr + 28) >= 1) {
            // 36 bytes of header, then loops of 24: cue, type, start, end, fraction, count
            out->has_loop = true;
            out->loop_start = wav_u32(chunk.ptr + 44);
            out->loop_end = wav_u32(chunk.ptr + 48) + 1; // the chunk's end is inclusive
        }

        at += 8 + size + (size & 1);
    }

    if (!have_format) return orb_error_set(err, "wav: no fmt chunk");
    if (!data.ptr) return orb_error_set(err, "wav: no data chunk");

    out->data = data;
    out->count = (uint32_t)(data.len / ((size_t)out->channels * out->bits / 8));

    if (out->has_loop && out->loop_end > out->count) out->loop_end = out->count;

    return true;
}

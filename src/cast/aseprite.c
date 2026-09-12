#include "aseprite.h"
#include "../core/bytes.h"
#include "inflate.h"

#include <stdio.h>
#include <string.h>

constexpr int ASE_MAX_LAYERS = 64;

typedef struct {
    const uint8_t* zdata;
    uint32_t zlen;
    int16_t x, y;
    uint16_t w, h;
    bool present;
} ase_cel;

typedef struct {
    uint16_t flags, type, blend;
    uint8_t opacity;
} ase_layer;

static bool ase_fail(orb_error* err, const char* msg) {
    return orb_error_set(err, "aseprite: %s", msg);
}

bool orb_ase_parse(orb_arena* a, orb_span file, orb_ase* out, orb_error* err) {
    const uint8_t* p = file.ptr;
    const uint8_t* end = file.ptr + file.len;

    if (file.len < 128 || orb_bytes_u16(p + 4) != 0xA5E0)
        return ase_fail(err, "not an aseprite file");
    if (orb_bytes_u16(p + 12) != 8) return ase_fail(err, "not in indexed color mode");

    memset(out, 0, sizeof *out);

    out->frame_count = orb_bytes_u16(p + 6);
    out->w = orb_bytes_u16(p + 8);
    out->h = orb_bytes_u16(p + 10);
    out->transparent = orb_bytes_u8(p + 28);
    out->color_count = orb_bytes_u16(p + 32);

    if (out->color_count == 0) out->color_count = 256;

    ase_layer layers[ASE_MAX_LAYERS];
    int layer_count = 0;
    ase_cel* cels = orb_arena_push_array(a, ase_cel, out->frame_count * ASE_MAX_LAYERS);

    out->durations = orb_arena_push_array(a, uint16_t, out->frame_count);

    p += 128;

    for (int f = 0; f < out->frame_count; f++) {
        if (p + 16 > end) return ase_fail(err, "truncated frame");

        const uint8_t* frame_end = p + orb_bytes_u32(p);

        if (orb_bytes_u16(p + 4) != 0xF1FA || frame_end > end) return ase_fail(err, "bad frame");

        uint32_t chunk_count = orb_bytes_u16(p + 6);

        if (chunk_count == 0xFFFF) chunk_count = orb_bytes_u32(p + 12);

        out->durations[f] = orb_bytes_u16(p + 8);
        p += 16;

        for (uint32_t c = 0; c < chunk_count; c++) {
            if (p + 6 > frame_end) return ase_fail(err, "truncated chunk");

            uint32_t size = orb_bytes_u32(p);
            uint16_t type = orb_bytes_u16(p + 4);
            const uint8_t* d = p + 6;
            const uint8_t* chunk_end = p + size;

            if (size < 6 || chunk_end > frame_end) return ase_fail(err, "bad chunk size");

            if (type == 0x0004) {
                int packets = orb_bytes_u16(d);
                const uint8_t* q = d + 2;
                int index = 0;

                for (int k = 0; k < packets; k++) {
                    index += q[0];
                    int n = q[1] ? q[1] : 256;
                    q += 2;

                    for (int i = 0; i < n && index < 256; i++, index++, q += 3) {
                        out->rgb[index][0] = q[0];
                        out->rgb[index][1] = q[1];
                        out->rgb[index][2] = q[2];
                    }
                }
            } else if (type == 0x2019) { // new palette
                uint32_t first = orb_bytes_u32(d + 4), last = orb_bytes_u32(d + 8);
                const uint8_t* q = d + 20;

                for (uint32_t i = first; i <= last && i < 256; i++) {
                    uint16_t flags = orb_bytes_u16(q);

                    out->rgb[i][0] = q[2];
                    out->rgb[i][1] = q[3];
                    out->rgb[i][2] = q[4];
                    q += 6;

                    if (flags & 1) q += 2 + orb_bytes_u16(q);
                }
            } else if (type == 0x2004) { // layer
                if (layer_count >= ASE_MAX_LAYERS) return ase_fail(err, "too many layers");

                ase_layer* layer = &layers[layer_count++];

                layer->flags = orb_bytes_u16(d);
                layer->type = orb_bytes_u16(d + 2);
                layer->blend = orb_bytes_u16(d + 10);
                layer->opacity = orb_bytes_u8(d + 12);
            } else if (type == 0x2005) { // cel
                uint16_t layer = orb_bytes_u16(d);
                uint16_t cel_type = orb_bytes_u16(d + 7);

                if (layer >= ASE_MAX_LAYERS) return ase_fail(err, "cel layer out of range");
                if (cel_type != 2) return ase_fail(err, "only compressed image cels are supported");

                ase_cel* cel = &cels[f * ASE_MAX_LAYERS + layer];

                cel->x = orb_bytes_i16(d + 2);
                cel->y = orb_bytes_i16(d + 4);
                cel->w = orb_bytes_u16(d + 16);
                cel->h = orb_bytes_u16(d + 18);
                cel->zdata = d + 20;
                cel->zlen = (uint32_t)(chunk_end - cel->zdata);
                cel->present = true;
            } else if (type == 0x2018) { // tags
                out->tag_count = orb_bytes_u16(d);
                out->tags = orb_arena_push_array(a, orb_ase_tag, out->tag_count);

                const uint8_t* q = d + 10;

                for (int t = 0; t < out->tag_count; t++) {
                    orb_ase_tag* tag = &out->tags[t];

                    tag->from = orb_bytes_u16(q);
                    tag->to = orb_bytes_u16(q + 2);
                    tag->direction = orb_bytes_u8(q + 4);

                    uint16_t name_len = orb_bytes_u16(q + 17);
                    char* name = orb_arena_push(a, name_len + 1, 1);

                    memcpy(name, q + 19, name_len);

                    tag->name = name;
                    q += 19 + name_len;
                }
            }

            p = chunk_end;
        }

        p = frame_end;
    }

    size_t frame_size = (size_t)out->w * out->h;

    out->frames = orb_arena_push(a, frame_size * out->frame_count, 1);
    memset(out->frames, out->transparent, frame_size * out->frame_count);

    for (int f = 0; f < out->frame_count; f++) {
        uint8_t* frame = out->frames + f * frame_size;

        for (int l = 0; l < layer_count; l++) {
            const ase_layer* layer = &layers[l];
            const ase_cel* cel = &cels[f * ASE_MAX_LAYERS + l];

            if (!cel->present || !(layer->flags & 1) || layer->type != 0) continue;

            if (layer->blend != 0 || layer->opacity != 255) {
                return ase_fail(
                    err, "layer uses a blend mode or opacity; only normal at 255 is supported"
                );
            }

            size_t cel_size = (size_t)cel->w * cel->h;
            uint8_t* pixels = orb_arena_push(a, cel_size, 1);

            if (orb_inflate(cel->zdata, cel->zlen, pixels, cel_size) != (ptrdiff_t)cel_size) {
                return ase_fail(err, "cel decompression failed");
            }

            for (int y = 0; y < cel->h; y++) {
                int fy = cel->y + y;

                if (fy < 0 || fy >= out->h) continue;

                for (int x = 0; x < cel->w; x++) {
                    int fx = cel->x + x;

                    if (fx < 0 || fx >= out->w) continue;

                    uint8_t index = pixels[y * cel->w + x];

                    if (index != out->transparent) frame[fy * out->w + fx] = index;
                }
            }
        }
    }

    return true;
}

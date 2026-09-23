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
    uint16_t width, height;
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

    if (file.len < 128 || orb_bytes_u16(p + 4) != 0xA5E0)
        return ase_fail(err, "not an aseprite file");
    if (orb_bytes_u16(p + 12) != 8) return ase_fail(err, "not in indexed color mode");

    memset(out, 0, sizeof *out);

    out->frame_count = orb_bytes_u16(p + 6);
    out->width = orb_bytes_u16(p + 8);
    out->height = orb_bytes_u16(p + 10);
    out->transparent = orb_bytes_u8(p + 28);
    out->color_count = orb_bytes_u16(p + 32);
    out->grid_x = orb_bytes_i16(p + 36);
    out->grid_y = orb_bytes_i16(p + 38);
    out->grid_width = orb_bytes_u16(p + 40);
    out->grid_height = orb_bytes_u16(p + 42);

    if (out->color_count == 0) out->color_count = 256;
    if (out->color_count > 256) return ase_fail(err, "more than 256 colors");

    ase_layer layers[ASE_MAX_LAYERS];
    int layer_count = 0;
    ase_cel* cels = orb_arena_push_array(a, ase_cel, out->frame_count * ASE_MAX_LAYERS);

    out->durations = orb_arena_push_array(a, uint16_t, out->frame_count);

    size_t at = 128;

    for (int f = 0; f < out->frame_count; f++) {
        orb_span head = orb_span_sub(file, at, 16);

        if (head.len < 16) return ase_fail(err, "truncated frame");

        orb_span frame = orb_span_sub(file, at, orb_bytes_u32(head.ptr));

        if (orb_bytes_u16(head.ptr + 4) != 0xF1FA || frame.len < 16)
            return ase_fail(err, "bad frame");

        uint32_t chunk_count = orb_bytes_u16(head.ptr + 6);

        if (chunk_count == 0xFFFF) chunk_count = orb_bytes_u32(head.ptr + 12);

        out->durations[f] = orb_bytes_u16(head.ptr + 8);

        size_t chunk_at = 16;

        for (uint32_t c = 0; c < chunk_count; c++) {
            orb_span chunk_head = orb_span_sub(frame, chunk_at, 6);

            if (chunk_head.len < 6) return ase_fail(err, "truncated chunk");

            uint32_t size = orb_bytes_u32(chunk_head.ptr);
            uint16_t type = orb_bytes_u16(chunk_head.ptr + 4);
            orb_span chunk = orb_span_sub(frame, chunk_at, size);

            if (size < 6 || chunk.len < size) return ase_fail(err, "bad chunk size");

            orb_span d = orb_span_sub(chunk, 6, chunk.len - 6);

            if (type == 0x0004) {
                if (d.len < 2) return ase_fail(err, "truncated chunk");

                int packets = orb_bytes_u16(d.ptr);
                orb_span q = orb_span_sub(d, 2, d.len - 2);
                int index = 0;

                for (int k = 0; k < packets; k++) {
                    if (q.len < 2) return ase_fail(err, "truncated chunk");

                    index += q.ptr[0];
                    int n = q.ptr[1] ? q.ptr[1] : 256;
                    q = orb_span_sub(q, 2, q.len - 2);

                    for (int i = 0; i < n && index < 256; i++, index++) {
                        if (q.len < 3) return ase_fail(err, "truncated chunk");

                        out->rgb[index][0] = q.ptr[0];
                        out->rgb[index][1] = q.ptr[1];
                        out->rgb[index][2] = q.ptr[2];
                        q = orb_span_sub(q, 3, q.len - 3);
                    }
                }
            } else if (type == 0x2019) { // new palette
                if (d.len < 20) return ase_fail(err, "truncated chunk");

                uint32_t first = orb_bytes_u32(d.ptr + 4), last = orb_bytes_u32(d.ptr + 8);
                orb_span q = orb_span_sub(d, 20, d.len - 20);

                for (uint32_t i = first; i <= last && i < 256; i++) {
                    if (q.len < 6) return ase_fail(err, "truncated chunk");

                    uint16_t flags = orb_bytes_u16(q.ptr);

                    out->rgb[i][0] = q.ptr[2];
                    out->rgb[i][1] = q.ptr[3];
                    out->rgb[i][2] = q.ptr[4];
                    q = orb_span_sub(q, 6, q.len - 6);

                    if (flags & 1) {
                        if (q.len < 2) return ase_fail(err, "truncated chunk");

                        size_t skip = 2 + orb_bytes_u16(q.ptr);

                        if (q.len < skip) return ase_fail(err, "truncated chunk");

                        q = orb_span_sub(q, skip, q.len - skip);
                    }
                }
            } else if (type == 0x2004) { // layer
                if (d.len < 16) return ase_fail(err, "truncated chunk");
                if (layer_count >= ASE_MAX_LAYERS) return ase_fail(err, "too many layers");

                ase_layer* layer = &layers[layer_count++];

                layer->flags = orb_bytes_u16(d.ptr);
                layer->type = orb_bytes_u16(d.ptr + 2);
                layer->blend = orb_bytes_u16(d.ptr + 10);
                layer->opacity = orb_bytes_u8(d.ptr + 12);
            } else if (type == 0x2005) { // cel
                if (d.len < 20) return ase_fail(err, "truncated chunk");

                uint16_t layer = orb_bytes_u16(d.ptr);
                uint16_t cel_type = orb_bytes_u16(d.ptr + 7);

                if (layer >= ASE_MAX_LAYERS) return ase_fail(err, "cel layer out of range");
                if (cel_type != 2) return ase_fail(err, "only compressed image cels are supported");

                ase_cel* cel = &cels[f * ASE_MAX_LAYERS + layer];

                cel->x = orb_bytes_i16(d.ptr + 2);
                cel->y = orb_bytes_i16(d.ptr + 4);
                cel->width = orb_bytes_u16(d.ptr + 16);
                cel->height = orb_bytes_u16(d.ptr + 18);
                cel->zdata = d.ptr + 20;
                cel->zlen = (uint32_t)(d.len - 20);
                cel->present = true;
            } else if (type == 0x2018) { // tags
                if (d.len < 10) return ase_fail(err, "truncated chunk");

                out->tag_count = orb_bytes_u16(d.ptr);
                out->tags = orb_arena_push_array(a, orb_ase_tag, out->tag_count);

                orb_span q = orb_span_sub(d, 10, d.len - 10);

                for (int t = 0; t < out->tag_count; t++) {
                    if (q.len < 19) return ase_fail(err, "truncated chunk");

                    orb_ase_tag* tag = &out->tags[t];

                    tag->from = orb_bytes_u16(q.ptr);
                    tag->to = orb_bytes_u16(q.ptr + 2);
                    tag->direction = orb_bytes_u8(q.ptr + 4);

                    if (tag->from > tag->to || tag->to >= out->frame_count)
                        return ase_fail(err, "tag frame range out of bounds");

                    uint16_t name_len = orb_bytes_u16(q.ptr + 17);
                    orb_span name_span = orb_span_sub(q, 19, name_len);

                    if (name_span.len < name_len) return ase_fail(err, "truncated chunk");

                    char* name = orb_arena_push(a, (size_t)name_len + 1, 1);

                    memcpy(name, name_span.ptr, name_len);

                    tag->name = name;
                    q = orb_span_sub(q, 19 + name_len, q.len - (19 + name_len));
                }
            }

            chunk_at += size;
        }

        at += frame.len;
    }

    size_t frame_size = (size_t)out->width * out->height;

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

            size_t cel_size = (size_t)cel->width * cel->height;
            uint8_t* pixels = orb_arena_push(a, cel_size, 1);

            if (orb_inflate(cel->zdata, cel->zlen, pixels, cel_size) != (ptrdiff_t)cel_size) {
                return ase_fail(err, "cel decompression failed");
            }

            for (int y = 0; y < cel->height; y++) {
                int fy = cel->y + y;

                if (fy < 0 || fy >= out->height) continue;

                for (int x = 0; x < cel->width; x++) {
                    int fx = cel->x + x;

                    if (fx < 0 || fx >= out->width) continue;

                    uint8_t index = pixels[y * cel->width + x];

                    if (index != out->transparent) frame[fy * out->width + fx] = index;
                }
            }
        }
    }

    return true;
}

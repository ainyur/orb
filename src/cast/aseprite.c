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

bool orb_ase_parse(orb_arena* arena, orb_span file, orb_ase* out, orb_error* err) {
    const uint8_t* header = file.ptr;

    if (file.len < 128 || orb_bytes_u16(header + 4) != 0xA5E0)
        return ase_fail(err, "not an aseprite file");
    if (orb_bytes_u16(header + 12) != 8) return ase_fail(err, "not in indexed color mode");

    memset(out, 0, sizeof *out);

    out->frame_count = orb_bytes_u16(header + 6);
    out->width = orb_bytes_u16(header + 8);
    out->height = orb_bytes_u16(header + 10);
    out->transparent = orb_bytes_u8(header + 28);
    out->color_count = orb_bytes_u16(header + 32);
    out->grid_x = orb_bytes_i16(header + 36);
    out->grid_y = orb_bytes_i16(header + 38);
    out->grid_width = orb_bytes_u16(header + 40);
    out->grid_height = orb_bytes_u16(header + 42);

    if (out->color_count == 0) out->color_count = 256;
    if (out->color_count > 256) return ase_fail(err, "more than 256 colors");

    ase_layer layers[ASE_MAX_LAYERS];
    int layer_count = 0;
    ase_cel* cels = orb_arena_push_array(arena, ase_cel, out->frame_count * ASE_MAX_LAYERS);

    out->durations = orb_arena_push_array(arena, uint16_t, out->frame_count);

    size_t at = 128;

    for (int frame_index = 0; frame_index < out->frame_count; frame_index++) {
        orb_span head = orb_span_sub(file, at, 16);

        if (head.len < 16) return ase_fail(err, "truncated frame");

        orb_span frame = orb_span_sub(file, at, orb_bytes_u32(head.ptr));

        if (orb_bytes_u16(head.ptr + 4) != 0xF1FA || frame.len < 16)
            return ase_fail(err, "bad frame");

        uint32_t chunk_count = orb_bytes_u16(head.ptr + 6);

        if (chunk_count == 0xFFFF) chunk_count = orb_bytes_u32(head.ptr + 12);

        out->durations[frame_index] = orb_bytes_u16(head.ptr + 8);

        size_t chunk_at = 16;

        for (uint32_t chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
            orb_span chunk_head = orb_span_sub(frame, chunk_at, 6);

            if (chunk_head.len < 6) return ase_fail(err, "truncated chunk");

            uint32_t size = orb_bytes_u32(chunk_head.ptr);
            uint16_t type = orb_bytes_u16(chunk_head.ptr + 4);
            orb_span chunk = orb_span_sub(frame, chunk_at, size);

            if (size < 6 || chunk.len < size) return ase_fail(err, "bad chunk size");

            orb_span data = orb_span_sub(chunk, 6, chunk.len - 6);

            if (type == 0x0004) {
                if (data.len < 2) return ase_fail(err, "truncated chunk");

                int packets = orb_bytes_u16(data.ptr);
                orb_span cursor = orb_span_sub(data, 2, data.len - 2);
                int index = 0;

                for (int k = 0; k < packets; k++) {
                    if (cursor.len < 2) return ase_fail(err, "truncated chunk");

                    index += cursor.ptr[0];
                    int n = cursor.ptr[1] ? cursor.ptr[1] : 256;
                    cursor = orb_span_sub(cursor, 2, cursor.len - 2);

                    for (int i = 0; i < n && index < 256; i++, index++) {
                        if (cursor.len < 3) return ase_fail(err, "truncated chunk");

                        out->rgb[index][0] = cursor.ptr[0];
                        out->rgb[index][1] = cursor.ptr[1];
                        out->rgb[index][2] = cursor.ptr[2];
                        cursor = orb_span_sub(cursor, 3, cursor.len - 3);
                    }
                }
            } else if (type == 0x2019) { // new palette
                if (data.len < 20) return ase_fail(err, "truncated chunk");

                uint32_t first = orb_bytes_u32(data.ptr + 4), last = orb_bytes_u32(data.ptr + 8);
                orb_span cursor = orb_span_sub(data, 20, data.len - 20);

                for (uint32_t i = first; i <= last && i < 256; i++) {
                    if (cursor.len < 6) return ase_fail(err, "truncated chunk");

                    uint16_t flags = orb_bytes_u16(cursor.ptr);

                    out->rgb[i][0] = cursor.ptr[2];
                    out->rgb[i][1] = cursor.ptr[3];
                    out->rgb[i][2] = cursor.ptr[4];
                    cursor = orb_span_sub(cursor, 6, cursor.len - 6);

                    if (flags & 1) {
                        if (cursor.len < 2) return ase_fail(err, "truncated chunk");

                        size_t skip = 2 + orb_bytes_u16(cursor.ptr);

                        if (cursor.len < skip) return ase_fail(err, "truncated chunk");

                        cursor = orb_span_sub(cursor, skip, cursor.len - skip);
                    }
                }
            } else if (type == 0x2004) { // layer
                if (data.len < 16) return ase_fail(err, "truncated chunk");
                if (layer_count >= ASE_MAX_LAYERS) return ase_fail(err, "too many layers");

                ase_layer* layer = &layers[layer_count++];

                layer->flags = orb_bytes_u16(data.ptr);
                layer->type = orb_bytes_u16(data.ptr + 2);
                layer->blend = orb_bytes_u16(data.ptr + 10);
                layer->opacity = orb_bytes_u8(data.ptr + 12);
            } else if (type == 0x2005) { // cel
                if (data.len < 20) return ase_fail(err, "truncated chunk");

                uint16_t layer = orb_bytes_u16(data.ptr);
                uint16_t cel_type = orb_bytes_u16(data.ptr + 7);

                if (layer >= ASE_MAX_LAYERS) return ase_fail(err, "cel layer out of range");
                if (cel_type != 2) return ase_fail(err, "only compressed image cels are supported");

                ase_cel* cel = &cels[frame_index * ASE_MAX_LAYERS + layer];

                cel->x = orb_bytes_i16(data.ptr + 2);
                cel->y = orb_bytes_i16(data.ptr + 4);
                cel->width = orb_bytes_u16(data.ptr + 16);
                cel->height = orb_bytes_u16(data.ptr + 18);
                cel->zdata = data.ptr + 20;
                cel->zlen = (uint32_t)(data.len - 20);
                cel->present = true;
            } else if (type == 0x2018) { // tags
                if (data.len < 10) return ase_fail(err, "truncated chunk");

                out->tag_count = orb_bytes_u16(data.ptr);
                out->tags = orb_arena_push_array(arena, orb_ase_tag, out->tag_count);

                orb_span cursor = orb_span_sub(data, 10, data.len - 10);

                for (int tag_index = 0; tag_index < out->tag_count; tag_index++) {
                    if (cursor.len < 19) return ase_fail(err, "truncated chunk");

                    orb_ase_tag* tag = &out->tags[tag_index];

                    tag->from = orb_bytes_u16(cursor.ptr);
                    tag->to = orb_bytes_u16(cursor.ptr + 2);
                    tag->direction = orb_bytes_u8(cursor.ptr + 4);

                    if (tag->from > tag->to || tag->to >= out->frame_count)
                        return ase_fail(err, "tag frame range out of bounds");

                    uint16_t name_len = orb_bytes_u16(cursor.ptr + 17);
                    orb_span name_span = orb_span_sub(cursor, 19, name_len);

                    if (name_span.len < name_len) return ase_fail(err, "truncated chunk");

                    char* name = orb_arena_push(arena, (size_t)name_len + 1, 1);

                    memcpy(name, name_span.ptr, name_len);

                    tag->name = name;
                    cursor = orb_span_sub(cursor, 19 + name_len, cursor.len - (19 + name_len));
                }
            }

            chunk_at += size;
        }

        at += frame.len;
    }

    size_t frame_size = (size_t)out->width * out->height;

    out->frames = orb_arena_push(arena, frame_size * out->frame_count, 1);
    memset(out->frames, out->transparent, frame_size * out->frame_count);

    for (int frame_index = 0; frame_index < out->frame_count; frame_index++) {
        uint8_t* frame = out->frames + frame_index * frame_size;

        for (int layer_index = 0; layer_index < layer_count; layer_index++) {
            const ase_layer* layer = &layers[layer_index];
            const ase_cel* cel = &cels[frame_index * ASE_MAX_LAYERS + layer_index];

            if (!cel->present || !(layer->flags & 1) || layer->type != 0) continue;

            if (layer->blend != 0 || layer->opacity != 255) {
                return ase_fail(
                    err, "layer uses a blend mode or opacity; only normal at 255 is supported"
                );
            }

            size_t cel_size = (size_t)cel->width * cel->height;
            uint8_t* pixels = orb_arena_push(arena, cel_size, 1);

            if (orb_inflate(cel->zdata, cel->zlen, pixels, cel_size) != (ptrdiff_t)cel_size) {
                return ase_fail(err, "cel decompression failed");
            }

            for (int y = 0; y < cel->height; y++) {
                int frame_y = cel->y + y;

                if (frame_y < 0 || frame_y >= out->height) continue;

                for (int x = 0; x < cel->width; x++) {
                    int frame_x = cel->x + x;

                    if (frame_x < 0 || frame_x >= out->width) continue;

                    uint8_t index = pixels[y * cel->width + x];

                    if (index != out->transparent) frame[frame_y * out->width + frame_x] = index;
                }
            }
        }
    }

    return true;
}

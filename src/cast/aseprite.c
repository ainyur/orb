#include "aseprite.h"
#include "../core/bytes.h"
#include "inflate.h"

#include <stdio.h>
#include <string.h>

constexpr int ASE_MAX_LAYERS = 64;
constexpr u32 ASE_MAX_TILESETS = 64;

typedef struct {
    const u8* zdata;
    u32 zlen;
    i16 x, y;
    u16 width, height; // pixels, or tiles for a tilemap
    bool present;
    bool tilemap;
    u32 id_mask, flip_mask;
} ase_cel;

typedef struct {
    u16 flags, type, blend;
    u8 opacity;
    u32 tileset; // a tilemap layer's
} ase_layer;

typedef struct {
    const u8* zdata;
    u32 zlen;
    u32 count;
    u16 width, height; // of a tile
    bool present;
} ase_tileset;

static bool ase_fail(orb_error* err, const char* msg) {
    return orb_error_set(err, "aseprite: %s", msg);
}

// A tilemap cel's pixels, its tiles' pictures from the tileset laid out row by row, as LDtk
// flattens them. LDtk draws no flipped tile, so one is refused.
static bool ase_tilemap_pixels(
    orb_arena* scratch,
    const ase_cel* cel,
    const ase_tileset* tileset,
    u8** out,
    orb_error* err
) {
    if (!tileset->present) return ase_fail(err, "a tilemap's tileset is not in the file");

    usize tile_count = (usize)cel->width * cel->height;
    usize tile_size = (usize)tileset->width * tileset->height;
    usize width = (usize)cel->width * tileset->width;
    u8* map = orb_arena_push_checked(scratch, 4, tile_count, 1);
    u8* tiles = orb_arena_push_checked(scratch, tile_size, tileset->count, 1);
    u8* pixels = orb_arena_push_checked(scratch, width, (usize)cel->height * tileset->height, 1);

    if (orb_inflate(cel->zdata, cel->zlen, map, tile_count * 4) != (isize)(tile_count * 4))
        return ase_fail(err, "tilemap decompression failed");
    if (orb_inflate(tileset->zdata, tileset->zlen, tiles, tile_size * tileset->count) !=
        (isize)(tile_size * tileset->count))
        return ase_fail(err, "tileset decompression failed");

    for (usize i = 0; i < tile_count; i++) {
        u32 value = orb_bytes_u32(map + i * 4);
        u32 id = value & cel->id_mask;
        usize left = i % cel->width * tileset->width;
        usize top = i / cel->width * tileset->height;

        if (value & cel->flip_mask)
            return ase_fail(err, "a tilemap tile is flipped, which LDtk does not draw");
        if (id >= tileset->count) return ase_fail(err, "a tilemap tile is past its tileset");

        for (usize y = 0; y < tileset->height; y++)
            memcpy(
                pixels + (top + y) * width + left, tiles + id * tile_size + y * tileset->width,
                tileset->width
            );
    }

    *out = pixels;
    return true;
}

bool orb_ase_parse(orb_arena* scratch, u8_span file, orb_ase* out, orb_error* err) {
    const u8* header = file.elems;

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
    ase_tileset tilesets[ASE_MAX_TILESETS] = {};
    int layer_count = 0;
    ase_cel* cels = orb_arena_push_array(scratch, ase_cel, out->frame_count * ASE_MAX_LAYERS);

    out->durations = orb_arena_push_array(scratch, u16, out->frame_count);

    usize at = 128;

    for (int frame_index = 0; frame_index < out->frame_count; frame_index++) {
        u8_span head = orb_bytes_sub(file, at, 16);

        if (head.len < 16) return ase_fail(err, "truncated frame");

        u8_span frame = orb_bytes_sub(file, at, orb_bytes_u32(head.elems));

        if (orb_bytes_u16(head.elems + 4) != 0xF1FA || frame.len < 16)
            return ase_fail(err, "bad frame");

        u32 chunk_count = orb_bytes_u16(head.elems + 6);

        if (chunk_count == 0xFFFF) chunk_count = orb_bytes_u32(head.elems + 12);

        out->durations[frame_index] = orb_bytes_u16(head.elems + 8);

        usize chunk_at = 16;

        for (u32 chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
            u8_span chunk_head = orb_bytes_sub(frame, chunk_at, 6);

            if (chunk_head.len < 6) return ase_fail(err, "truncated chunk");

            u32 size = orb_bytes_u32(chunk_head.elems);
            u16 type = orb_bytes_u16(chunk_head.elems + 4);
            u8_span chunk = orb_bytes_sub(frame, chunk_at, size);

            if (size < 6 || chunk.len < size) return ase_fail(err, "bad chunk size");

            u8_span data = orb_bytes_sub(chunk, 6, chunk.len - 6);

            if (type == 0x0004) {
                if (data.len < 2) return ase_fail(err, "truncated chunk");

                int packets = orb_bytes_u16(data.elems);
                u8_span cursor = orb_bytes_sub(data, 2, data.len - 2);
                int index = 0;

                for (int k = 0; k < packets; k++) {
                    if (cursor.len < 2) return ase_fail(err, "truncated chunk");

                    index += cursor.elems[0];
                    int n = cursor.elems[1] ? cursor.elems[1] : 256;
                    cursor = orb_bytes_sub(cursor, 2, cursor.len - 2);

                    for (int i = 0; i < n && index < 256; i++, index++) {
                        if (cursor.len < 3) return ase_fail(err, "truncated chunk");

                        out->rgb[index][0] = cursor.elems[0];
                        out->rgb[index][1] = cursor.elems[1];
                        out->rgb[index][2] = cursor.elems[2];
                        cursor = orb_bytes_sub(cursor, 3, cursor.len - 3);
                    }
                }
            } else if (type == 0x2019) { // new palette
                if (data.len < 20) return ase_fail(err, "truncated chunk");

                u32 first = orb_bytes_u32(data.elems + 4), last = orb_bytes_u32(data.elems + 8);
                u8_span cursor = orb_bytes_sub(data, 20, data.len - 20);

                for (u32 i = first; i <= last && i < 256; i++) {
                    if (cursor.len < 6) return ase_fail(err, "truncated chunk");

                    u16 flags = orb_bytes_u16(cursor.elems);

                    out->rgb[i][0] = cursor.elems[2];
                    out->rgb[i][1] = cursor.elems[3];
                    out->rgb[i][2] = cursor.elems[4];
                    cursor = orb_bytes_sub(cursor, 6, cursor.len - 6);

                    if (flags & 1) {
                        if (cursor.len < 2) return ase_fail(err, "truncated chunk");

                        usize skip = 2 + orb_bytes_u16(cursor.elems);

                        if (cursor.len < skip) return ase_fail(err, "truncated chunk");

                        cursor = orb_bytes_sub(cursor, skip, cursor.len - skip);
                    }
                }
            } else if (type == 0x2004) { // layer
                if (data.len < 16) return ase_fail(err, "truncated chunk");
                if (layer_count >= ASE_MAX_LAYERS) return ase_fail(err, "too many layers");

                ase_layer* layer = &layers[layer_count++];

                layer->flags = orb_bytes_u16(data.elems);
                layer->type = orb_bytes_u16(data.elems + 2);
                layer->blend = orb_bytes_u16(data.elems + 10);
                layer->opacity = orb_bytes_u8(data.elems + 12);

                if (layer->type == 2) { // tilemap: its tileset's index follows the name
                    if (data.len < 18) return ase_fail(err, "truncated chunk");

                    usize name_end = 18 + (usize)orb_bytes_u16(data.elems + 16);

                    if (data.len < name_end + 4) return ase_fail(err, "truncated chunk");

                    layer->tileset = orb_bytes_u32(data.elems + name_end);
                }
            } else if (type == 0x2023) { // tileset
                if (data.len < 34) return ase_fail(err, "truncated chunk");

                u32 id = orb_bytes_u32(data.elems);
                u32 flags = orb_bytes_u32(data.elems + 4);
                usize tiles_at = 34 + (usize)orb_bytes_u16(data.elems + 32);

                if (id >= ASE_MAX_TILESETS) return ase_fail(err, "too many tilesets");
                if (!(flags & 2)) return ase_fail(err, "a tileset's tiles are not in the file");
                if (flags & 1) tiles_at += 8; // the link to an external file comes first
                if (data.len < tiles_at + 4) return ase_fail(err, "truncated chunk");

                ase_tileset* tileset = &tilesets[id];

                tileset->count = orb_bytes_u32(data.elems + 8);
                tileset->width = orb_bytes_u16(data.elems + 12);
                tileset->height = orb_bytes_u16(data.elems + 14);
                tileset->zlen = orb_bytes_u32(data.elems + tiles_at);
                tileset->zdata = data.elems + tiles_at + 4;
                tileset->present = true;

                if (tileset->zlen > data.len - tiles_at - 4)
                    return ase_fail(err, "truncated chunk");
            } else if (type == 0x2005) { // cel
                if (data.len < 20) return ase_fail(err, "truncated chunk");

                u16 layer = orb_bytes_u16(data.elems);
                u16 cel_type = orb_bytes_u16(data.elems + 7);

                if (layer >= ASE_MAX_LAYERS) return ase_fail(err, "cel layer out of range");
                if (cel_type != 2 && cel_type != 3)
                    return ase_fail(err, "only compressed image and tilemap cels are supported");

                ase_cel* cel = &cels[frame_index * ASE_MAX_LAYERS + layer];
                usize pixels_at = cel_type == 3 ? 48 : 20;

                if (data.len < pixels_at) return ase_fail(err, "truncated chunk");
                if (cel_type == 3 && orb_bytes_u16(data.elems + 20) != 32)
                    return ase_fail(err, "a tilemap's tiles are not 32 bits");

                cel->x = orb_bytes_i16(data.elems + 2);
                cel->y = orb_bytes_i16(data.elems + 4);
                cel->width = orb_bytes_u16(data.elems + 16);
                cel->height = orb_bytes_u16(data.elems + 18);
                cel->zdata = data.elems + pixels_at;
                cel->zlen = (u32)(data.len - pixels_at);
                cel->present = true;
                cel->tilemap = cel_type == 3;

                if (cel->tilemap) {
                    cel->id_mask = orb_bytes_u32(data.elems + 22);
                    cel->flip_mask = orb_bytes_u32(data.elems + 26) |
                                     orb_bytes_u32(data.elems + 30) |
                                     orb_bytes_u32(data.elems + 34);
                }
            } else if (type == 0x2018) { // tags
                if (data.len < 10) return ase_fail(err, "truncated chunk");

                u32 tag_count = orb_bytes_u16(data.elems);

                out->tags = (orb_ase_tag_slice) {
                    .elems = orb_arena_push_array(scratch, orb_ase_tag, tag_count), .len = tag_count
                };

                u8_span cursor = orb_bytes_sub(data, 10, data.len - 10);

                for (u32 tag_index = 0; tag_index < tag_count; tag_index++) {
                    if (cursor.len < 19) return ase_fail(err, "truncated chunk");

                    orb_ase_tag* tag = &out->tags.elems[tag_index];

                    tag->from = orb_bytes_u16(cursor.elems);
                    tag->to = orb_bytes_u16(cursor.elems + 2);
                    tag->direction = orb_bytes_u8(cursor.elems + 4);

                    if (tag->from > tag->to || tag->to >= out->frame_count)
                        return ase_fail(err, "tag frame range out of bounds");

                    u16 name_len = orb_bytes_u16(cursor.elems + 17);
                    u8_span name_span = orb_bytes_sub(cursor, 19, name_len);

                    if (name_span.len < name_len) return ase_fail(err, "truncated chunk");

                    char* name = orb_arena_push(scratch, (usize)name_len + 1, 1);

                    memcpy(name, name_span.elems, name_len);

                    tag->name = name;
                    cursor = orb_bytes_sub(cursor, 19 + name_len, cursor.len - (19 + name_len));
                }
            }

            chunk_at += size;
        }

        at += frame.len;
    }

    usize frame_size = (usize)out->width * out->height;

    out->frames = orb_arena_push(scratch, frame_size * out->frame_count, 1);
    memset(out->frames, out->transparent, frame_size * out->frame_count);

    for (int frame_index = 0; frame_index < out->frame_count; frame_index++) {
        u8* frame = out->frames + frame_index * frame_size;

        for (int layer_index = 0; layer_index < layer_count; layer_index++) {
            const ase_layer* layer = &layers[layer_index];
            const ase_cel* cel = &cels[frame_index * ASE_MAX_LAYERS + layer_index];

            if (!cel->present || !(layer->flags & 1) || (layer->type != 0 && layer->type != 2))
                continue;

            if (layer->blend != 0 || layer->opacity != 255) {
                return ase_fail(
                    err, "layer uses a blend mode or opacity; only normal at 255 is supported"
                );
            }

            if (cel->tilemap != (layer->type == 2))
                return ase_fail(err, "a tilemap cel on a layer of another kind");

            usize cel_width = cel->width, cel_height = cel->height;
            u8* pixels = nullptr;

            if (cel->tilemap) {
                if (layer->tileset >= ASE_MAX_TILESETS)
                    return ase_fail(err, "a tilemap's tileset is not in the file");

                const ase_tileset* tileset = &tilesets[layer->tileset];

                if (!ase_tilemap_pixels(scratch, cel, tileset, &pixels, err)) return false;

                cel_width *= tileset->width;
                cel_height *= tileset->height;
            } else {
                usize cel_size = cel_width * cel_height;

                pixels = orb_arena_push(scratch, cel_size, 1);

                if (orb_inflate(cel->zdata, cel->zlen, pixels, cel_size) != (isize)cel_size) {
                    return ase_fail(err, "cel decompression failed");
                }
            }

            for (usize y = 0; y < cel_height; y++) {
                i64 frame_y = cel->y + (i64)y;

                if (frame_y < 0 || frame_y >= out->height) continue;

                for (usize x = 0; x < cel_width; x++) {
                    i64 frame_x = cel->x + (i64)x;

                    if (frame_x < 0 || frame_x >= out->width) continue;

                    u8 index = pixels[y * cel_width + x];

                    if (index != out->transparent) frame[frame_y * out->width + frame_x] = index;
                }
            }
        }
    }

    return true;
}

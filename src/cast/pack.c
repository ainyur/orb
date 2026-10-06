#include "pack.h"

#include <string.h>

typedef struct {
    int x0, y0, x1, y1; // inclusive bounds; x1 < x0 when empty
} pack_bounds;

static pack_bounds pack_trim(const u8* frame, int width, int height) {
    pack_bounds bounds = {width, height, -1, -1};

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (frame[y * width + x] == 0) continue;
            if (x < bounds.x0) bounds.x0 = x;
            if (x > bounds.x1) bounds.x1 = x;
            if (y < bounds.y0) bounds.y0 = y;
            if (y > bounds.y1) bounds.y1 = y;
        }
    }

    return bounds;
}

static bool pack_same(
    const u8* frame_a,
    pack_bounds a,
    const u8* frame_b,
    pack_bounds b,
    int width
) {
    if (a.x1 - a.x0 != b.x1 - b.x0 || a.y1 - a.y0 != b.y1 - b.y0) return false;

    for (int y = 0; y <= a.y1 - a.y0; y++) {
        if (memcmp(
                frame_a + (a.y0 + y) * width + a.x0, frame_b + (b.y0 + y) * width + b.x0,
                a.x1 - a.x0 + 1
            ) != 0)
            return false;
    }

    return true;
}

void orb_pack_frames(
    arena* scratch,
    const u8* frames,
    u32 frame_count,
    orb_size frame,
    orb_pack* out
) {
    int width = frame.width, height = frame.height;
    usize frame_size = (usize)width * height;
    pack_bounds* bounds = orb_arena_push_array(scratch, pack_bounds, frame_count);
    u32* owner = orb_arena_push_array(scratch, u32, frame_count); // frame that a rect came from

    out->frames = orb_arena_push_array(scratch, orb_pack_frame, frame_count);
    out->rects = (orb_pack_rect_list) {
        .elems = orb_arena_push_array(scratch, orb_pack_rect, frame_count), .cap = frame_count
    };

    for (u32 frame_index = 0; frame_index < frame_count; frame_index++) {
        const u8* frame_data = frames + frame_index * frame_size;
        bounds[frame_index] = pack_trim(frame_data, width, height);
        bool empty = bounds[frame_index].x1 < bounds[frame_index].x0;
        u32 found = out->rects.len;

        for (u32 rect_index = 0; rect_index < out->rects.len; rect_index++) {
            const u8* other = frames + owner[rect_index] * frame_size;
            bool other_empty = bounds[owner[rect_index]].x1 < bounds[owner[rect_index]].x0;

            if (empty && other_empty) {
                found = rect_index;
                break;
            }

            if (!empty && !other_empty &&
                pack_same(
                    frame_data, bounds[frame_index], other, bounds[owner[rect_index]], width
                )) {
                found = rect_index;
                break;
            }
        }

        if (found == out->rects.len) {
            orb_pack_rect rect = {
                .width = empty ? 0 : (u16)(bounds[frame_index].x1 - bounds[frame_index].x0 + 1),
                .height = empty ? 0 : (u16)(bounds[frame_index].y1 - bounds[frame_index].y0 + 1)
            };

            owner[out->rects.len] = frame_index;
            (void)push(scratch, &out->rects, rect);
        }

        out->frames[frame_index].rect = found;
        out->frames[frame_index].ox = empty ? 0 : (i16)bounds[frame_index].x0;
        out->frames[frame_index].oy = empty ? 0 : (i16)bounds[frame_index].y0;
    }

    out->sheet_width = 256;

    for (u32 rect_index = 0; rect_index < out->rects.len; rect_index++) {
        if (out->rects.elems[rect_index].width > out->sheet_width)
            out->sheet_width = out->rects.elems[rect_index].width;
    }

    int cursor_x = 0, cursor_y = 0, shelf_height = 0;

    for (u32 rect_index = 0; rect_index < out->rects.len; rect_index++) {
        orb_pack_rect* rect = &out->rects.elems[rect_index];

        if (rect->width == 0) continue;

        if (cursor_x + rect->width > out->sheet_width) {
            cursor_x = 0;
            cursor_y += shelf_height;
            shelf_height = 0;
        }

        rect->x = (u16)cursor_x;
        rect->y = (u16)cursor_y;
        cursor_x += rect->width;

        if (rect->height > shelf_height) shelf_height = rect->height;
    }

    out->sheet_height = (u16)(cursor_y + shelf_height);
    out->pixels = orb_arena_push(scratch, (usize)out->sheet_width * out->sheet_height, 1);

    for (u32 rect_index = 0; rect_index < out->rects.len; rect_index++) {
        const orb_pack_rect* rect = &out->rects.elems[rect_index];
        const u8* frame_data = frames + owner[rect_index] * frame_size;
        pack_bounds owner_bounds = bounds[owner[rect_index]];

        for (int y = 0; y < rect->height; y++) {
            memcpy(
                out->pixels + (rect->y + y) * out->sheet_width + rect->x,
                frame_data + (owner_bounds.y0 + y) * width + owner_bounds.x0, rect->width
            );
        }
    }
}

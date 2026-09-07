#include "pack.h"

#include <stdalign.h>
#include <string.h>

typedef struct {
    int x0, y0, x1, y1; // inclusive bounds; x1 < x0 when empty
} pack_bounds;

static pack_bounds pack_trim(const uint8_t* frame, int w, int h) {
    pack_bounds b = {w, h, -1, -1};

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (frame[y * w + x] == 0) continue;
            if (x < b.x0) b.x0 = x;
            if (x > b.x1) b.x1 = x;
            if (y < b.y0) b.y0 = y;
            if (y > b.y1) b.y1 = y;
        }
    }

    return b;
}

static bool pack_same(const uint8_t* fa, pack_bounds a, const uint8_t* fb, pack_bounds b, int w) {
    if (a.x1 - a.x0 != b.x1 - b.x0 || a.y1 - a.y0 != b.y1 - b.y0) return false;

    for (int y = 0; y <= a.y1 - a.y0; y++) {
        if (memcmp(fa + (a.y0 + y) * w + a.x0, fb + (b.y0 + y) * w + b.x0, a.x1 - a.x0 + 1) != 0)
            return false;
    }

    return true;
}

void orb_pack_frames(
    orb_arena* a, const uint8_t* frames, uint32_t frame_count, uint16_t w, uint16_t h, orb_pack* out
) {
    size_t frame_size = (size_t)w * h;
    pack_bounds* bounds = orb_arena_push_array(a, pack_bounds, frame_count);
    uint32_t* owner = orb_arena_push_array(a, uint32_t, frame_count); // frame that a rect came from

    out->frames = orb_arena_push_array(a, orb_pack_frame, frame_count);
    out->rects = orb_arena_push_array(a, orb_pack_rect, frame_count);
    out->frame_count = frame_count;
    out->rect_count = 0;

    for (uint32_t f = 0; f < frame_count; f++) {
        const uint8_t* frame = frames + f * frame_size;
        bounds[f] = pack_trim(frame, w, h);
        bool empty = bounds[f].x1 < bounds[f].x0;
        uint32_t found = out->rect_count;

        for (uint32_t r = 0; r < out->rect_count; r++) {
            const uint8_t* other = frames + owner[r] * frame_size;
            bool other_empty = bounds[owner[r]].x1 < bounds[owner[r]].x0;

            if (empty && other_empty) {
                found = r;
                break;
            }

            if (!empty && !other_empty && pack_same(frame, bounds[f], other, bounds[owner[r]], w)) {
                found = r;
                break;
            }
        }

        if (found == out->rect_count) {
            orb_pack_rect* rect = &out->rects[out->rect_count];

            rect->w = empty ? 0 : (uint16_t)(bounds[f].x1 - bounds[f].x0 + 1);
            rect->h = empty ? 0 : (uint16_t)(bounds[f].y1 - bounds[f].y0 + 1);
            owner[out->rect_count++] = f;
        }

        out->frames[f].rect = found;
        out->frames[f].ox = empty ? 0 : (int16_t)bounds[f].x0;
        out->frames[f].oy = empty ? 0 : (int16_t)bounds[f].y0;
    }

    out->sheet_w = 256;

    for (uint32_t r = 0; r < out->rect_count; r++) {
        if (out->rects[r].w > out->sheet_w) out->sheet_w = out->rects[r].w;
    }

    int cursor_x = 0, cursor_y = 0, shelf_h = 0;

    for (uint32_t r = 0; r < out->rect_count; r++) {
        orb_pack_rect* rect = &out->rects[r];

        if (rect->w == 0) continue;

        if (cursor_x + rect->w > out->sheet_w) {
            cursor_x = 0;
            cursor_y += shelf_h;
            shelf_h = 0;
        }

        rect->x = (uint16_t)cursor_x;
        rect->y = (uint16_t)cursor_y;
        cursor_x += rect->w;

        if (rect->h > shelf_h) shelf_h = rect->h;
    }

    out->sheet_h = (uint16_t)(cursor_y + shelf_h);
    out->pixels = orb_arena_push(a, (size_t)out->sheet_w * out->sheet_h, 1);

    for (uint32_t r = 0; r < out->rect_count; r++) {
        const orb_pack_rect* rect = &out->rects[r];
        const uint8_t* frame = frames + owner[r] * frame_size;
        pack_bounds b = bounds[owner[r]];

        for (int y = 0; y < rect->h; y++) {
            memcpy(
                out->pixels + (rect->y + y) * out->sheet_w + rect->x, frame + (b.y0 + y) * w + b.x0,
                rect->w
            );
        }
    }
}

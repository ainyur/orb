#include "sprite.h"

// A handle is current when it carries the generation the table holds for its
// index; a table of nullptr means everything is at generation 0.
static bool sprite_current(const uint8_t* generations, uint32_t index, uint32_t generation) {
    return generations ? generations[index] == generation : generation == 0;
}

void orb_animation_start(orb_animation_state* st, orb_animation a) {
    st->animation = a;
    st->frame = 0;
    st->ticks = 0;
}

orb_sprite orb_animation_step(const orb_assets* assets, orb_animation_state* st) {
    uint32_t index = ORB_HANDLE_INDEX(st->animation);

    if (index >= assets->animation_count) return ORB_SPRITE(0xffffffu);
    if (!sprite_current(assets->animation_generations, index, ORB_HANDLE_GENERATION(st->animation)))
        return ORB_SPRITE(0xffffffu);

    const orb_animation_desc* d = &assets->animations[index];

    st->frame = (uint16_t)(st->frame % d->count); // state may outlive a recast that shrank the tag

    orb_sprite s = ORB_SPRITE(d->first_sprite + st->frame);

    st->ticks++;

    if (st->ticks >= assets->durations[d->first_duration + st->frame]) {
        st->ticks = 0;
        st->frame = (uint16_t)((st->frame + 1) % d->count);
    }

    return s;
}

void orb_sprite_draw(
    orb_framebuffer* fb, const orb_assets* assets, const orb_camera* cam, orb_sprite s, int x,
    int y, uint32_t flags, const uint8_t* remap
) {
    uint32_t index = ORB_HANDLE_INDEX(s);

    if (index >= assets->sprite_count) return;
    if (!sprite_current(assets->sprite_generations, index, ORB_HANDLE_GENERATION(s))) return;

    const orb_sprite_desc* d = &assets->sprites[index];

    if (d->w == 0) return;

    const orb_sheet_desc* sheet = &assets->sheets[d->sheet];

    if (cam) {
        x -= (int)cam->x;
        y -= (int)cam->y;
    }

    bool flip_x = flags & ORB_FLIP_X, flip_y = flags & ORB_FLIP_Y;
    int dx = x + (flip_x ? d->fw - d->ox - d->w : d->ox);
    int dy = y + (flip_y ? d->fh - d->oy - d->h : d->oy);
    const uint8_t* src = assets->pixels + sheet->pixels + d->y * sheet->w + d->x;

    orb_framebuffer_blit(fb, src, sheet->w, d->w, d->h, dx, dy, flip_x, flip_y, remap);
}

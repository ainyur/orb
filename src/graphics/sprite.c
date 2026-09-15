#include "sprite.h"

#include <stdio.h>

void orb_animation_start(orb_animation_state* st, orb_animation a) {
    st->animation = a;
    st->frame = 0;
    st->ticks = 0;
}

orb_sprite orb_animation_step(const orb_assets* assets, orb_animation_state* st) {
    uint32_t index = orb_asset_index_of(assets, st->animation);

    if (index == ORB_NO_INDEX) return ORB_NO_SPRITE;

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
    orb_framebuffer* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_sprite s,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
) {
    uint32_t index = orb_asset_index_of(assets, s);

    if (index == ORB_NO_INDEX) return;

    const orb_sprite_desc* d = &assets->sprites[index];

    if (d->width == 0) return;

    const orb_sheet_desc* sheet = &assets->sheets[d->sheet];

    at.x -= orb_floor(cam.x);
    at.y -= orb_floor(cam.y);
    at.x += flags & ORB_FLIP_X ? d->frame_width - d->ox - d->width : d->ox;
    at.y += flags & ORB_FLIP_Y ? d->frame_height - d->oy - d->height : d->oy;

    const uint8_t* src = assets->pixels + sheet->pixels + d->y * sheet->width + d->x;

    orb_framebuffer_blit(fb, src, sheet->width, (orb_size) {d->width, d->height}, at, flags, remap);
}

uint64_t orb_sprite_id(const char* stem, int frame) {
    char suffix[16];

    snprintf(suffix, sizeof suffix, "%d", frame);
    return orb_asset_id(stem, suffix);
}

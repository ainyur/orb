#include "sprite.h"

#include <stdio.h>

void orb_anim_start(orb_anim_state* state, orb_anim anim) {
    state->anim = anim;
    state->frame = 0;
    state->ticks = 0;
}

orb_sprite orb_anim_step(const orb_assets* assets, orb_anim_state* state) {
    uint32_t index = orb_asset_index_of(assets, state->anim);

    if (index == ORB_NO_INDEX) return ORB_NO_SPRITE;

    const orb_anim_desc* desc = &assets->anims[index];

    state->frame =
        (uint16_t)(state->frame % desc->count); // state may outlive a recast that shrank the tag

    orb_sprite sprite = ORB_SPRITE(desc->first_sprite + state->frame);

    state->ticks++;

    if (state->ticks >= assets->durations[desc->first_duration + state->frame]) {
        state->ticks = 0;
        state->frame = (uint16_t)((state->frame + 1) % desc->count);
    }

    return sprite;
}

orb_sprite orb_anim_frame(const orb_assets* assets, const orb_anim_state* state) {
    uint32_t index = orb_asset_index_of(assets, state->anim);

    if (index == ORB_NO_INDEX) return ORB_NO_SPRITE;

    const orb_anim_desc* desc = &assets->anims[index];

    return ORB_SPRITE(desc->first_sprite + state->frame % desc->count);
}

void orb_sprite_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_sprite sprite,
    orb_vec2 at,
    uint32_t flags,
    const uint8_t* remap
) {
    uint32_t index = orb_asset_index_of(assets, sprite);

    if (index == ORB_NO_INDEX) return;

    const orb_sprite_desc* desc = &assets->sprites[index];

    if (desc->width == 0) return;

    const orb_sheet_desc* sheet = &assets->sheets[desc->sheet];

    at.x -= orb_floor(cam.x);
    at.y -= orb_floor(cam.y);
    at.x += flags & ORB_FLIP_X ? desc->frame_width - desc->ox - desc->width : desc->ox;
    at.y += flags & ORB_FLIP_Y ? desc->frame_height - desc->oy - desc->height : desc->oy;

    const uint8_t* src = assets->pixels + sheet->pixels + desc->y * sheet->width + desc->x;

    orb_fb_blit(fb, src, sheet->width, (orb_size) {desc->width, desc->height}, at, flags, remap);
}

uint64_t orb_sprite_id(const char* stem, int frame) {
    char suffix[16];

    snprintf(suffix, sizeof suffix, "%d", frame);
    return orb_asset_id(stem, suffix);
}

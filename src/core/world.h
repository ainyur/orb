#pragma once

#include "../graphics/fb.h"
#include "../orb.h"

void orb_world_collision(const char* layer, const u8 kinds[256]);
void orb_world_revalidate(void); // after a recast: collision layers re-found
orb_cell_kind orb_world_cell_kind(orb_vec2 at);
void orb_world_gravity(orb_vec2f gravity);
void orb_world_update(void);
void orb_world_draw(orb_fb* fb, orb_vec2f cam, int layer);
void orb_world_debug_draw(u32* rgb, orb_size size, orb_vec2f cam);
void orb_world_remap_set(int index, const u8 table[256]);
orb_entity_id_list orb_query_rect(orb_arena* out, orb_rect rect, u32 mask, orb_entity_id except);
orb_entity_id_list orb_query_circle(
    orb_arena* out,
    orb_vec2 center,
    int radius,
    u32 mask,
    orb_entity_id except
);
orb_entity_id_list orb_query_point(orb_arena* out, orb_vec2 at, u32 mask, orb_entity_id except);
bool orb_query_ray(
    orb_vec2 from,
    orb_vec2 to,
    u32 mask,
    u32 flags,
    orb_entity_id except,
    orb_hit* hit
);

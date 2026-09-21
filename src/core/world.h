#pragma once

#include "../graphics/fb.h"
#include "../orb.h"

void orb_world_collision(const char* layer, const uint8_t kinds[256]);
void orb_world_revalidate(void); // after a recast: collision layers re-found
orb_cell_kind orb_world_cell_kind(orb_vec2 at);
void orb_world_gravity(orb_vec2f gravity);
void orb_world_update(void);
void orb_world_draw(orb_fb* fb, orb_vec2f cam, int layer);
void orb_world_debug_draw(uint32_t* rgb, orb_size size, orb_vec2f cam);
void orb_world_remap_set(int index, const uint8_t table[256]);
int orb_query_rect(orb_rect rect, uint32_t mask, orb_entity_id except, orb_entity_id* out, int max);
int orb_query_circle(
    orb_vec2 center,
    int radius,
    uint32_t mask,
    orb_entity_id except,
    orb_entity_id* out,
    int max
);
int orb_query_point(orb_vec2 at, uint32_t mask, orb_entity_id except, orb_entity_id* out, int max);
bool orb_query_ray(
    orb_vec2 from,
    orb_vec2 to,
    uint32_t mask,
    uint32_t flags,
    orb_entity_id except,
    orb_hit* hit
);

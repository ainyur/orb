#pragma once

#include "../orb.h"
#include "arena.h"
#include "asset.h"

typedef struct orb_type_fns {
    orb_entity_fn init, update;
} orb_type_fns;

typedef struct orb_sort_entry {
    i32 key;
    u32 slot;
} orb_sort_entry;

orb_slice(u32);

typedef struct orb_pool {
    orb_entity* entities;
    u8* gens;        // the generation the next spawn in a slot gets; 1 at boot
    u32* free_slots; // a FIFO ring of free slots
    u32 free_head, free_count;
    u32 max;
    void* components[ORB_MAX_COMPONENTS];
    u32 sizes[ORB_MAX_COMPONENTS];
    int kinds;            // registered kinds, the built-ins included
    orb_sort_entry* sort; // max entries, for the draw
    // storage for max entries; live ORB_BODY_SOLID slots, gathered once per update
    u32_slice solids;
} orb_pool;

extern bool orb_entity_debug;    // the entities.debug variable
extern bool orb_entity_updating; // inside world_update, so a spawn gets ORB_ENTITY_NEW

// Bytes the pool needs for the config.
usize orb_entity_region_size(const orb_config* config);
void orb_entity_boot(
    orb_arena* region,
    const orb_config* config,
    void* state,
    const orb_api* api,
    const orb_assets* assets
);
bool orb_entity_reset(const orb_config* config); // re-carves the region; false when it does not fit
void orb_entity_types_clear(void);
// After a recast: placements re-found by iid, previous the assets from before it.
void orb_entity_revalidate(const orb_assets* previous);
void orb_entity_free_despawning(void);

orb_pool* orb_entity_pool(void);
const orb_assets* orb_entity_assets(void);
void* orb_entity_state(void);
const orb_api* orb_entity_api(void);
const orb_type_fns* orb_entity_type_fns(u32 type_index);
orb_entity* orb_entity_at(u32 slot);   // live or nullptr
u32 orb_entity_slot(orb_entity_id id); // ORB_NO_INDEX when stale

void orb_type_bind(orb_type type, orb_entity_fn init, orb_entity_fn update);
orb_entity_id orb_entity_spawn(orb_type type, orb_vec2f at);
void orb_entity_despawn(orb_entity_id id);
orb_entity* orb_entity_get(orb_entity_id id);
void* orb_entity_add(orb_entity_id id, int kind);
void orb_entity_remove(orb_entity_id id, int kind);
void* orb_entity_component(orb_entity_id id, int kind);
orb_vec2f orb_entity_world_at(orb_entity_id id);
orb_entity_id_list orb_entity_all(orb_arena* out);
orb_entity_id_list orb_entity_of_type(orb_arena* out, orb_type type);
int orb_entity_field_count(orb_entity_id id, const char* name);
i32 orb_entity_field_int(orb_entity_id id, const char* name, int index);
f32 orb_entity_field_float(orb_entity_id id, const char* name, int index);
bool orb_entity_field_bool(orb_entity_id id, const char* name, int index);
const char* orb_entity_field_string(orb_entity_id id, const char* name, int index);
orb_vec2 orb_entity_field_point(orb_entity_id id, const char* name, int index);
orb_entity_id orb_entity_field_ref(orb_entity_id id, const char* name, int index);
void orb_level_spawn(orb_level level);
void orb_level_despawn(orb_level level);

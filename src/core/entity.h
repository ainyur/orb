#pragma once

#include "../orb.h"
#include "arena.h"
#include "asset.h"

typedef struct orb_type_fns {
    orb_entity_fn init, update;
} orb_type_fns;

typedef struct orb_sort_entry {
    int32_t key;
    uint32_t slot;
} orb_sort_entry;

typedef struct orb_pool {
    orb_entity* entities;
    uint8_t* gens;        // the generation the next spawn in a slot gets; 1 at boot
    uint32_t* free_slots; // a FIFO ring of free slots
    uint32_t free_head, free_count;
    uint32_t max;
    void* components[ORB_MAX_COMPONENTS];
    uint32_t sizes[ORB_MAX_COMPONENTS];
    int kinds;            // registered kinds, the built-ins included
    orb_sort_entry* sort; // max entries, for the draw
    uint32_t* solids;     // max entries; live ORB_BODY_SOLID slots, gathered once per update
    uint32_t solid_count;
} orb_pool;

extern bool orb_entity_debug;    // the entities.debug variable
extern bool orb_entity_updating; // inside world_update, so a spawn gets ORB_ENTITY_NEW

// Bytes the pool needs for the config.
size_t orb_entity_region_size(const orb_config* config);
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
const orb_type_fns* orb_entity_type_fns(uint32_t type_index);
orb_entity* orb_entity_at(uint32_t slot);   // live or nullptr
uint32_t orb_entity_slot(orb_entity_id id); // ORB_NO_INDEX when stale

void orb_type_bind(orb_type type, orb_entity_fn init, orb_entity_fn update);
orb_entity_id orb_entity_spawn(orb_type type, orb_vec2f at);
void orb_entity_despawn(orb_entity_id id);
orb_entity* orb_entity_get(orb_entity_id id);
void* orb_entity_add(orb_entity_id id, int kind);
void orb_entity_remove(orb_entity_id id, int kind);
void* orb_entity_component(orb_entity_id id, int kind);
orb_vec2f orb_entity_world_at(orb_entity_id id);
int orb_entity_all(orb_entity_id* out, int max);
int orb_entity_of_type(orb_type type, orb_entity_id* out, int max);
int orb_entity_field_count(orb_entity_id id, const char* name);
int32_t orb_entity_field_int(orb_entity_id id, const char* name, int index);
float orb_entity_field_float(orb_entity_id id, const char* name, int index);
bool orb_entity_field_bool(orb_entity_id id, const char* name, int index);
const char* orb_entity_field_string(orb_entity_id id, const char* name, int index);
orb_vec2 orb_entity_field_point(orb_entity_id id, const char* name, int index);
orb_entity_id orb_entity_field_ref(orb_entity_id id, const char* name, int index);
void orb_level_spawn(orb_level level);
void orb_level_despawn(orb_level level);

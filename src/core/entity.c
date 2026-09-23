#include "entity.h"
#include "../graphics/fb.h"
#include "../graphics/tilemap.h"
#include "bytes.h"
#include "console.h"
#include "log.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static orb_pool entity_pool;
static orb_arena* entity_region;
static void* entity_state;
static const orb_api* entity_api;
static const orb_assets* entity_assets;
static orb_type_fns entity_types[ORB_MAX_TYPES];
bool orb_entity_debug;
bool orb_entity_updating;

uint32_t orb_entity_slot(orb_entity_id id) {
    uint32_t index = ORB_HANDLE_INDEX(id);

    if (index >= entity_pool.max || entity_pool.entities[index].self.v != id.v) return ORB_NO_INDEX;

    return index;
}

orb_entity* orb_entity_at(uint32_t slot) {
    orb_entity* entity = &entity_pool.entities[slot];

    return entity->self.v == ORB_NO_INDEX ? nullptr : entity;
}

static void* entity_slot_component(uint32_t slot, int kind) {
    return (uint8_t*)entity_pool.components[kind] + (size_t)slot * entity_pool.sizes[kind];
}

static uint32_t entity_type_index(orb_type type) {
    return orb_asset_index_of(entity_assets, type);
}

// The placement an entity was spawned from, or nullptr.
static const orb_placement_desc* entity_placement(const orb_entity* entity) {
    if (entity->placement >= entity_assets->placement_count) return nullptr;

    return &entity_assets->placements[entity->placement];
}

// The field named id within fields[first, first + count), or nullptr.
static const orb_field_desc* entity_field_in(uint32_t first, uint32_t count, uint64_t id) {
    for (uint32_t k = 0; k < count; k++)
        if (entity_assets->fields[first + k].name == id) return &entity_assets->fields[first + k];

    return nullptr;
}

// The named field on the entity's placement, else on its type, or nullptr.
static const orb_field_desc* entity_field(const orb_entity* entity, const char* name) {
    uint64_t id = orb_asset_id(name, "");
    const orb_placement_desc* placement = entity_placement(entity);

    if (placement) {
        const orb_field_desc* field =
            entity_field_in(placement->first_field, placement->field_count, id);

        if (field) return field;
    }

    uint32_t type = entity_type_index(entity->type);

    if (type == ORB_NO_INDEX) return nullptr;

    return entity_field_in(
        entity_assets->types[type].first_field, entity_assets->types[type].field_count, id
    );
}

// The element's bytes, or nullptr for a stale handle, a missing field, a kind mismatch, or
// an index outside the count.
static const uint8_t* entity_element(
    orb_entity_id id,
    const char* name,
    int index,
    orb_field_kind kind
) {
    uint32_t slot = orb_entity_slot(id);

    if (slot == ORB_NO_INDEX) return nullptr;

    const orb_field_desc* field = entity_field(&entity_pool.entities[slot], name);

    if (!field || field->kind != kind || index < 0 || index >= field->count) return nullptr;

    return entity_assets->field_data + field->data + (uint32_t)index * orb_field_width(kind);
}

// Fills the slot at the head of the free ring and runs the type's init; ORB_NO_ENTITY when
// the pool is full.
static orb_entity_id entity_spawn_slot(
    uint32_t type,
    orb_vec2f at,
    orb_size size,
    uint32_t level,
    uint32_t placement,
    uint64_t iid
) {
    orb_pool* pool = &entity_pool;

    if (pool->free_count == 0) return ORB_NO_ENTITY;

    uint32_t slot = pool->free_slots[pool->free_head];

    pool->free_head = (pool->free_head + 1) % pool->max;
    pool->free_count--;

    orb_entity* entity = &pool->entities[slot];

    *entity = (orb_entity) {
        .iid = iid,
        .self = ORB_ENTITY(slot | (uint32_t)pool->gens[slot] << 24),
        .type = ORB_TYPE(type | (uint32_t)entity_assets->type_gens[type] << 24),
        .level = level == ORB_NO_INDEX
                     ? ORB_NO_LEVEL
                     : ORB_LEVEL(level | (uint32_t)entity_assets->level_gens[level] << 24),
        .parent = ORB_NO_ENTITY,
        .placement = placement,
        .flags = (uint16_t)(ORB_ENTITY_VISIBLE | (orb_entity_updating ? ORB_ENTITY_NEW : 0)),
        .at = at,
        .size = size
    };

    const orb_type_fns* fns = &entity_types[type];

    if (fns->init) fns->init(entity_state, entity_api, entity->self);

    return entity->self;
}

// Frees the slot: children are orphaned at their world position, the generation moves on
// (255 wraps to 1), and the slot joins the tail of the free ring.
static void entity_free(uint32_t slot) {
    orb_pool* pool = &entity_pool;
    orb_entity* entity = &pool->entities[slot];

    for (uint32_t i = 0; i < pool->max; i++) {
        orb_entity* child = &pool->entities[i];

        if (child->self.v == ORB_NO_INDEX || child->parent.v != entity->self.v) continue;

        child->at = orb_entity_world_at(child->self);
        child->parent = ORB_NO_ENTITY;
    }

    entity->self = ORB_NO_ENTITY;
    entity->components = 0;
    pool->gens[slot] = pool->gens[slot] == 255 ? 1 : (uint8_t)(pool->gens[slot] + 1);
    pool->free_slots[(pool->free_head + pool->free_count) % pool->max] = slot;
    pool->free_count++;
}

// Live, non-despawning handles, all or of one type index, in slot order.
static int entity_list(uint32_t type, orb_entity_id* out, int max) {
    int n = 0;

    for (uint32_t slot = 0; slot < entity_pool.max && n < max; slot++) {
        const orb_entity* entity = orb_entity_at(slot);

        if (!entity || entity->flags & ORB_ENTITY_DESPAWNING) continue;
        if (type != ORB_NO_INDEX && ORB_HANDLE_INDEX(entity->type) != type) continue;

        out[n++] = entity->self;
    }

    return n;
}

void orb_type_bind(orb_type type, orb_entity_fn init, orb_entity_fn update) {
    uint32_t index = entity_type_index(type);

    if (index == ORB_NO_INDEX) {
        orb_log("type_bind: stale or null type");
        return;
    }

    entity_types[index] = (orb_type_fns) {init, update};
}

orb_entity_id orb_entity_spawn(orb_type type, orb_vec2f at) {
    uint32_t index = entity_type_index(type);

    if (index == ORB_NO_INDEX) {
        orb_log("entity_spawn: stale or null type");
        return ORB_NO_ENTITY;
    }

    const orb_type_desc* type_desc = &entity_assets->types[index];
    orb_vec2 pixel = {orb_floor(at.x), orb_floor(at.y)};
    uint32_t level = orb_tilemap_level_at(entity_assets, pixel);
    orb_entity_id id = entity_spawn_slot(
        index, at, (orb_size) {type_desc->width, type_desc->height}, level, ORB_NO_INDEX, 0
    );

    if (id.v == ORB_NO_INDEX)
        orb_log("the entity pool is full at %u; type %u not spawned", entity_pool.max, index);

    return id;
}

void orb_entity_despawn(orb_entity_id id) {
    uint32_t slot = orb_entity_slot(id);

    if (slot != ORB_NO_INDEX) entity_pool.entities[slot].flags |= ORB_ENTITY_DESPAWNING;
}

orb_entity* orb_entity_get(orb_entity_id id) {
    uint32_t slot = orb_entity_slot(id);

    return slot == ORB_NO_INDEX ? nullptr : &entity_pool.entities[slot];
}

void* orb_entity_add(orb_entity_id id, int kind) {
    uint32_t slot = orb_entity_slot(id);

    if (slot == ORB_NO_INDEX || kind < 0 || kind >= entity_pool.kinds) return nullptr;

    orb_entity* entity = &entity_pool.entities[slot];
    void* component = entity_slot_component(slot, kind);

    if (entity->components & 1u << kind) return component;

    memset(component, 0, entity_pool.sizes[kind]);
    entity->components |= 1u << kind;

    if (kind == ORB_COMPONENT_SPRITE) {
        orb_sprite_component* sprite_component = component;

        sprite_component->sprite = ORB_NO_SPRITE;
        sprite_component->anim.anim = ORB_NO_ANIM;
        sprite_component->remap = -1;
    } else if (kind == ORB_COMPONENT_BODY) {
        orb_body* body = component;

        body->box.size = entity->size;
        body->standing_on = ORB_NO_ENTITY;
        body->carrier = ORB_NO_ENTITY;
        body->last_at = orb_entity_world_at(id);
    }

    return component;
}

void orb_entity_remove(orb_entity_id id, int kind) {
    uint32_t slot = orb_entity_slot(id);

    if (slot != ORB_NO_INDEX && kind >= 0 && kind < entity_pool.kinds)
        entity_pool.entities[slot].components &= ~(1u << kind);
}

void* orb_entity_component(orb_entity_id id, int kind) {
    uint32_t slot = orb_entity_slot(id);

    if (slot == ORB_NO_INDEX || kind < 0 || kind >= entity_pool.kinds) return nullptr;
    if (!(entity_pool.entities[slot].components & 1u << kind)) return nullptr;

    return entity_slot_component(slot, kind);
}

orb_vec2f orb_entity_world_at(orb_entity_id id) {
    const orb_entity* entity = orb_entity_get(id);

    if (!entity) return (orb_vec2f) {};

    const orb_entity* parent = orb_entity_get(entity->parent);

    if (!parent) return entity->at;

    return (orb_vec2f) {parent->at.x + entity->at.x, parent->at.y + entity->at.y};
}

int orb_entity_all(orb_entity_id* out, int max) {
    return entity_list(ORB_NO_INDEX, out, max);
}

int orb_entity_of_type(orb_type type, orb_entity_id* out, int max) {
    uint32_t index = entity_type_index(type);

    return index == ORB_NO_INDEX ? 0 : entity_list(index, out, max);
}

int orb_entity_field_count(orb_entity_id id, const char* name) {
    uint32_t slot = orb_entity_slot(id);

    if (slot == ORB_NO_INDEX) return 0;

    const orb_field_desc* field = entity_field(&entity_pool.entities[slot], name);

    return field ? field->count : 0;
}

int32_t orb_entity_field_int(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_INT);

    return bytes ? orb_bytes_i32(bytes) : 0;
}

float orb_entity_field_float(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_FLOAT);
    uint32_t bits = bytes ? orb_bytes_u32(bytes) : 0;
    float value;

    memcpy(&value, &bits, 4);
    return value;
}

bool orb_entity_field_bool(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_BOOL);

    return bytes && bytes[0] != 0;
}

const char* orb_entity_field_string(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_STRING);

    return bytes ? (const char*)entity_assets->field_data + orb_bytes_u32(bytes) : "";
}

orb_vec2 orb_entity_field_point(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_POINT);

    return bytes ? (orb_vec2) {orb_bytes_i32(bytes), orb_bytes_i32(bytes + 4)} : (orb_vec2) {};
}

orb_entity_id orb_entity_field_ref(orb_entity_id id, const char* name, int index) {
    const uint8_t* bytes = entity_element(id, name, index, ORB_FIELD_REF);

    if (!bytes) return ORB_NO_ENTITY;

    uint32_t target = orb_bytes_u32(bytes);

    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        const orb_entity* entity = orb_entity_at(slot);

        if (entity && !(entity->flags & ORB_ENTITY_DESPAWNING) && entity->placement == target)
            return entity->self;
    }

    return ORB_NO_ENTITY;
}

void orb_level_despawn(orb_level level) {
    uint32_t index = orb_asset_index_of(entity_assets, level);

    if (index == ORB_NO_INDEX) {
        orb_log("level_despawn: stale or null level");
        return;
    }

    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        orb_entity* entity = orb_entity_at(slot);

        if (!entity || entity->flags & ORB_ENTITY_PERSISTENT || entity->level.v == ORB_NO_INDEX)
            continue;
        if (ORB_HANDLE_INDEX(entity->level) == index) entity->flags |= ORB_ENTITY_DESPAWNING;
    }
}

void orb_level_spawn(orb_level level) {
    uint32_t index = orb_asset_index_of(entity_assets, level);

    if (index == ORB_NO_INDEX) {
        orb_log("level_spawn: stale or null level");
        return;
    }

    orb_level_despawn(level);

    const orb_level_desc* desc = &entity_assets->levels[index];

    for (uint32_t i = 0; i < desc->placement_count; i++) {
        uint32_t at = desc->first_placement + i;
        const orb_placement_desc* placement = &entity_assets->placements[at];
        orb_entity_id id = entity_spawn_slot(
            placement->type, (orb_vec2f) {(float)placement->x, (float)placement->y},
            (orb_size) {placement->width, placement->height}, index, at, placement->iid
        );

        if (id.v == ORB_NO_INDEX) {
            orb_log("level %u: the entity pool is full at %u", index, entity_pool.max);
            return;
        }
    }
}

// The new type index sharing a type id with the old index in previous, or ORB_NO_INDEX.
static uint32_t entity_type_remap(const orb_assets* previous, uint32_t old_index) {
    if (old_index >= previous->type_count) return ORB_NO_INDEX;

    uint64_t id = previous->type_ids[old_index];

    for (uint32_t i = 0; i < entity_assets->type_count; i++)
        if (entity_assets->type_ids[i] == id) return i;

    return ORB_NO_INDEX;
}

// Placements are re-found by iid; each entity's type index comes from the found placement,
// or else is mapped through the previous asset set's type ids. Generations are rebuilt last.
void orb_entity_revalidate(const orb_assets* previous) {
    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        orb_entity* entity = orb_entity_at(slot);

        if (!entity) continue;

        entity->type = ORB_TYPE(entity_type_remap(previous, ORB_HANDLE_INDEX(entity->type)));

        if (entity->placement == ORB_NO_INDEX && entity->iid == 0) continue;

        const orb_placement_desc* placement = entity_placement(entity);

        if (!placement || placement->iid != entity->iid) {
            placement = nullptr;
            entity->placement = ORB_NO_INDEX;

            for (uint32_t i = 0; i < entity_assets->placement_count; i++) {
                if (entity_assets->placements[i].iid != entity->iid) continue;

                placement = &entity_assets->placements[i];
                entity->placement = i;
                break;
            }
        }

        if (placement) entity->type = ORB_TYPE(placement->type);
    }

    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        orb_entity* entity = orb_entity_at(slot);

        if (!entity) continue;

        uint32_t index = ORB_HANDLE_INDEX(entity->type);

        entity->type = index < entity_assets->type_count
                           ? ORB_TYPE(index | (uint32_t)entity_assets->type_gens[index] << 24)
                           : ORB_NO_TYPE;
    }
}

void orb_entity_free_despawning(void) {
    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        const orb_entity* entity = orb_entity_at(slot);

        if (entity && entity->flags & ORB_ENTITY_DESPAWNING) entity_free(slot);
    }
}

void orb_entity_types_clear(void) {
    memset(entity_types, 0, sizeof entity_types);
}

orb_pool* orb_entity_pool(void) {
    return &entity_pool;
}

const orb_assets* orb_entity_assets(void) {
    return entity_assets;
}

void* orb_entity_state(void) {
    return entity_state;
}

const orb_api* orb_entity_api(void) {
    return entity_api;
}

const orb_type_fns* orb_entity_type_fns(uint32_t type_index) {
    static const orb_type_fns none;

    return type_index < ORB_MAX_TYPES ? &entity_types[type_index] : &none;
}

static void entity_command_spawn(void*, const orb_api*, int argc, const char* const* argv) {
    if (argc != 4) {
        orb_log("spawn <type> <x> <y>");
        return;
    }

    orb_type type = entity_api->type_find(argv[1]);

    if (type.v == ORB_NO_INDEX) return;

    orb_vec2f at = {strtof(argv[2], nullptr), strtof(argv[3], nullptr)};
    orb_entity_id id = orb_entity_spawn(type, at);

    if (id.v != ORB_NO_INDEX) orb_log("entity %u", ORB_HANDLE_INDEX(id));
}

// The slot an argument names, when it holds a live entity, else ORB_NO_INDEX with a log line.
static uint32_t entity_command_slot(const char* arg) {
    char* end;
    unsigned long slot = strtoul(arg, &end, 10);

    if (*end || slot >= entity_pool.max || !orb_entity_at((uint32_t)slot)) {
        orb_log("no entity %s", arg);
        return ORB_NO_INDEX;
    }

    return (uint32_t)slot;
}

static void entity_command_despawn(void*, const orb_api*, int argc, const char* const* argv) {
    if (argc != 2) {
        orb_log("despawn <index>");
        return;
    }

    uint32_t slot = entity_command_slot(argv[1]);

    if (slot != ORB_NO_INDEX) orb_entity_despawn(entity_pool.entities[slot].self);
}

static void entity_command_entities(void*, const orb_api*, int argc, const char* const* argv) {
    uint32_t type = ORB_NO_INDEX;
    int n = 0;

    if (argc > 2) {
        orb_log("entities [type]");
        return;
    }

    if (argc == 2) {
        type = entity_type_index(entity_api->type_find(argv[1]));

        if (type == ORB_NO_INDEX) return;
    }

    for (uint32_t slot = 0; slot < entity_pool.max; slot++) {
        const orb_entity* entity = orb_entity_at(slot);

        if (!entity || (type != ORB_NO_INDEX && ORB_HANDLE_INDEX(entity->type) != type)) continue;

        orb_vec2f at = orb_entity_world_at(entity->self);

        orb_log(
            "%u type %u (%g, %g) %dx%d %c%c%c%c", slot, ORB_HANDLE_INDEX(entity->type), at.x, at.y,
            entity->size.width, entity->size.height, entity->flags & ORB_ENTITY_VISIBLE ? 'v' : '-',
            entity->flags & ORB_ENTITY_PAUSED ? 'p' : '-',
            entity->flags & ORB_ENTITY_PERSISTENT ? 'P' : '-',
            entity->flags & ORB_ENTITY_DESPAWNING ? 'd' : '-'
        );
        n++;
    }

    orb_log("%d entities", n);
}

// One field row: its name id, kind, count, and values.
static void entity_log_field(const orb_field_desc* field) {
    static const char* const kinds[] = {"int", "float", "bool", "string", "point", "ref"};
    char line[ORB_LOG_LINE_MAX];
    int n = snprintf(
        line, sizeof line, "  %016llx %s[%u]:", (unsigned long long)field->name, kinds[field->kind],
        field->count
    );

    for (uint32_t k = 0; k < field->count && n < (int)sizeof line; k++) {
        const uint8_t* bytes =
            entity_assets->field_data + field->data + k * orb_field_width(field->kind);
        size_t room = sizeof line - (size_t)n;

        switch (field->kind) {
        case ORB_FIELD_INT:
            n += snprintf(line + n, room, " %d", orb_bytes_i32(bytes));
            break;
        case ORB_FIELD_FLOAT: {
            uint32_t bits = orb_bytes_u32(bytes);
            float value;

            memcpy(&value, &bits, 4);
            n += snprintf(line + n, room, " %g", value);
            break;
        }
        case ORB_FIELD_BOOL:
            n += snprintf(line + n, room, " %s", bytes[0] ? "true" : "false");
            break;
        case ORB_FIELD_STRING:
            n += snprintf(
                line + n, room, " \"%s\"",
                (const char*)entity_assets->field_data + orb_bytes_u32(bytes)
            );
            break;
        case ORB_FIELD_POINT:
            n += snprintf(
                line + n, room, " (%d, %d)", orb_bytes_i32(bytes), orb_bytes_i32(bytes + 4)
            );
            break;
        case ORB_FIELD_REF:
            n += snprintf(line + n, room, " #%u", orb_bytes_u32(bytes));
            break;
        }
    }

    orb_log("%s", line);
}

// Fields [first, first + count), one log line each.
static void entity_log_fields(uint32_t first, uint32_t count) {
    for (uint32_t k = 0; k < count; k++)
        entity_log_field(&entity_assets->fields[first + k]);
}

static void entity_command_entity(void*, const orb_api*, int argc, const char* const* argv) {
    if (argc != 2) {
        orb_log("entity <index>");
        return;
    }

    uint32_t slot = entity_command_slot(argv[1]);

    if (slot == ORB_NO_INDEX) return;

    const orb_entity* entity = &entity_pool.entities[slot];
    orb_vec2f at = orb_entity_world_at(entity->self);
    const orb_placement_desc* placement = entity_placement(entity);
    uint32_t type = ORB_HANDLE_INDEX(entity->type);

    orb_log(
        "entity %u: type %u level %u placement %u at (%g, %g) %dx%d flags %#x parent %u", slot,
        type, ORB_HANDLE_INDEX(entity->level), entity->placement, at.x, at.y, entity->size.width,
        entity->size.height, entity->flags, ORB_HANDLE_INDEX(entity->parent)
    );

    if (placement) entity_log_fields(placement->first_field, placement->field_count);

    if (type < entity_assets->type_count)
        entity_log_fields(
            entity_assets->types[type].first_field, entity_assets->types[type].field_count
        );

    for (int kind = 0; kind < entity_pool.kinds; kind++) {
        if (!(entity->components & 1u << kind)) continue;

        const uint8_t* bytes = entity_slot_component(slot, kind);

        orb_log("component %d, %u bytes", kind, entity_pool.sizes[kind]);

        for (uint32_t i = 0; i < entity_pool.sizes[kind]; i += 16) {
            char line[ORB_LOG_LINE_MAX];
            int n = 0;

            for (uint32_t j = i; j < entity_pool.sizes[kind] && j < i + 16; j++)
                n += snprintf(line + n, sizeof line - (size_t)n, "%02x ", bytes[j]);

            orb_log("  %s", line);
        }
    }
}

// Before the console boots, so these are orb's own and survive every clear.
static void entity_console_register(void) {
    static bool done;

    if (done) return;

    done = true;
    orb_console_command("spawn", entity_command_spawn, "spawn <type> <x> <y>");
    orb_console_command("despawn", entity_command_despawn, "despawn <index>");
    orb_console_command(
        "entities", entity_command_entities, "list live entities, or those of a type"
    );
    orb_console_command(
        "entity", entity_command_entity, "entity <index>: record, fields, components"
    );
    orb_console_var_bool("entities.debug", &orb_entity_debug, "outline every body");
}

size_t orb_entity_region_size(const orb_config* config) {
    size_t slots = config->max_entities, total = 0;

    total += slots * sizeof(orb_entity) + 16;
    total += slots + 16;
    total += slots * sizeof(uint32_t) + 16;
    total += slots * sizeof(orb_sort_entry) + 16;
    total += slots * sizeof(uint32_t) + 16;
    total += slots * (sizeof(orb_sprite_component) + sizeof(orb_body) + sizeof(orb_tag)) + 3 * 16;

    for (int i = 0; i < ORB_MAX_COMPONENTS - ORB_COMPONENT_GAME && config->components[i]; i++)
        total += slots * config->components[i] + 16;

    return total;
}

bool orb_entity_reset(const orb_config* config) {
    if (config->max_entities == 0 || config->max_entities > ORB_MAX_ENTITIES)
        orb_fatal("max_entities %u is not 1 to %u", config->max_entities, ORB_MAX_ENTITIES);

    if (orb_entity_region_size(config) > entity_region->size) return false;

    orb_arena_reset(entity_region);

    orb_pool* pool = &entity_pool;

    *pool = (orb_pool) {.max = config->max_entities};
    pool->entities = orb_arena_push_array(entity_region, orb_entity, pool->max);
    pool->gens = orb_arena_push_array(entity_region, uint8_t, pool->max);
    pool->free_slots = orb_arena_push_array(entity_region, uint32_t, pool->max);
    pool->sort = orb_arena_push_array(entity_region, orb_sort_entry, pool->max);
    pool->solids = orb_arena_push_array(entity_region, uint32_t, pool->max);
    pool->sizes[ORB_COMPONENT_SPRITE] = sizeof(orb_sprite_component);
    pool->sizes[ORB_COMPONENT_BODY] = sizeof(orb_body);
    pool->sizes[ORB_COMPONENT_TAG] = sizeof(orb_tag);
    pool->kinds = ORB_COMPONENT_GAME;

    for (int i = 0; i < ORB_MAX_COMPONENTS - ORB_COMPONENT_GAME && config->components[i]; i++)
        pool->sizes[pool->kinds++] = config->components[i];

    for (int k = 0; k < pool->kinds; k++)
        pool->components[k] = orb_arena_push(entity_region, (size_t)pool->sizes[k] * pool->max, 16);

    for (uint32_t i = 0; i < pool->max; i++) {
        pool->entities[i].self = ORB_NO_ENTITY;
        pool->gens[i] = 1;
        pool->free_slots[i] = i;
    }

    pool->free_count = pool->max;
    orb_entity_types_clear();
    return true;
}

void orb_entity_boot(
    orb_arena* region,
    const orb_config* config,
    void* state,
    const orb_api* api,
    const orb_assets* assets
) {
    entity_region = region;
    entity_state = state;
    entity_api = api;
    entity_assets = assets;
    orb_entity_updating = false;
    entity_console_register();

    if (!orb_entity_reset(config)) orb_fatal("the entity pool does not fit its region");
}

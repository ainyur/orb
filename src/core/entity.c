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
    orb_entity* e = &entity_pool.entities[slot];

    return e->self.v == ORB_NO_INDEX ? nullptr : e;
}

static void* entity_slot_component(uint32_t slot, int kind) {
    return (uint8_t*)entity_pool.components[kind] + (size_t)slot * entity_pool.sizes[kind];
}

static uint32_t entity_type_index(orb_type type) {
    return orb_asset_index_of(entity_assets, type);
}

// The placement an entity was spawned from, or nullptr.
static const orb_placement_desc* entity_placement(const orb_entity* e) {
    if (e->placement >= entity_assets->placement_count) return nullptr;

    return &entity_assets->placements[e->placement];
}

// The field named id within fields[first, first + count), or nullptr.
static const orb_field_desc* entity_field_in(uint32_t first, uint32_t count, uint64_t id) {
    for (uint32_t k = 0; k < count; k++)
        if (entity_assets->fields[first + k].name == id) return &entity_assets->fields[first + k];

    return nullptr;
}

// The named field on the entity's placement, else on its type, or nullptr.
static const orb_field_desc* entity_field(const orb_entity* e, const char* name) {
    uint64_t id = orb_asset_id(name, "");
    const orb_placement_desc* p = entity_placement(e);

    if (p) {
        const orb_field_desc* f = entity_field_in(p->first_field, p->field_count, id);

        if (f) return f;
    }

    uint32_t type = entity_type_index(e->type);

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

    const orb_field_desc* f = entity_field(&entity_pool.entities[slot], name);

    if (!f || f->kind != kind || index < 0 || index >= f->count) return nullptr;

    return entity_assets->field_data + f->data + (uint32_t)index * orb_field_width(kind);
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
    orb_pool* p = &entity_pool;

    if (p->free_count == 0) return ORB_NO_ENTITY;

    uint32_t slot = p->free_slots[p->free_head];

    p->free_head = (p->free_head + 1) % p->max;
    p->free_count--;

    orb_entity* e = &p->entities[slot];

    *e = (orb_entity) {
        .iid = iid,
        .self = ORB_ENTITY(slot | (uint32_t)p->gens[slot] << 24),
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

    if (fns->init) fns->init(entity_state, entity_api, e->self);

    return e->self;
}

// Frees the slot: children are orphaned at their world position, the generation moves on
// (255 wraps to 1), and the slot joins the tail of the free ring.
static void entity_free(uint32_t slot) {
    orb_pool* p = &entity_pool;
    orb_entity* e = &p->entities[slot];

    for (uint32_t i = 0; i < p->max; i++) {
        orb_entity* child = &p->entities[i];

        if (child->self.v == ORB_NO_INDEX || child->parent.v != e->self.v) continue;

        child->at = orb_entity_world_at(child->self);
        child->parent = ORB_NO_ENTITY;
    }

    e->self = ORB_NO_ENTITY;
    e->components = 0;
    p->gens[slot] = p->gens[slot] == 255 ? 1 : (uint8_t)(p->gens[slot] + 1);
    p->free_slots[(p->free_head + p->free_count) % p->max] = slot;
    p->free_count++;
}

// Live, non-despawning handles, all or of one type index, in slot order.
static int entity_list(uint32_t type, orb_entity_id* out, int max) {
    int n = 0;

    for (uint32_t s = 0; s < entity_pool.max && n < max; s++) {
        const orb_entity* e = orb_entity_at(s);

        if (!e || e->flags & ORB_ENTITY_DESPAWNING) continue;
        if (type != ORB_NO_INDEX && ORB_HANDLE_INDEX(e->type) != type) continue;

        out[n++] = e->self;
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

    const orb_type_desc* t = &entity_assets->types[index];
    orb_vec2 pixel = {orb_floor(at.x), orb_floor(at.y)};
    uint32_t level = orb_tilemap_level_at(entity_assets, pixel);
    orb_entity_id id =
        entity_spawn_slot(index, at, (orb_size) {t->width, t->height}, level, ORB_NO_INDEX, 0);

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

    orb_entity* e = &entity_pool.entities[slot];
    void* c = entity_slot_component(slot, kind);

    if (e->components & 1u << kind) return c;

    memset(c, 0, entity_pool.sizes[kind]);
    e->components |= 1u << kind;

    if (kind == ORB_COMPONENT_SPRITE) {
        orb_sprite_component* s = c;

        s->sprite = ORB_NO_SPRITE;
        s->anim.anim = ORB_NO_ANIM;
        s->remap = -1;
    } else if (kind == ORB_COMPONENT_BODY) {
        orb_body* b = c;

        b->box.size = e->size;
        b->standing_on = ORB_NO_ENTITY;
        b->carrier = ORB_NO_ENTITY;
        b->last_at = orb_entity_world_at(id);
    }

    return c;
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
    const orb_entity* e = orb_entity_get(id);

    if (!e) return (orb_vec2f) {};

    const orb_entity* parent = orb_entity_get(e->parent);

    if (!parent) return e->at;

    return (orb_vec2f) {parent->at.x + e->at.x, parent->at.y + e->at.y};
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

    const orb_field_desc* f = entity_field(&entity_pool.entities[slot], name);

    return f ? f->count : 0;
}

int32_t orb_entity_field_int(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_INT);

    return p ? orb_bytes_i32(p) : 0;
}

float orb_entity_field_float(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_FLOAT);
    uint32_t bits = p ? orb_bytes_u32(p) : 0;
    float v;

    memcpy(&v, &bits, 4);
    return v;
}

bool orb_entity_field_bool(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_BOOL);

    return p && p[0] != 0;
}

const char* orb_entity_field_string(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_STRING);

    return p ? (const char*)entity_assets->field_data + orb_bytes_u32(p) : "";
}

orb_vec2 orb_entity_field_point(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_POINT);

    return p ? (orb_vec2) {orb_bytes_i32(p), orb_bytes_i32(p + 4)} : (orb_vec2) {};
}

orb_entity_id orb_entity_field_ref(orb_entity_id id, const char* name, int index) {
    const uint8_t* p = entity_element(id, name, index, ORB_FIELD_REF);

    if (!p) return ORB_NO_ENTITY;

    uint32_t target = orb_bytes_u32(p);

    for (uint32_t s = 0; s < entity_pool.max; s++) {
        const orb_entity* e = orb_entity_at(s);

        if (e && !(e->flags & ORB_ENTITY_DESPAWNING) && e->placement == target) return e->self;
    }

    return ORB_NO_ENTITY;
}

void orb_level_despawn(orb_level level) {
    uint32_t index = orb_asset_index_of(entity_assets, level);

    if (index == ORB_NO_INDEX) {
        orb_log("level_despawn: stale or null level");
        return;
    }

    for (uint32_t s = 0; s < entity_pool.max; s++) {
        orb_entity* e = orb_entity_at(s);

        if (!e || e->flags & ORB_ENTITY_PERSISTENT || e->level.v == ORB_NO_INDEX) continue;
        if (ORB_HANDLE_INDEX(e->level) == index) e->flags |= ORB_ENTITY_DESPAWNING;
    }
}

void orb_level_spawn(orb_level level) {
    uint32_t index = orb_asset_index_of(entity_assets, level);

    if (index == ORB_NO_INDEX) {
        orb_log("level_spawn: stale or null level");
        return;
    }

    orb_level_despawn(level);

    const orb_level_desc* d = &entity_assets->levels[index];

    for (uint32_t i = 0; i < d->placement_count; i++) {
        uint32_t at = d->first_placement + i;
        const orb_placement_desc* p = &entity_assets->placements[at];
        orb_entity_id id = entity_spawn_slot(
            p->type, (orb_vec2f) {(float)p->x, (float)p->y}, (orb_size) {p->width, p->height},
            index, at, p->iid
        );

        if (id.v == ORB_NO_INDEX) {
            orb_log("level %u: the entity pool is full at %u", index, entity_pool.max);
            return;
        }
    }
}

// A recast can bump a type's generation, reorder placements, or both. Every live entity's
// placement is re-found by iid alone, and its type index and generation are rebuilt from
// the found placement, or from the kept index when none matches.
void orb_entity_revalidate(void) {
    for (uint32_t s = 0; s < entity_pool.max; s++) {
        orb_entity* e = orb_entity_at(s);

        if (!e || (e->placement == ORB_NO_INDEX && e->iid == 0)) continue;

        const orb_placement_desc* p = entity_placement(e);

        if (!p || p->iid != e->iid) {
            p = nullptr;
            e->placement = ORB_NO_INDEX;

            for (uint32_t i = 0; i < entity_assets->placement_count; i++) {
                if (entity_assets->placements[i].iid != e->iid) continue;

                p = &entity_assets->placements[i];
                e->placement = i;
                break;
            }
        }

        if (p) e->type = ORB_TYPE(p->type);
    }

    for (uint32_t s = 0; s < entity_pool.max; s++) {
        orb_entity* e = orb_entity_at(s);

        if (!e) continue;

        uint32_t index = ORB_HANDLE_INDEX(e->type);

        e->type = index < entity_assets->type_count
                      ? ORB_TYPE(index | (uint32_t)entity_assets->type_gens[index] << 24)
                      : ORB_NO_TYPE;
    }
}

void orb_entity_free_despawning(void) {
    for (uint32_t s = 0; s < entity_pool.max; s++) {
        const orb_entity* e = orb_entity_at(s);

        if (e && e->flags & ORB_ENTITY_DESPAWNING) entity_free(s);
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

    for (uint32_t s = 0; s < entity_pool.max; s++) {
        const orb_entity* e = orb_entity_at(s);

        if (!e || (type != ORB_NO_INDEX && ORB_HANDLE_INDEX(e->type) != type)) continue;

        orb_vec2f at = orb_entity_world_at(e->self);

        orb_log(
            "%u type %u (%g, %g) %dx%d %c%c%c%c", s, ORB_HANDLE_INDEX(e->type), at.x, at.y,
            e->size.width, e->size.height, e->flags & ORB_ENTITY_VISIBLE ? 'v' : '-',
            e->flags & ORB_ENTITY_PAUSED ? 'p' : '-', e->flags & ORB_ENTITY_PERSISTENT ? 'P' : '-',
            e->flags & ORB_ENTITY_DESPAWNING ? 'd' : '-'
        );
        n++;
    }

    orb_log("%d entities", n);
}

// One field row: its name id, kind, count, and values.
static void entity_log_field(const orb_field_desc* f) {
    static const char* const kinds[] = {"int", "float", "bool", "string", "point", "ref"};
    char line[ORB_LOG_LINE_MAX];
    int n = snprintf(
        line, sizeof line, "  %016llx %s[%u]:", (unsigned long long)f->name, kinds[f->kind],
        f->count
    );

    for (uint32_t k = 0; k < f->count && n < (int)sizeof line; k++) {
        const uint8_t* p = entity_assets->field_data + f->data + k * orb_field_width(f->kind);
        size_t room = sizeof line - (size_t)n;

        switch (f->kind) {
        case ORB_FIELD_INT:
            n += snprintf(line + n, room, " %d", orb_bytes_i32(p));
            break;
        case ORB_FIELD_FLOAT: {
            uint32_t bits = orb_bytes_u32(p);
            float v;

            memcpy(&v, &bits, 4);
            n += snprintf(line + n, room, " %g", v);
            break;
        }
        case ORB_FIELD_BOOL:
            n += snprintf(line + n, room, " %s", p[0] ? "true" : "false");
            break;
        case ORB_FIELD_STRING:
            n += snprintf(
                line + n, room, " \"%s\"", (const char*)entity_assets->field_data + orb_bytes_u32(p)
            );
            break;
        case ORB_FIELD_POINT:
            n += snprintf(line + n, room, " (%d, %d)", orb_bytes_i32(p), orb_bytes_i32(p + 4));
            break;
        case ORB_FIELD_REF:
            n += snprintf(line + n, room, " #%u", orb_bytes_u32(p));
            break;
        }
    }

    orb_log("%s", line);
}

static void entity_command_entity(void*, const orb_api*, int argc, const char* const* argv) {
    if (argc != 2) {
        orb_log("entity <index>");
        return;
    }

    uint32_t slot = entity_command_slot(argv[1]);

    if (slot == ORB_NO_INDEX) return;

    const orb_entity* e = &entity_pool.entities[slot];
    orb_vec2f at = orb_entity_world_at(e->self);
    const orb_placement_desc* p = entity_placement(e);
    uint32_t type = ORB_HANDLE_INDEX(e->type);

    orb_log(
        "entity %u: type %u level %u placement %u at (%g, %g) %dx%d flags %#x parent %u", slot,
        type, ORB_HANDLE_INDEX(e->level), e->placement, at.x, at.y, e->size.width, e->size.height,
        e->flags, ORB_HANDLE_INDEX(e->parent)
    );

    for (uint32_t k = 0; p && k < p->field_count; k++)
        entity_log_field(&entity_assets->fields[p->first_field + k]);

    for (uint32_t k = 0;
         type < entity_assets->type_count && k < entity_assets->types[type].field_count; k++)
        entity_log_field(&entity_assets->fields[entity_assets->types[type].first_field + k]);

    for (int kind = 0; kind < entity_pool.kinds; kind++) {
        if (!(e->components & 1u << kind)) continue;

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

size_t orb_entity_region_size(const orb_config* c, int scale) {
    size_t slots = (size_t)c->max_entities * (size_t)scale, total = 0;

    total += slots * sizeof(orb_entity) + 16;
    total += slots + 16;
    total += slots * sizeof(uint32_t) + 16;
    total += slots * sizeof(orb_sort_entry) + 16;
    total += slots * (sizeof(orb_sprite_component) + sizeof(orb_body) + sizeof(orb_tag)) + 3 * 16;

    for (int i = 0; i < ORB_MAX_COMPONENTS - ORB_COMPONENT_GAME && c->components[i]; i++)
        total += slots * (size_t)c->components[i] * (size_t)scale + 16;

    return total;
}

bool orb_entity_reset(const orb_config* c) {
    if (c->max_entities == 0 || c->max_entities > ORB_MAX_ENTITIES)
        orb_fatal("max_entities %u is not 1 to %u", c->max_entities, ORB_MAX_ENTITIES);

    if (orb_entity_region_size(c, 1) > entity_region->size) return false;

    orb_arena_reset(entity_region);

    orb_pool* p = &entity_pool;

    *p = (orb_pool) {.max = c->max_entities};
    p->entities = orb_arena_push_array(entity_region, orb_entity, p->max);
    p->gens = orb_arena_push_array(entity_region, uint8_t, p->max);
    p->free_slots = orb_arena_push_array(entity_region, uint32_t, p->max);
    p->sort = orb_arena_push_array(entity_region, orb_sort_entry, p->max);
    p->sizes[ORB_COMPONENT_SPRITE] = sizeof(orb_sprite_component);
    p->sizes[ORB_COMPONENT_BODY] = sizeof(orb_body);
    p->sizes[ORB_COMPONENT_TAG] = sizeof(orb_tag);
    p->kinds = ORB_COMPONENT_GAME;

    for (int i = 0; i < ORB_MAX_COMPONENTS - ORB_COMPONENT_GAME && c->components[i]; i++)
        p->sizes[p->kinds++] = c->components[i];

    for (int k = 0; k < p->kinds; k++)
        p->components[k] = orb_arena_push(entity_region, (size_t)p->sizes[k] * p->max, 16);

    for (uint32_t i = 0; i < p->max; i++) {
        p->entities[i].self = ORB_NO_ENTITY;
        p->gens[i] = 1;
        p->free_slots[i] = i;
    }

    p->free_count = p->max;
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

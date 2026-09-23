#include "world.h"
#include "../graphics/pal.h"
#include "../graphics/sprite.h"
#include "../graphics/tilemap.h"
#include "api.h"
#include "entity.h"
#include "log.h"
#include "macros.h"

#include <stdlib.h>
#include <string.h>

static uint64_t world_layer_id; // 0 until world_collision names a layer
static uint8_t world_kinds[256];
static int16_t world_layers[ORB_MAX_LEVELS]; // each level's collision layer index, or -1
static orb_vec2f world_gravity_value;
static uint8_t world_remaps[ORB_MAX_REMAPS][256];
static bool world_remap_live[ORB_MAX_REMAPS];

typedef struct world_box {
    int left, top, right, bottom; // right and bottom exclusive
} world_box;

typedef bool (*world_cell_fn)(void* ctx, orb_cell_kind kind, world_box cell);

static int world_floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static bool world_overlaps(world_box a, world_box b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

static world_box world_body_box(const orb_entity* e, const orb_body* b) {
    orb_vec2f at = orb_entity_world_at(e->self);
    int x = orb_floor(at.x) + b->box.at.x, y = orb_floor(at.y) + b->box.at.y;

    return (world_box) {x, y, x + b->box.size.width, y + b->box.size.height};
}

// A live, non-despawning entity with a body, or nullptr.
static orb_entity* world_bodied(uint32_t slot, orb_body** body) {
    orb_entity* e = orb_entity_at(slot);

    if (!e || e->flags & ORB_ENTITY_DESPAWNING || !(e->components & 1u << ORB_COMPONENT_BODY))
        return nullptr;

    *body = orb_entity_component(e->self, ORB_COMPONENT_BODY);
    return e;
}

// Whether the update moves the entity: unpaused and parentless.
static bool world_moves(const orb_entity* e) {
    return !(e->flags & ORB_ENTITY_PAUSED) && orb_entity_get(e->parent) == nullptr;
}

static uint16_t world_oneways(const orb_body* b) {
    return b->flags &
           (ORB_BODY_ONEWAY_N | ORB_BODY_ONEWAY_S | ORB_BODY_ONEWAY_E | ORB_BODY_ONEWAY_W);
}

static bool world_full_solid(const orb_body* b) {
    return (b->flags & ORB_BODY_SOLID) && !world_oneways(b);
}

// The one-way cell kind and body flag that block a move: +x arrives from the west.
static orb_cell_kind world_oneway_kind(int axis, int dir) {
    if (axis == 0) return dir > 0 ? ORB_CELL_ONEWAY_W : ORB_CELL_ONEWAY_E;

    return dir > 0 ? ORB_CELL_ONEWAY_N : ORB_CELL_ONEWAY_S;
}

static uint16_t world_oneway_flag(int axis, int dir) {
    if (axis == 0) return dir > 0 ? ORB_BODY_ONEWAY_W : ORB_BODY_ONEWAY_E;

    return dir > 0 ? ORB_BODY_ONEWAY_N : ORB_BODY_ONEWAY_S;
}

void orb_world_revalidate(void) {
    const orb_assets* as = orb_entity_assets();

    for (uint32_t level = 0; level < as->level_count; level++) {
        const orb_level_desc* d = &as->levels[level];

        world_layers[level] = -1;

        for (int i = 0; world_layer_id && i < d->layer_count; i++) {
            if (as->layer_ids[d->first_layer + i] != world_layer_id) continue;
            if (as->layers[d->first_layer + i].cells != ORB_NO_INDEX)
                world_layers[level] = (int16_t)i;

            break;
        }
    }
}

orb_cell_kind orb_world_cell_kind(orb_vec2 at) {
    if (!world_layer_id) return ORB_CELL_OPEN;

    const orb_assets* as = orb_entity_assets();
    uint32_t level = orb_tilemap_level_at(as, at);

    if (level == ORB_NO_INDEX) return ORB_CELL_OPEN;

    int layer = world_layers[level];

    if (layer < 0) return ORB_CELL_OPEN;

    orb_level handle = ORB_LEVEL(level | (uint32_t)as->level_gens[level] << 24);

    return (orb_cell_kind)world_kinds[orb_tilemap_cell(as, handle, layer, at)];
}

// Calls fn for every non-open collision cell overlapping the box, in every level the box
// touches, until fn returns false.
static void world_cells(world_box box, world_cell_fn fn, void* ctx) {
    if (!world_layer_id || box.right <= box.left || box.bottom <= box.top) return;

    const orb_assets* as = orb_entity_assets();

    for (uint32_t i = 0; i < as->level_count; i++) {
        const orb_level_desc* d = &as->levels[i];
        world_box bounds = {d->world_x, d->world_y, d->world_x + d->width, d->world_y + d->height};

        if (!world_overlaps(box, bounds)) continue;

        int layer = world_layers[i];

        if (layer < 0) continue;

        const orb_layer_desc* l = &as->layers[d->first_layer + layer];
        int ox = d->world_x + l->offset_x, oy = d->world_y + l->offset_y, g = l->grid;
        int c0 = orb_max(0, world_floor_div(box.left - ox, g));
        int c1 = orb_min(l->columns - 1, world_floor_div(box.right - 1 - ox, g));
        int r0 = orb_max(0, world_floor_div(box.top - oy, g));
        int r1 = orb_min(l->rows - 1, world_floor_div(box.bottom - 1 - oy, g));

        for (int r = r0; r <= r1; r++) {
            for (int c = c0; c <= c1; c++) {
                orb_cell_kind kind =
                    (orb_cell_kind)world_kinds[as->cells[l->cells + r * l->columns + c]];

                if (kind == ORB_CELL_OPEN) continue;

                world_box cell = {ox + c * g, oy + r * g, ox + (c + 1) * g, oy + (r + 1) * g};

                if (!fn(ctx, kind, cell)) return;
            }
        }
    }
}

typedef struct world_probe {
    int axis, dir, from, limit;
    bool drop;
} world_probe;

// A cell blocks when it is solid, or one-way against this direction without DROP, and its
// near edge lies between the leading edge and the current limit.
static bool world_probe_cell(void* ctx, orb_cell_kind kind, world_box cell) {
    world_probe* p = ctx;

    if (kind != ORB_CELL_SOLID && (p->drop || kind != world_oneway_kind(p->axis, p->dir)))
        return true;

    int near = p->axis == 0 ? (p->dir > 0 ? cell.left : cell.right)
                            : (p->dir > 0 ? cell.top : cell.bottom);

    if (p->dir > 0 ? near >= p->from && near < p->limit : near <= p->from && near > p->limit)
        p->limit = near;

    return true;
}

// The nearest edge a move along axis in direction dir enters between the box's leading edge
// (from) and its target edge (to). Cells and solids the box already overlaps do not count.
// Returns to when nothing blocks.
static int world_first_blocker(
    const orb_entity* self,
    world_box box,
    int axis,
    int dir,
    int from,
    int to,
    bool drop,
    bool against_solids
) {
    world_probe probe = {axis, dir, from, to, drop};
    world_box band = box;

    if (axis == 0) {
        band.left = dir > 0 ? from : to;
        band.right = dir > 0 ? to : from;
    } else {
        band.top = dir > 0 ? from : to;
        band.bottom = dir > 0 ? to : from;
    }

    world_cells(band, world_probe_cell, &probe);

    if (!against_solids) return probe.limit;

    orb_pool* pool = orb_entity_pool();

    for (uint32_t i = 0; i < pool->solid_count; i++) {
        orb_body* ob;
        const orb_entity* o = world_bodied(pool->solids[i], &ob);

        if (!o || o == self) continue;

        uint16_t oneways = world_oneways(ob);

        if (oneways && (drop || !(oneways & world_oneway_flag(axis, dir)))) continue;

        world_box other = world_body_box(o, ob);
        bool beside = axis == 0 ? other.bottom <= box.top || other.top >= box.bottom
                                : other.right <= box.left || other.left >= box.right;

        if (beside) continue;

        int near =
            axis == 0 ? (dir > 0 ? other.left : other.right) : (dir > 0 ? other.top : other.bottom);

        if (dir > 0 ? near < from || near >= to || near >= probe.limit
                    : near > from || near <= to || near <= probe.limit)
            continue;

        probe.limit = near;
    }

    return probe.limit;
}

// Moves the body along one axis by delta, stopping flush at the first blocker with the
// fraction dropped. A move that would not cross into the next pixel still probes that one
// pixel ahead, so a body flush against a blocker reports it without waiting for a pixel of
// motion. True when blocked.
static bool world_move_axis(
    orb_entity* e,
    orb_body* b,
    int axis,
    float delta,
    bool against_solids
) {
    if (delta == 0) return false;

    float* pos = axis == 0 ? &e->at.x : &e->at.y;
    int box_at = axis == 0 ? b->box.at.x : b->box.at.y;
    int extent = axis == 0 ? b->box.size.width : b->box.size.height;
    int dir = delta > 0 ? 1 : -1, lead = dir > 0 ? extent : 0;
    int from = orb_floor(*pos) + box_at + lead, to = orb_floor(*pos + delta) + box_at + lead;

    if (dir > 0 ? to <= from : to >= from) to = from + dir;

    world_box box = world_body_box(e, b);
    int limit =
        world_first_blocker(e, box, axis, dir, from, to, b->flags & ORB_BODY_DROP, against_solids);

    if (limit == to) {
        *pos += delta;
        return false;
    }

    *pos = (float)(limit - box_at - lead);
    return true;
}

// A solid's own move: gravity, then y and x against cells alone.
static void world_move_solid(orb_entity* e, orb_body* b) {
    b->velocity.x += world_gravity_value.x * b->gravity;
    b->velocity.y += world_gravity_value.y * b->gravity;
    b->impact = (orb_vec2f) {};

    for (int axis = 1; axis >= 0; axis--) {
        float* v = axis ? &b->velocity.y : &b->velocity.x;

        if (!world_move_axis(e, b, axis, *v, false)) continue;

        *(axis ? &b->impact.y : &b->impact.x) = *v;
        *v = 0;
    }
}

// Moves the body out of a full solid that moved into it, along the solid's motion, y then x.
static void world_push(orb_entity* e, orb_body* b, const orb_entity* solid, const orb_body* sb) {
    for (int axis = 1; axis >= 0; axis--) {
        float moved = axis ? sb->moved.y : sb->moved.x;
        world_box box = world_body_box(e, b), other = world_body_box(solid, sb);

        if (moved == 0 || !world_overlaps(box, other)) continue;

        int amount = axis ? (moved > 0 ? other.bottom - box.top : other.top - box.bottom)
                          : (moved > 0 ? other.right - box.left : other.left - box.right);

        world_move_axis(e, b, axis, (float)amount, true);
    }
}

// Moves a rider by its solid's delta, y then x, swept.
static void world_carry(orb_entity* e, orb_body* b, const orb_body* sb) {
    world_move_axis(e, b, 1, sb->moved.y, true);
    world_move_axis(e, b, 0, sb->moved.x, true);
}

// A body's own move: gravity, y then x, the blocked bits and impact, DROP consumed.
static void world_sweep(orb_entity* e, orb_body* b) {
    b->velocity.x += world_gravity_value.x * b->gravity;
    b->velocity.y += world_gravity_value.y * b->gravity;
    b->flags &= (uint16_t)~(
        ORB_BODY_GROUNDED | ORB_BODY_CEILING | ORB_BODY_WALL_LEFT | ORB_BODY_WALL_RIGHT
    );
    b->impact = (orb_vec2f) {};

    for (int axis = 1; axis >= 0; axis--) {
        float* v = axis ? &b->velocity.y : &b->velocity.x;

        if (!world_move_axis(e, b, axis, *v, true)) continue;

        if (axis) {
            b->impact.y = *v;
            b->flags |= *v > 0 ? ORB_BODY_GROUNDED : ORB_BODY_CEILING;
        } else {
            b->impact.x = *v;
            b->flags |= *v > 0 ? ORB_BODY_WALL_RIGHT : ORB_BODY_WALL_LEFT;
        }

        *v = 0;
    }

    b->flags &= (uint16_t)~ORB_BODY_DROP;
}

// The solid whose top touches the body's bottom with x overlap, or ORB_NO_ENTITY.
static orb_entity_id world_support(const orb_entity* e, const orb_body* b) {
    world_box box = world_body_box(e, b);
    orb_pool* pool = orb_entity_pool();

    for (uint32_t i = 0; i < pool->solid_count; i++) {
        orb_body* ob;
        const orb_entity* o = world_bodied(pool->solids[i], &ob);

        if (!o || o == e) continue;

        world_box other = world_body_box(o, ob);

        if (other.top == box.bottom && other.left < box.right && other.right > box.left)
            return o->self;
    }

    return ORB_NO_ENTITY;
}

static bool world_crush_cell(void* ctx, orb_cell_kind kind, world_box cell) {
    (void)cell;

    if (kind != ORB_CELL_SOLID) return true;

    *(bool*)ctx = true;
    return false;
}

// Whether the body overlaps a solid cell or a full solid.
static bool world_crushed(const orb_entity* e, const orb_body* b) {
    world_box box = world_body_box(e, b);
    bool hit = false;

    world_cells(box, world_crush_cell, &hit);

    if (hit) return true;

    orb_pool* pool = orb_entity_pool();

    for (uint32_t i = 0; i < pool->solid_count; i++) {
        orb_body* ob;
        const orb_entity* o = world_bodied(pool->solids[i], &ob);

        if (o && o != e && world_full_solid(ob) && world_overlaps(box, world_body_box(o, ob)))
            return true;
    }

    return false;
}

// The solid a rider follows this tick: its standing_on when that moved, else its carrier.
static const orb_body* world_mover_of(const orb_body* b) {
    orb_entity_id ids[2] = {b->standing_on, b->carrier};

    for (int i = 0; i < 2; i++) {
        const orb_body* sb = orb_entity_component(ids[i], ORB_COMPONENT_BODY);

        if (sb && (sb->flags & ORB_BODY_SOLID) && (sb->moved.x != 0 || sb->moved.y != 0)) return sb;
    }

    return nullptr;
}

void orb_world_collision(const char* layer, const uint8_t kinds[256]) {
    world_layer_id = orb_asset_id(layer, "");
    memcpy(world_kinds, kinds, sizeof world_kinds);
    orb_world_revalidate();
}

void orb_world_gravity(orb_vec2f gravity) {
    world_gravity_value = gravity;
}

// Live solid slots, gathered after the type updates, the last code this tick that can change them.
static void world_gather_solids(orb_pool* pool) {
    pool->solid_count = 0;

    for (uint32_t s = 0; s < pool->max; s++) {
        orb_body* b;

        if (world_bodied(s, &b) && (b->flags & ORB_BODY_SOLID))
            pool->solids[pool->solid_count++] = s;
    }
}

void orb_world_update(void) {
    orb_pool* pool = orb_entity_pool();
    const orb_assets* as = orb_entity_assets();
    void* state = orb_entity_state();
    const orb_api* api = orb_entity_api();
    orb_body* b;
    orb_entity* e;

    orb_entity_updating = true;

    for (uint32_t s = 0; s < pool->max; s++) {
        e = orb_entity_at(s);

        if (!e || e->flags & (ORB_ENTITY_PAUSED | ORB_ENTITY_DESPAWNING | ORB_ENTITY_NEW)) continue;

        const orb_type_fns* fns = orb_entity_type_fns(ORB_HANDLE_INDEX(e->type));

        if (fns->update) fns->update(state, api, e->self);
    }

    world_gather_solids(pool);

    for (uint32_t i = 0; i < pool->solid_count; i++) {
        if (!(e = world_bodied(pool->solids[i], &b))) continue;
        if (world_moves(e)) world_move_solid(e, b);

        orb_vec2f at = orb_entity_world_at(e->self);

        b->moved = (orb_vec2f) {at.x - b->last_at.x, at.y - b->last_at.y};
    }

    for (uint32_t s = 0; s < pool->max; s++) {
        if (!(e = world_bodied(s, &b)) || !world_full_solid(b)) continue;
        if (b->moved.x == 0 && b->moved.y == 0) continue;

        world_box other = world_body_box(e, b);

        for (uint32_t t = 0; t < pool->max; t++) {
            orb_body* tb;
            orb_entity* te = world_bodied(t, &tb);

            if (!te || tb->flags & ORB_BODY_SOLID || !world_moves(te)) continue;
            if (tb->standing_on.v == e->self.v || tb->carrier.v == e->self.v) continue;
            if (world_overlaps(world_body_box(te, tb), other)) world_push(te, tb, e, b);
        }
    }

    for (uint32_t s = 0; s < pool->max; s++) {
        if (!(e = world_bodied(s, &b)) || b->flags & ORB_BODY_SOLID || !world_moves(e)) continue;

        const orb_body* sb = world_mover_of(b);

        if (sb) world_carry(e, b, sb);
    }

    for (uint32_t s = 0; s < pool->max; s++) {
        if (!(e = world_bodied(s, &b)) || b->flags & ORB_BODY_SOLID || !world_moves(e)) continue;

        world_sweep(e, b);
        b->standing_on = world_support(e, b);
    }

    for (uint32_t s = 0; s < pool->max; s++) {
        if (!(e = world_bodied(s, &b))) continue;

        if (!(b->flags & ORB_BODY_SOLID) && !(e->flags & ORB_ENTITY_PAUSED)) {
            if (world_crushed(e, b))
                b->flags |= ORB_BODY_CRUSHED;
            else
                b->flags &= (uint16_t)~ORB_BODY_CRUSHED;
        }

        b->last_at = orb_entity_world_at(e->self);
    }

    for (uint32_t s = 0; s < pool->max; s++) {
        e = orb_entity_at(s);

        if (!e || e->flags & (ORB_ENTITY_PAUSED | ORB_ENTITY_DESPAWNING)) continue;

        orb_sprite_component* sc = orb_entity_component(e->self, ORB_COMPONENT_SPRITE);

        if (sc && sc->anim.anim.v != ORB_NO_ANIM.v) orb_anim_step(as, &sc->anim);
    }

    orb_entity_free_despawning();

    for (uint32_t s = 0; s < pool->max; s++)
        if ((e = orb_entity_at(s))) e->flags &= (uint16_t)~ORB_ENTITY_NEW;

    orb_entity_updating = false;
}

// A query's entity: live with a body, not the exception, and tagged by the mask.
static orb_entity* world_query_match(
    uint32_t slot,
    uint32_t mask,
    orb_entity_id except,
    orb_body** body
) {
    orb_entity* e = world_bodied(slot, body);

    if (!e || e->self.v == except.v) return nullptr;

    const orb_tag* tag = orb_entity_component(e->self, ORB_COMPONENT_TAG);

    return (tag ? tag->bits : 0) & mask ? e : nullptr;
}

int orb_query_rect(
    orb_rect rect,
    uint32_t mask,
    orb_entity_id except,
    orb_entity_id* out,
    int max
) {
    world_box r = {rect.at.x, rect.at.y, rect.at.x + rect.size.width, rect.at.y + rect.size.height};
    orb_pool* pool = orb_entity_pool();
    int n = 0;

    for (uint32_t s = 0; s < pool->max && n < max; s++) {
        orb_body* b;
        const orb_entity* e = world_query_match(s, mask, except, &b);

        if (e && world_overlaps(world_body_box(e, b), r)) out[n++] = e->self;
    }

    return n;
}

int orb_query_circle(
    orb_vec2 center,
    int radius,
    uint32_t mask,
    orb_entity_id except,
    orb_entity_id* out,
    int max
) {
    orb_pool* pool = orb_entity_pool();
    int n = 0;

    for (uint32_t s = 0; s < pool->max && n < max; s++) {
        orb_body* b;
        const orb_entity* e = world_query_match(s, mask, except, &b);

        if (!e) continue;

        world_box box = world_body_box(e, b);
        int dx = center.x < box.left     ? box.left - center.x
                 : center.x >= box.right ? center.x - (box.right - 1)
                                         : 0;
        int dy = center.y < box.top       ? box.top - center.y
                 : center.y >= box.bottom ? center.y - (box.bottom - 1)
                                          : 0;

        if ((int64_t)dx * dx + (int64_t)dy * dy <= (int64_t)radius * radius) out[n++] = e->self;
    }

    return n;
}

int orb_query_point(orb_vec2 at, uint32_t mask, orb_entity_id except, orb_entity_id* out, int max) {
    return orb_query_rect((orb_rect) {at, {1, 1}}, mask, except, out, max);
}

typedef struct world_slab_hit {
    bool hit;
    float t;
    int axis;
} world_slab_hit;

// Where the segment from + t * d enters the box, t in [0, 1], and through which axis. No
// hit when it misses or starts inside.
static world_slab_hit world_slab(orb_vec2 from, orb_vec2 d, world_box box) {
    float t0 = 0, t1 = 1;
    int axis = -1;

    for (int a = 0; a < 2; a++) {
        int o = a ? from.y : from.x, v = a ? d.y : d.x;
        int lo = a ? box.top : box.left, hi = a ? box.bottom : box.right;

        if (v == 0) {
            if (o < lo || o >= hi) return (world_slab_hit) {};

            continue;
        }

        float ta = (float)(lo - o) / (float)v, tb = (float)(hi - o) / (float)v;

        if (ta > tb) {
            float swap = ta;

            ta = tb;
            tb = swap;
        }

        if (ta >= t0) {
            t0 = ta;
            axis = a;
        }

        if (tb < t1) t1 = tb;
        if (t0 > t1) return (world_slab_hit) {};
    }

    if (axis < 0) return (world_slab_hit) {};

    return (world_slab_hit) {.hit = true, .t = t0, .axis = axis};
}

bool orb_query_ray(
    orb_vec2 from,
    orb_vec2 to,
    uint32_t mask,
    uint32_t flags,
    orb_entity_id except,
    orb_hit* hit
) {
    orb_vec2 d = {to.x - from.x, to.y - from.y};
    orb_hit best = {.fraction = 2};
    orb_pool* pool = orb_entity_pool();

    for (uint32_t s = 0; s < pool->max; s++) {
        orb_body* b;
        const orb_entity* e = world_bodied(s, &b);

        if (!e || e->self.v == except.v) continue;

        const orb_tag* tag = orb_entity_component(e->self, ORB_COMPONENT_TAG);
        bool tagged = ((tag ? tag->bits : 0) & mask) != 0;
        bool solid = (flags & ORB_RAY_SOLIDS) && (b->flags & ORB_BODY_SOLID);

        if (!tagged && !solid) continue;

        world_box box = world_body_box(e, b);

        if (from.x >= box.left && from.x < box.right && from.y >= box.top && from.y < box.bottom)
            continue;

        world_slab_hit slab = world_slab(from, d, box);

        if (!slab.hit) continue;

        int dir = (slab.axis ? d.y : d.x) > 0 ? 1 : -1;
        uint16_t oneways = world_oneways(b);

        if (oneways && (b->flags & ORB_BODY_SOLID) &&
            (!(flags & ORB_RAY_ONEWAY) || !(oneways & world_oneway_flag(slab.axis, dir))))
            continue;

        if (slab.t >= best.fraction) continue;

        best = (orb_hit) {
            .entity = e->self,
            .at =
                {from.x + orb_floor(slab.t * (float)d.x), from.y + orb_floor(slab.t * (float)d.y)},
            .normal = slab.axis ? (orb_vec2) {0, -dir} : (orb_vec2) {-dir, 0},
            .fraction = slab.t
        };
    }

    if (flags & ORB_RAY_CELLS) {
        int ax = abs(d.x), ay = abs(d.y), sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
        int steps = ax + ay, x = from.x, y = from.y, ix = 0, iy = 0;

        for (int i = 1; i <= steps; i++) {
            // step the axis whose next pixel center the ray reaches first
            int axis = ay == 0 || (ax != 0 && (2 * ix + 1) * ay < (2 * iy + 1) * ax) ? 0 : 1;

            if (axis == 0) {
                x += sx;
                ix++;
            } else {
                y += sy;
                iy++;
            }

            orb_cell_kind kind = orb_world_cell_kind((orb_vec2) {x, y});

            if (kind == ORB_CELL_OPEN) continue;

            int dir = axis ? sy : sx;

            if (kind != ORB_CELL_SOLID &&
                (!(flags & ORB_RAY_ONEWAY) || kind != world_oneway_kind(axis, dir)))
                continue;

            float t = (float)i / (float)steps;

            if (t < best.fraction)
                best = (orb_hit) {
                    .entity = ORB_NO_ENTITY,
                    .at = {axis ? x : x - sx, axis ? y - sy : y},
                    .normal = axis ? (orb_vec2) {0, -dir} : (orb_vec2) {-dir, 0},
                    .fraction = t
                };

            break;
        }
    }

    if (best.fraction > 1) return false;

    *hit = best;
    return true;
}

static int world_sort_compare(const void* a, const void* b) {
    const orb_sort_entry *x = a, *y = b;

    if (x->key != y->key) return x->key < y->key ? -1 : 1;

    return x->slot < y->slot ? -1 : x->slot > y->slot;
}

void orb_world_remap_set(int index, const uint8_t table[256]) {
    if (index < 0 || index >= ORB_MAX_REMAPS) {
        orb_log("remap_set: index %d is not 0 to %d", index, ORB_MAX_REMAPS - 1);
        return;
    }

    memcpy(world_remaps[index], table, 256);
    world_remap_live[index] = true;
}

void orb_world_draw(orb_fb* fb, orb_vec2f cam, int layer) {
    orb_pool* pool = orb_entity_pool();
    const orb_assets* as = orb_entity_assets();
    int n = 0;

    for (uint32_t s = 0; s < pool->max; s++) {
        const orb_entity* e = orb_entity_at(s);

        if (!e || !(e->flags & ORB_ENTITY_VISIBLE) || e->flags & ORB_ENTITY_DESPAWNING) continue;

        const orb_sprite_component* sc = orb_entity_component(e->self, ORB_COMPONENT_SPRITE);

        if (!sc || sc->layer != layer) continue;

        orb_vec2f at = orb_entity_world_at(e->self);

        pool->sort[n++] = (orb_sort_entry) {orb_floor(at.y) + e->size.height + sc->sort_bias, s};
    }

    qsort(pool->sort, (size_t)n, sizeof *pool->sort, world_sort_compare);

    for (int i = 0; i < n; i++) {
        const orb_entity* e = &pool->entities[pool->sort[i].slot];
        const orb_sprite_component* sc = orb_entity_component(e->self, ORB_COMPONENT_SPRITE);
        orb_vec2f at = orb_entity_world_at(e->self);
        orb_sprite sprite =
            sc->anim.anim.v == ORB_NO_ANIM.v ? sc->sprite : orb_anim_frame(as, &sc->anim);
        bool remapped = sc->remap >= 0 && sc->remap < ORB_MAX_REMAPS && world_remap_live[sc->remap];
        orb_vec2 screen = {orb_floor(at.x) + sc->offset.x, orb_floor(at.y) + sc->offset.y};

        orb_sprite_draw(
            fb, as, cam, sprite, screen, sc->flags, remapped ? world_remaps[sc->remap] : nullptr
        );
    }
}

static void world_plot(uint32_t* rgb, orb_size size, int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < size.width && y < size.height) rgb[y * size.width + x] = color;
}

void orb_world_debug_draw(uint32_t* rgb, orb_size size, orb_vec2f cam) {
    if (!orb_entity_debug) return;

    uint32_t dark = 0, bright = 0;
    orb_pool* pool = orb_entity_pool();

    orb_pal_extremes(orb_api_pal_base(), &dark, &bright);

    for (uint32_t s = 0; s < pool->max; s++) {
        orb_body* b;
        const orb_entity* e = world_bodied(s, &b);

        if (!e) continue;

        world_box box = world_body_box(e, b);
        uint32_t color = b->flags & ORB_BODY_SOLID ? dark : bright;
        int cx = orb_floor(cam.x), cy = orb_floor(cam.y);

        for (int x = box.left; x < box.right; x++) {
            world_plot(rgb, size, x - cx, box.top - cy, color);
            world_plot(rgb, size, x - cx, box.bottom - 1 - cy, color);
        }

        for (int y = box.top; y < box.bottom; y++) {
            world_plot(rgb, size, box.left - cx, y - cy, color);
            world_plot(rgb, size, box.right - 1 - cx, y - cy, color);
        }
    }
}

#include "arena.h"
#include "../orb_math.h"
#include "../os/os.h"
#include "log.h"

// From the page holding the committed end through end, rounded up to a commit
// step from the arena's base and then to a page, clamped to the arena's last
// page. A child starts mid-page, and committing a page twice is harmless.
static bool arena_grow(arena* region, usize end) {
    usize base = (usize)region->base;
    usize from = (base + region->committed) & ~(ORB_PAGE - 1);
    usize limit = orb_round_up(base + region->size, ORB_PAGE);
    usize to = orb_round_up(base + orb_round_up(end, ORB_COMMIT_STEP), ORB_PAGE);

    if (to > limit) to = limit;

    if (!orb_os_commit((void*)from, to - from)) {
        region->overflow = to - from;
        region->refused = true;
        return false;
    }

    region->committed = orb_min(to - base, region->size);
    return true;
}

static void arena_failed(arena* region) {
    if (region->logged) return;

    orb_error error;

    orb_arena_error(region, &error);
    orb_log("%s", error.text);
    region->logged = true;
}

static void arena_high(arena* region) {
    region->warned = true;
    orb_log(
        "'%s' at %u%% of %s", region->name, (unsigned)(region->peak * 100 / region->size),
        orb_bytes_format(region->size).text
    );
}

#ifndef ORB_RELEASE
static arena* arena_recorded[ORB_MAX_ARENAS];
static int arena_recorded_count;
static bool arena_full_logged;
static const arena *arena_state_home, *arena_global_home, *arena_frame_home;

static bool arena_holds(const arena* region, const void* at) {
    return region && region->base && (const u8*)at >= region->base &&
           (const u8*)at < region->base + region->size;
}

static void arena_made(arena* region) {
    bool home = arena_holds(arena_state_home, region) || arena_holds(arena_global_home, region);

    if (!home || arena_holds(arena_frame_home, region->base)) return;

    for (int i = 0; i < arena_recorded_count; i++)
        if (arena_recorded[i] == region) return;

    if (arena_recorded_count == ORB_MAX_ARENAS) {
        if (!arena_full_logged) orb_log("memory: %d arenas recorded, the most", ORB_MAX_ARENAS);

        arena_full_logged = true;
        return;
    }

    arena_recorded[arena_recorded_count++] = region;
}

// A clear drops every recorded arena whose memory lay in the cleared one.
static void arena_cleared(arena* region) {
    for (int i = 0; i < arena_recorded_count;)
        if (arena_recorded[i] != region && (arena_holds(region, arena_recorded[i]) ||
                                            arena_holds(region, arena_recorded[i]->base)))
            arena_recorded[i] = arena_recorded[--arena_recorded_count];
        else
            i++;
}

void orb_arena_homes(const arena* state, const arena* global, const arena* frame) {
    arena_state_home = state;
    arena_global_home = global;
    arena_frame_home = frame;
    arena_recorded_count = 0;
    arena_full_logged = false;
}

const arena* orb_arena_recorded(int index) {
    return index >= 0 && index < arena_recorded_count ? arena_recorded[index] : nullptr;
}
#else
static void arena_made(arena*) {
}

static void arena_cleared(arena*) {
}
#endif

const arena_hooks orb_arena_hooks = {
    .grow = arena_grow,
    .failed = arena_failed,
    .made = arena_made,
    .cleared = arena_cleared,
    .high = arena_high,
};

void orb_arena_init(arena* region, const char* name, void* mem, usize size) {
    *region = (arena) {.base = mem, .size = size, .committed = size};
    snprintf(region->name, sizeof region->name, "%s", name);
}

bool orb_arena_reserve(arena* region, const char* name, usize size) {
    size = orb_round_up(size, ORB_COMMIT_STEP);

    void* mem = orb_os_reserve(size);

    if (!mem) return false;

    *region = (arena) {.base = mem, .size = size, .reserved = size, .hooks = &orb_arena_hooks};
    snprintf(region->name, sizeof region->name, "%s", name);
    return true;
}

void orb_arena_release(arena* region) {
    if (region->base) orb_os_release(region->base, region->reserved);

    *region = (arena) {};
}

bool orb_arena_error(const arena* region, orb_error* err) {
    if (region->refused)
        return orb_error_set(
            err, "'%s' cannot commit %s", region->name, orb_bytes_format(region->overflow).text
        );

    return orb_error_set(
        err, "'%s' exhausted: %s over its %s size", region->name,
        orb_bytes_format(region->overflow).text, orb_bytes_format(region->size).text
    );
}

// Unwind to the recovering caller, or end the process naming the region.
[[noreturn]] static void arena_fail(arena* region, usize bytes, bool refused) {
    region->overflow = bytes;
    region->refused = refused;

    if (region->recover) longjmp(*region->recover, 1);

    orb_error error;

    orb_arena_error(region, &error);
    orb_fatal("region %s", error.text);
}

void* orb_arena_push(arena* region, usize size, usize align) {
    usize start = orb_round_up(region->used, align);
    usize end;
    bool overflowed = ckd_add(&end, start, size);

    if (overflowed || end > region->size)
        arena_fail(region, overflowed ? size : end - region->size, false);

    if (end > region->committed && !(region->hooks && region->hooks->grow(region, end)))
        arena_fail(region, region->hooks ? region->overflow : end - region->committed, true);

    region->used = end;

    if (region->used > region->peak) region->peak = region->used;

    memset(region->base + start, 0, size);
    return region->base + start;
}

// A count-times-size multiply that overflows fails through orb_arena_push, the
// same as a push too large for the arena.
void* orb_arena_push_checked(arena* region, usize elem, usize count, usize align) {
    usize size;

    if (ckd_mul(&size, elem, count)) size = SIZE_MAX;

    return orb_arena_push(region, size, align);
}

// In release alloc has already set failed on the arena.
void* orb_arena_list_take(
    arena* out,
    [[maybe_unused]] const char* call,
    usize size,
    usize count,
    usize align
) {
    void* elems = std__alloc(out, size, count, align);
#ifndef ORB_RELEASE
    if (!elems) std_trapf("%s: '%s' cannot fit the result", call, out->name);
#endif
    return elems;
}

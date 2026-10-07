#include "arena.h"
#include "../orb_math.h"
#include "../os/os.h"
#include "log.h"

#include <stdckdint.h>
#include <stdio.h>
#include <string.h>

// A test defines orb_arena_trap before including orb to catch the trap.
#ifndef orb_arena_trap
#define orb_arena_trap(...) orb_fatal(__VA_ARGS__)
#endif

// Every T_list has u8_list's layout, so list_resize copies a list in and out as one.
orb_list(u8);

static jmp_buf* arena_recover;

// Records a failed allocation and returns false.
static bool arena_short(orb_arena* region, usize overflow, bool refused) {
    region->overflow = overflow;
    region->refused = refused;
    region->failed = true;
    return false;
}

// From the page holding the committed end through end, rounded up to a commit
// step from the arena's base and then to a page, clamped to the arena's last
// page. A child starts mid-page, and committing a page twice is harmless.
static bool arena_grow(orb_arena* region, usize end) {
    usize base = (usize)region->base;
    usize from = (base + region->committed) & ~(ORB_PAGE - 1);
    usize limit = orb_round_up(base + region->size, ORB_PAGE);
    usize to = orb_round_up(base + orb_round_up(end, ORB_COMMIT_STEP), ORB_PAGE);

    if (to > limit) to = limit;
    if (!orb_os_commit((void*)from, to - from)) return arena_short(region, to - from, true);

    region->committed = orb_min(to - base, region->size);
    return true;
}

// size bytes at align past used: true with *start their offset from base.
static bool arena_take(orb_arena* region, usize size, usize align, bool commit, usize* start) {
    usize from = orb_round_up(region->used, align), end;

    if (from < region->used || ckd_add(&end, from, size)) return arena_short(region, size, false);
    if (end > region->size) return arena_short(region, end - region->size, false);
    if (commit && end > region->committed && !arena_grow(region, end)) return false;

    region->used = end;

    if (end > region->peak) region->peak = end;

    *start = from;
    return true;
}

bool orb_arena_error(const orb_arena* region, orb_error* err) {
    if (region->refused)
        return orb_error_set(
            err, "'%s' cannot commit %s", region->name, orb_bytes_format(region->overflow).text
        );

    return orb_error_set(
        err, "'%s' exhausted: %s over its %s size", region->name,
        orb_bytes_format(region->overflow).text, orb_bytes_format(region->size).text
    );
}

// A game call's failure, logged the first time its arena fails.
static void arena_failed(orb_arena* region) {
    if (region->logged) return;

    orb_error error;

    orb_arena_error(region, &error);
    orb_log("%s", error.text);
    region->logged = true;
}

// Logs the first time a game call raises the arena's peak past 90% of its size.
static void arena_high(orb_arena* region, usize peak) {
    if (region->warned || region->peak == peak || region->peak <= region->size - region->size / 10)
        return;

    region->warned = true;
    orb_log(
        "'%s' at %u%% of %s", region->name, (unsigned)(region->peak * 100 / region->size),
        orb_bytes_format(region->size).text
    );
}

#ifndef ORB_RELEASE
static orb_arena* arena_recorded[ORB_MAX_ARENAS];
static int arena_recorded_count;
static bool arena_full_logged;
static const orb_arena *arena_state_home, *arena_global_home;

static bool arena_holds(const orb_arena* region, const void* at) {
    return region && region->base && (const u8*)at >= region->base &&
           (const u8*)at < region->base + region->size;
}

static void arena_made(orb_arena* region) {
    if (!arena_holds(arena_state_home, region) && !arena_holds(arena_global_home, region)) return;

    for (int i = 0; i < arena_recorded_count; i++)
        if (arena_recorded[i] == region) return;

    if (arena_recorded_count == ORB_MAX_ARENAS) {
        if (!arena_full_logged) orb_log("memory: %d arenas recorded, the most", ORB_MAX_ARENAS);

        arena_full_logged = true;
        return;
    }

    arena_recorded[arena_recorded_count++] = region;
}

// A clear drops every recorded arena whose struct or memory lay in the cleared one.
static void arena_cleared(orb_arena* region) {
    for (int i = 0; i < arena_recorded_count;)
        if (arena_recorded[i] != region && (arena_holds(region, arena_recorded[i]) ||
                                            arena_holds(region, arena_recorded[i]->base)))
            arena_recorded[i] = arena_recorded[--arena_recorded_count];
        else
            i++;
}

void orb_arena_homes(const orb_arena* state, const orb_arena* global) {
    arena_state_home = state;
    arena_global_home = global;
    arena_recorded_count = 0;
    arena_full_logged = false;
}

const orb_arena* orb_arena_recorded(int index) {
    return index >= 0 && index < arena_recorded_count ? arena_recorded[index] : nullptr;
}
#else
static void arena_made(orb_arena*) {
}

static void arena_cleared(orb_arena*) {
}
#endif

void orb_arena_init(orb_arena* region, const char* name, void* mem, usize size) {
    *region = (orb_arena) {.base = mem, .size = size, .committed = size};
    snprintf(region->name, sizeof region->name, "%s", name);
}

bool orb_arena_reserve(orb_arena* region, const char* name, usize size) {
    size = orb_round_up(size, ORB_COMMIT_STEP);

    void* mem = orb_os_reserve(size);

    if (!mem) return false;

    *region = (orb_arena) {.base = mem, .size = size, .reserved = size};
    snprintf(region->name, sizeof region->name, "%s", name);
    return true;
}

void orb_arena_release(orb_arena* region) {
    if (region->base) orb_os_release(region->base, region->reserved);

    *region = (orb_arena) {};
}

bool orb_arena_new(orb_arena* out, orb_arena* parent, const char* name, usize size) {
    usize peak = parent->peak, start;
    bool fits = arena_take(parent, size, alignof(max_align_t), false, &start);

    *out = (orb_arena) {};
    snprintf(out->name, sizeof out->name, "%s", name);

    if (!fits) {
        arena_failed(parent);
        return false;
    }

    arena_high(parent, peak);
    out->base = parent->base ? parent->base + start : nullptr;
    out->size = size;
    out->committed = parent->committed > start ? orb_min(parent->committed - start, size) : 0;
    arena_made(out);
    return true;
}

void orb_arena_clear(orb_arena* region) {
    region->used = 0;
    region->failed = false;
    arena_cleared(region);
}

void* orb_arena_alloc(orb_arena* region, usize count, usize size) {
    usize bytes, peak = region->peak, start;

    if (ckd_mul(&bytes, count, size)) {
        arena_short(region, SIZE_MAX, false);
        arena_failed(region);
        return nullptr;
    }

    if (bytes == 0) return region->base;

    if (!arena_take(region, bytes, alignof(max_align_t), true, &start)) {
        arena_failed(region);
        return nullptr;
    }

    arena_high(region, peak);
    memset(region->base + start, 0, bytes);
    return region->base + start;
}

bool orb_arena_list_resize(orb_arena* region, void* list, u32 cap, usize size) {
    u8_list view;
    usize bytes, peak = region->peak, start;

    memcpy(&view, list, sizeof view);

    if (cap < view.len) return false;

    if (ckd_mul(&bytes, (usize)cap, size)) {
        arena_short(region, SIZE_MAX, false);
        arena_failed(region);
        return false;
    }

    usize old = (usize)view.cap * size;
    uintptr_t at = (uintptr_t)view.elems, base = (uintptr_t)region->base;
    bool top = view.elems && region->base && at >= base && at + old == base + region->used;

    if (top && bytes <= old)
        region->used -= old - bytes;
    else if (top) {
        if (!arena_take(region, bytes - old, 1, true, &start)) {
            arena_failed(region);
            return false;
        }
    } else if (bytes > old) {
        if (!arena_take(region, bytes, alignof(max_align_t), true, &start)) {
            arena_failed(region);
            return false;
        }

        u8* moved = region->base + start;

        if (view.len) memmove(moved, view.elems, (usize)view.len * size);

        view.elems = moved;
    }

    arena_high(region, peak);
    view.cap = cap;
    memcpy(list, &view, sizeof view);
    return true;
}

void orb_arena_recover(jmp_buf* at) {
    arena_recover = at;
}

// Unwind to the recovery point, or end the process naming the region.
[[noreturn]] static void arena_fail(orb_arena* region) {
    if (arena_recover) longjmp(*arena_recover, 1);

    orb_error error;

    orb_arena_error(region, &error);
    orb_fatal("region %s", error.text);
}

void* orb_arena_push(orb_arena* region, usize size, usize align) {
    usize start;

    if (!arena_take(region, size, align, true, &start)) arena_fail(region);

    memset(region->base + start, 0, size);
    return region->base + start;
}

// A count-times-size multiply that overflows fails through orb_arena_push, the
// same as a push too large for the arena.
void* orb_arena_push_checked(orb_arena* region, usize elem, usize count, usize align) {
    usize size;

    if (ckd_mul(&size, elem, count)) size = SIZE_MAX;

    return orb_arena_push(region, size, align);
}

// In release alloc has already set failed on the arena.
void* orb_arena_list_take(
    orb_arena* out,
    [[maybe_unused]] const char* call,
    usize count,
    usize size
) {
    void* elems = orb_arena_alloc(out, count, size);
#ifndef ORB_RELEASE
    if (!elems) orb_arena_trap("%s: '%s' cannot fit the result", call, out->name);
#endif
    return elems;
}

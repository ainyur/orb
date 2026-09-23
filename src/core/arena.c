#include "arena.h"
#include "../os/os.h"
#include "log.h"

#include <stdckdint.h>
#include <string.h>

static size_t arena_round(size_t n, size_t to) {
    return (n + to - 1) & ~(to - 1);
}

void orb_arena_init(orb_arena* arena, const char* name, void* mem, size_t size) {
    *arena = (orb_arena) {.name = name, .base = mem, .size = size, .committed = size};
}

bool orb_arena_reserve(orb_arena* arena, const char* name, size_t size) {
    size = arena_round(size, ORB_COMMIT_STEP);

    void* mem = orb_os_reserve(size);

    if (!mem) return false;

    *arena = (orb_arena) {.name = name, .base = mem, .size = size};
    return true;
}

void orb_arena_release(orb_arena* arena) {
    if (arena->base) orb_os_release(arena->base, arena->size);

    *arena = (orb_arena) {};
}

orb_arena orb_arena_carve(orb_arena* parent, const char* name, size_t size) {
    orb_arena child;

    orb_arena_init(&child, name, orb_arena_push(parent, size, 16), size);
    return child;
}

bool orb_arena_error(const orb_arena* arena, orb_error* err) {
    if (arena->refused)
        return orb_error_set(
            err, "'%s' cannot commit %s", arena->name, orb_bytes_format(arena->overflow).text
        );

    return orb_error_set(
        err, "'%s' exhausted: %s over its %s size", arena->name,
        orb_bytes_format(arena->overflow).text, orb_bytes_format(arena->size).text
    );
}

// Unwind to the recovering caller, or end the process naming the region.
[[noreturn]] static void arena_fail(orb_arena* arena, size_t bytes, bool refused) {
    arena->overflow = bytes;
    arena->refused = refused;

    if (arena->recover) longjmp(*arena->recover, 1);

    orb_error error;

    orb_arena_error(arena, &error);
    orb_fatal("region %s", error.text);
}

void* orb_arena_push(orb_arena* arena, size_t size, size_t align) {
    size_t start = arena_round(arena->used, align);
    size_t end;
    bool overflowed = ckd_add(&end, start, size);

    if (overflowed || end > arena->size)
        arena_fail(arena, overflowed ? size : end - arena->size, false);

    if (end > arena->committed) {
        size_t to = arena_round(end, ORB_COMMIT_STEP);

        if (!orb_os_commit(arena->base + arena->committed, to - arena->committed))
            arena_fail(arena, to - arena->committed, true);

        arena->committed = to;
    }

    arena->used = end;

    if (arena->used > arena->peak) arena->peak = arena->used;

    memset(arena->base + start, 0, size);
    return arena->base + start;
}

// A count-times-size multiply that overflows fails through orb_arena_push, the
// same as a push too large for the arena.
void* orb_arena_push_checked(orb_arena* arena, size_t elem, size_t count, size_t align) {
    size_t size;

    if (ckd_mul(&size, elem, count)) size = SIZE_MAX;

    return orb_arena_push(arena, size, align);
}

void orb_arena_reset(orb_arena* arena) {
    arena->used = 0;
}

#include "arena.h"
#include "../os/os.h"
#include "log.h"

#include <stdckdint.h>
#include <string.h>

static size_t arena_round(size_t n, size_t to) {
    return (n + to - 1) & ~(to - 1);
}

void orb_arena_init(orb_arena* a, const char* name, void* mem, size_t size) {
    *a = (orb_arena) {.name = name, .base = mem, .size = size, .committed = size};
}

bool orb_arena_reserve(orb_arena* a, const char* name, size_t size) {
    size = arena_round(size, ORB_COMMIT_STEP);

    void* mem = orb_os_reserve(size);

    if (!mem) return false;

    *a = (orb_arena) {.name = name, .base = mem, .size = size};
    return true;
}

void orb_arena_release(orb_arena* a) {
    if (a->base) orb_os_release(a->base, a->size);

    *a = (orb_arena) {};
}

orb_arena orb_arena_carve(orb_arena* parent, const char* name, size_t size) {
    orb_arena child;

    orb_arena_init(&child, name, orb_arena_push(parent, size, 16), size);
    return child;
}

bool orb_arena_error(const orb_arena* a, orb_error* err) {
    if (a->refused)
        return orb_error_set(
            err, "'%s' cannot commit %s", a->name, orb_bytes_format(a->overflow).text
        );

    return orb_error_set(
        err, "'%s' exhausted: %s over its %s size", a->name, orb_bytes_format(a->overflow).text,
        orb_bytes_format(a->size).text
    );
}

// Unwind to the recovering caller, or end the process naming the region.
[[noreturn]] static void arena_fail(orb_arena* a, size_t bytes, bool refused) {
    a->overflow = bytes;
    a->refused = refused;

    if (a->recover) longjmp(*a->recover, 1);

    orb_error e;

    orb_arena_error(a, &e);
    orb_fatal("region %s", e.text);
}

void* orb_arena_push(orb_arena* a, size_t size, size_t align) {
    size_t start = arena_round(a->used, align);
    size_t end;
    bool overflowed = ckd_add(&end, start, size);

    if (overflowed || end > a->size) arena_fail(a, overflowed ? size : end - a->size, false);

    if (end > a->committed) {
        size_t to = arena_round(end, ORB_COMMIT_STEP);

        if (!orb_os_commit(a->base + a->committed, to - a->committed))
            arena_fail(a, to - a->committed, true);

        a->committed = to;
    }

    a->used = end;

    if (a->used > a->peak) a->peak = a->used;

    memset(a->base + start, 0, size);
    return a->base + start;
}

// A count-times-size multiply that overflows fails through orb_arena_push, the
// same as a push too large for the arena.
void* orb_arena_push_checked(orb_arena* a, size_t elem, size_t count, size_t align) {
    size_t size;

    if (ckd_mul(&size, elem, count)) size = SIZE_MAX;

    return orb_arena_push(a, size, align);
}

void orb_arena_reset(orb_arena* a) {
    a->used = 0;
}

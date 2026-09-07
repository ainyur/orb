#include "arena.h"
#include "log.h"

#include <string.h>

void orb_arena_init(orb_arena* a, const char* name, void* mem, size_t size) {
    a->name = name;
    a->base = mem;
    a->size = size;
    a->used = 0;
    a->recover = nullptr;
    a->overflow = 0;
}

void* orb_arena_push(orb_arena* a, size_t size, size_t align) {
    size_t start = (a->used + align - 1) & ~(align - 1);

    if (start + size > a->size) {
        if (a->recover) {
            a->overflow = start + size - a->size;
            longjmp(*a->recover, 1);
        }

        orb_fatal(
            "region '%s' exhausted: %zu bytes over its %zu byte size", a->name,
            start + size - a->size, a->size
        );
    }

    a->used = start + size;
    memset(a->base + start, 0, size);
    return a->base + start;
}

void orb_arena_reset(orb_arena* a) {
    a->used = 0;
}

orb_arena orb_arena_carve(orb_arena* parent, const char* name, size_t size) {
    orb_arena child;

    orb_arena_init(&child, name, orb_arena_push(parent, size, 16), size);
    return child;
}

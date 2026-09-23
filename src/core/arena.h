#pragma once

#include "log.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t ORB_COMMIT_STEP = (size_t)1 << 20;
constexpr size_t ORB_REGION_RESERVE = (size_t)1 << 30;

typedef struct orb_arena {
    const char* name;
    uint8_t* base;
    size_t size;      // fixed: the memory's length; reserved: the address space held
    size_t committed; // bytes from base backed by memory; size for a fixed arena
    size_t used;      // a caller may save and restore it to drop what it pushed since
    size_t peak;      // the most used has been
    jmp_buf* recover; // if set, exhaustion longjmps here instead of being fatal
    size_t overflow;  // bytes the failed push was short by, or could not commit
    bool refused;     // the failed push was refused a commit rather than out of size
} orb_arena;

void orb_arena_init(orb_arena* a, const char* name, void* mem, size_t size);
bool orb_arena_reserve(orb_arena* a, const char* name, size_t size); // false when refused
void orb_arena_release(orb_arena* a);                                // reserved arenas only
orb_arena orb_arena_carve(orb_arena* parent, const char* name, size_t size);

void* orb_arena_push(orb_arena* a, size_t size, size_t align);
void* orb_arena_push_checked(orb_arena* a, size_t elem, size_t count, size_t align);

#define orb_arena_push_array(a, T, count)                                                          \
    ((T*)orb_arena_push_checked((a), sizeof(T), (size_t)(count), alignof(T)))

void orb_arena_reset(orb_arena* a);

// The failed push as text, "'<name>' exhausted: 3.2 MB over its 1.0 GB size" or
// "'<name>' cannot commit 1.0 MB". Returns false, as orb_error_set does.
bool orb_arena_error(const orb_arena* a, orb_error* err);

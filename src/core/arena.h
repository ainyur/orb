#pragma once
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

typedef struct orb_arena {
    const char* name;
    uint8_t* base;
    size_t size;
    size_t used;
    jmp_buf* recover; // if set, exhaustion longjmps here instead of being fatal
    size_t overflow;  // bytes the failed push was short by, for the recovering caller
} orb_arena;

void orb_arena_init(orb_arena* a, const char* name, void* mem, size_t size);
void* orb_arena_push(orb_arena* a, size_t size, size_t align);
void orb_arena_reset(orb_arena* a);
orb_arena orb_arena_carve(orb_arena* parent, const char* name, size_t size);

// Push count objects of type T, aligned for T. Returns T*.
#define orb_arena_push_array(a, T, count)                                                          \
    ((T*)orb_arena_push((a), sizeof(T) * (size_t)(count), alignof(T)))

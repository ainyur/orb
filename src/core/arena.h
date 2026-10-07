#pragma once

#include "../orb.h"
#include "log.h"

#include <setjmp.h>

constexpr usize ORB_COMMIT_STEP = (usize)1 << 20;
constexpr usize ORB_REGION_RESERVE = (usize)1 << 30;
constexpr usize ORB_PAGE = 4096;
constexpr int ORB_MAX_ARENAS = 64;

void orb_arena_init(orb_arena* region, const char* name, void* mem, usize size); // fully committed
bool orb_arena_reserve(orb_arena* region, const char* name, usize size); // false when refused
void orb_arena_release(orb_arena* region);                               // reserved arenas only

// orb_api's arena_new, arena_clear, alloc, and list_resize (orb.h).
bool orb_arena_new(orb_arena* out, orb_arena* parent, const char* name, usize size);
void orb_arena_clear(orb_arena* region);
void* orb_arena_alloc(orb_arena* region, usize count, usize size);
bool orb_arena_list_resize(orb_arena* region, void* list, u32 cap, usize size);

// While at is set, a failed orb_arena_push longjmps there; nullptr disarms.
void orb_arena_recover(jmp_buf* at);

// Exhaustion longjmps to the recovery point when one is set and is otherwise fatal.
void* orb_arena_push(orb_arena* region, usize size, usize align);
void* orb_arena_push_checked(orb_arena* region, usize elem, usize count, usize align);

#define orb_arena_push_array(region, T, count)                                                     \
    ((T*)orb_arena_push_checked((region), sizeof(T), (usize)(count), alignof(T)))

// The failed allocation as text, "'<name>' exhausted: 3.2 MB over its 1.0 GB size" or
// "'<name>' cannot commit 1.0 MB". Returns false, as orb_error_set does.
bool orb_arena_error(const orb_arena* region, orb_error* err);

// Space for a list result, or nullptr when out cannot hold it: traps in debug; in release the
// caller returns an empty list.
void* orb_arena_list_take(orb_arena* out, const char* call, usize count, usize size);

#define orb_arena_list_alloc(out, call, T, count)                                                  \
    ((T*)orb_arena_list_take((out), (call), (count), sizeof(T)))

#ifndef ORB_RELEASE
// The memory command lists game arenas: those whose struct lies in state or global.
void orb_arena_homes(const orb_arena* state, const orb_arena* global);
const orb_arena* orb_arena_recorded(int index); // nullptr past the last
#endif

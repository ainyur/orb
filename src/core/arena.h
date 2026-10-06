#pragma once

#include "../orb.h"
#include "log.h"

constexpr usize ORB_COMMIT_STEP = (usize)1 << 20;
constexpr usize ORB_REGION_RESERVE = (usize)1 << 30;
constexpr usize ORB_PAGE = 4096;
constexpr int ORB_MAX_ARENAS = 64;

extern const arena_hooks orb_arena_hooks;

void orb_arena_init(arena* region, const char* name, void* mem, usize size); // fixed, no hooks
bool orb_arena_reserve(arena* region, const char* name, usize size);         // false when refused
void orb_arena_release(arena* region);                                       // reserved arenas only

// Exhaustion longjmps to recover when set and is otherwise fatal.
void* orb_arena_push(arena* region, usize size, usize align);
void* orb_arena_push_checked(arena* region, usize elem, usize count, usize align);

#define orb_arena_push_array(region, T, count)                                                     \
    ((T*)orb_arena_push_checked((region), sizeof(T), (usize)(count), alignof(T)))

// The failed allocation as text, "'<name>' exhausted: 3.2 MB over its 1.0 GB size" or
// "'<name>' cannot commit 1.0 MB". Returns false, as orb_error_set does.
bool orb_arena_error(const arena* region, orb_error* err);

// Space for a list result, or nullptr when out cannot hold it: traps in debug; in release the
// caller returns an empty list.
void* orb_arena_list_take(arena* out, const char* call, usize size, usize count, usize align);

#define orb_arena_list_alloc(out, call, T, count)                                                  \
    ((T*)orb_arena_list_take((out), (call), sizeof(T), (count), alignof(T)))

#ifndef ORB_RELEASE
// The memory command lists game arenas: those whose struct lies in state or
// global and whose memory does not lie in frame.
void orb_arena_homes(const arena* state, const arena* global, const arena* frame);
const arena* orb_arena_recorded(int index); // nullptr past the last
#endif

#pragma once

// Bare names and std_ names are for games to use; std__ names are this header's
// internals.

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef ptrdiff_t isize;
typedef size_t usize;
typedef float f32;
typedef double f64;

constexpr usize KB = 1024;
constexpr usize MB = 1024 * KB;

// void std_trap(const char* message): writes the message to stderr and aborts. A
// file that defines std_trap before including this header catches traps itself.
// ORB_RELEASE compiles the checks out.
#ifndef std_trap
#define std_trap(message) (fprintf(stderr, "%s\n", (message)), abort())
#endif

// void std_trapf(const char* format, ...): std_trap with a formatted message
[[gnu::format(gnu_printf, 1, 2)]] static inline void std_trapf(const char* format, ...) {
    static char text[128];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    std_trap(text);
}

// Converting to usize first makes a negative index trap rather than wrap.
#ifdef ORB_RELEASE
#define std__index(i, len, op) ((usize)(i))
#else
static inline usize std__index(usize i, u32 len, const char* op) {
    if (i >= len) std_trapf("%s: index %zu past len %u", op, i, len);

    return i;
}
#endif

static inline u32 std__last(u32 len, [[maybe_unused]] const char* op) {
#ifndef ORB_RELEASE
    if (len == 0) std_trapf("%s: empty", op);
#endif
    return len - 1;
}

// A static_assert that can sit inside an expression.
#define std__assert(cond, message)                                                                 \
    ((void)sizeof(struct {                                                                         \
        static_assert(cond, message);                                                              \
        char unused;                                                                               \
    }))

#define std__not_void(T)                                                                           \
    _Generic((typeof(T)*)nullptr, void*: false, const void*: false, default: true)

// An array's elems is inline; a list's, slice's, or span's is a pointer.
#define std__is_array(container)                                                                   \
    _Generic(                                                                                      \
        (typeof_unqual((container).elems)*)nullptr,                                                \
        typeof((container).elems[0])**: false,                                                     \
        default: true                                                                              \
    )

#define std__elem(container) typeof_unqual((container).elems[0])

// __builtin_classify_type: 12 is a struct, 13 a union.
#define std__scalar(e) (__builtin_classify_type(e) != 12 && __builtin_classify_type(e) != 13)

// The value is the element type, or both are scalars and convert.
#define std__converts(elem, value)                                                                 \
    _Generic((value), typeof_unqual(elem): true, default: std__scalar(elem) && std__scalar(value))

#define std__pick2(_1, _2, which, ...) which
#define std__pick3(_1, _2, _3, which, ...) which
#define std__pick4(_1, _2, _3, _4, which, ...) which

#define std__def_span(stem, T)                                                                     \
    typedef struct stem##_span {                                                                   \
        const typeof(T)* elems;                                                                    \
        u32 len;                                                                                   \
    } stem##_span
#define std__def_slice(stem, T)                                                                    \
    std__def_span(stem, T);                                                                        \
    typedef struct stem##_slice {                                                                  \
        union {                                                                                    \
            struct {                                                                               \
                typeof(T)* elems;                                                                  \
                u32 len;                                                                           \
            };                                                                                     \
            stem##_span span;                                                                      \
        };                                                                                         \
    } stem##_slice
// owns overlaps the other fields; only lists and arrays have it.
#define std__def_list(stem, T)                                                                     \
    std__def_slice(stem, T);                                                                       \
    typedef struct stem##_list {                                                                   \
        union {                                                                                    \
            struct {                                                                               \
                typeof(T)* elems;                                                                  \
                u32 len, cap;                                                                      \
            };                                                                                     \
            stem##_slice slice;                                                                    \
            stem##_span span;                                                                      \
            u8 owns;                                                                               \
        };                                                                                         \
    } stem##_list
#define std__def_array(stem, T, N)                                                                 \
    std__def_slice(stem, T);                                                                       \
    typedef struct stem##_array {                                                                  \
        typeof(T) elems[N];                                                                        \
        union {                                                                                    \
            u32 len;                                                                               \
            u8 owns;                                                                               \
        };                                                                                         \
    } stem##_array

#define std__span_named(stem, T)                                                                   \
    static_assert(std__not_void(T), "span: element type is void");                                 \
    std__def_span(stem, T)
#define std__slice_named(stem, T)                                                                  \
    static_assert(std__not_void(T), "slice: element type is void");                                \
    std__def_slice(stem, T)
#define std__list_named(stem, T)                                                                   \
    static_assert(std__not_void(T), "list: element type is void");                                 \
    std__def_list(stem, T)
#define std__array_named(stem, T, N)                                                               \
    static_assert(std__not_void(T), "array: element type is void");                                \
    std__def_array(stem, T, N)
#define std__span_short(T) std__span_named(T, T)
#define std__slice_short(T) std__slice_named(T, T)
#define std__list_short(T) std__list_named(T, T)
#define std__array_short(T, N) std__array_named(T, T, N)

// span(T) or span(stem, T): T_span
#define span(...) std__pick2(__VA_ARGS__, std__span_named, std__span_short, )(__VA_ARGS__)
// slice(T) or slice(stem, T): T_slice, T_span
#define slice(...) std__pick2(__VA_ARGS__, std__slice_named, std__slice_short, )(__VA_ARGS__)
// list(T) or list(stem, T): T_list, T_slice, T_span
#define list(...) std__pick2(__VA_ARGS__, std__list_named, std__list_short, )(__VA_ARGS__)
// array(T, N) or array(stem, T, N): T_array, T_slice, T_span
#define array(...) std__pick3(__VA_ARGS__, std__array_named, std__array_short, )(__VA_ARGS__)

typedef struct arena arena;

// orb's functions for the steps that need orb. The table lives in orb, so a
// pointer to it survives a code reload.
typedef struct arena_hooks {
    bool (*grow)(arena* region, usize end); // commit through end; false records the failure
    void (*failed)(arena* region);          // an allocation did not fit
    void (*made)(arena* region);
    void (*cleared)(arena* region);
    void (*high)(arena* region); // peak passed 90% of size
} arena_hooks;

struct arena {
    char name[24];
    u8* base;
    usize size;       // usable bytes
    usize committed;  // bytes from base backed by memory
    usize used;       // a caller may save and restore it to drop what it allocated since
    usize peak;       // the most used has been
    usize reserved;   // the address space a reserved region holds; 0 otherwise
    jmp_buf* recover; // orb's own allocations longjmp here on failure when set
    usize overflow;   // bytes the failed allocation was short by, or could not commit
    bool refused;     // the failure was a refused commit rather than size
    bool failed;      // an allocation failed since it was made or last cleared
    bool logged, warned;
    const arena_hooks* hooks; // null: commits and logs nothing
};

static inline void std__arena_short(arena* region, usize overflow) {
    region->overflow = overflow;
    region->refused = false;
    region->failed = true;
}

// The next size bytes at align, or nullptr with the shortfall recorded. A child
// arena takes its range with commit false.
static inline u8* std__arena_take(arena* region, usize size, usize align, bool commit) {
    usize start = (region->used + align - 1) & ~(align - 1), end = 0;
    bool overflowed = start < region->used || ckd_add(&end, start, size);

    if (overflowed || end > region->size) {
        std__arena_short(region, overflowed ? size : end - region->size);
        return nullptr;
    }

    if (commit && end > region->committed && !(region->hooks && region->hooks->grow(region, end))) {
        if (!region->hooks) {
            region->overflow = end - region->committed;
            region->refused = true;
        }

        region->failed = true;
        return nullptr;
    }

    region->used = end;

    if (end > region->peak) {
        region->peak = end;

        if (!region->warned && region->hooks && end > region->size - region->size / 10)
            region->hooks->high(region);
    }

    return region->base ? region->base + start : nullptr;
}

static inline void std__arena_report(arena* region) {
    if (region->hooks) region->hooks->failed(region);
}

static inline u8* std__arena_take_report(arena* region, usize size, usize align, bool commit) {
    u8* bytes = std__arena_take(region, size, align, commit);

    if (!bytes) std__arena_report(region);

    return bytes;
}

static inline void* std__alloc(arena* region, usize elem, usize count, usize align) {
    usize size;

    if (ckd_mul(&size, elem, count)) {
        std__arena_short(region, SIZE_MAX);
        std__arena_report(region);
        return nullptr;
    }

    u8* bytes = std__arena_take_report(region, size, align, true);

    if (!bytes) return nullptr;

    memset(bytes, 0, size);
    return bytes;
}

// T* alloc(arena* region, T, usize count): zeroed, or nullptr when it does not fit
#define alloc(region, T, count) ((T*)std__alloc((region), sizeof(T), (count), alignof(T)))

// Makes *out an arena over size bytes of parent, neither committed nor zeroed.
// False, with *out a size-0 arena, when parent cannot fit them.
static inline bool arena_new(arena* out, arena* parent, const char* name, usize size) {
    u8* base = std__arena_take_report(parent, size, 16, false);

    *out = (arena) {.hooks = parent->hooks};
    snprintf(out->name, sizeof out->name, "%s", name);

    if (!base) return false;

    usize offset = (usize)(base - parent->base);

    out->base = base;
    out->size = size;
    out->committed = parent->committed > offset ? parent->committed - offset : 0;

    if (out->committed > size) out->committed = size;
    if (out->hooks) out->hooks->made(out);

    return true;
}

static inline void arena_clear(arena* region) {
    region->used = 0;
    region->failed = false;

    if (region->hooks) region->hooks->cleared(region);
}

// elems_at is the address of a list's elems pointer. A list ending at the
// arena's top grows in place; any other moves to the top.
static inline bool std__list_room(
    arena* region,
    void* elems_at,
    u32 len,
    u32* cap,
    usize size,
    usize align
) {
    if (len < *cap) return true;

    u8* elems;
    u64 next = *cap ? (u64)*cap * 2 : 8;
    usize old_bytes = (usize)*cap * size, new_bytes;

    memcpy(&elems, elems_at, sizeof elems);

    if (next > UINT32_MAX || ckd_mul(&new_bytes, (usize)next, size)) {
        std__arena_short(region, SIZE_MAX);
        std__arena_report(region);
        return false;
    }

    if (elems && region->base && elems + old_bytes == region->base + region->used) {
        if (!std__arena_take_report(region, new_bytes - old_bytes, 1, true)) return false;
    } else {
        u8* fresh = std__arena_take_report(region, new_bytes, align, true);

        if (!fresh) return false;

        if (len) memmove(fresh, elems, (usize)len * size);

        memcpy(elems_at, &fresh, sizeof fresh);
    }

    *cap = (u32)next;
    return true;
}

static inline bool std__array_insert(
    void* elems,
    u32* len,
    usize cap,
    usize size,
    usize i,
    const void* value
) {
#ifndef ORB_RELEASE
    if (i > *len) std_trapf("insert: index %zu past len %u", i, *len);
#endif
    if (*len >= cap) return false;

    u8* bytes = elems;

    memmove(bytes + (i + 1) * size, bytes + i * size, (*len - i) * size);
    memcpy(bytes + i * size, value, size);
    (*len)++;
    return true;
}

static inline bool std__list_insert(
    arena* region,
    void* elems_at,
    u32* len,
    u32* cap,
    usize size,
    usize align,
    usize i,
    const void* value
) {
#ifndef ORB_RELEASE
    if (i > *len) std_trapf("insert: index %zu past len %u", i, *len);
#endif
    if (!std__list_room(region, elems_at, *len, cap, size, align)) return false;

    u8* elems;

    memcpy(&elems, elems_at, sizeof elems);
    return std__array_insert(elems, len, *cap, size, i, value);
}

static inline u32 std__pop(u32* len) {
    return *len = std__last(*len, "pop");
}

static inline void* std__remove_at(void* elems, u32* len, usize size, usize i, void* out) {
    u8* bytes = elems;

    i = std__index(i, *len, "remove_at");
    memcpy(out, bytes + i * size, size);
    memmove(bytes + i * size, bytes + (i + 1) * size, (*len - i - 1) * size);
    (*len)--;
    return out;
}

static inline void* std__remove_swap(void* elems, u32* len, usize size, usize i, void* out) {
    u8* bytes = elems;

    i = std__index(i, *len, "remove_swap");
    memcpy(out, bytes + i * size, size);
    (*len)--;
    memmove(bytes + i * size, bytes + (usize)*len * size, size);
    return out;
}

static inline i64 std__find(const void* elems, u32 len, usize size, const void* value) {
    for (u32 i = 0; i < len; i++)
        if (memcmp((const u8*)elems + (usize)i * size, value, size) == 0) return i;

    return -1;
}

// What sub returns, punned to the caller's type. cap is meaningless.
typedef struct std__view {
    void* elems;
    u32 len, cap;
} std__view;

static inline std__view std__sub(
    void* elems,
    [[maybe_unused]] u32 len,
    usize size,
    usize from,
    usize to
) {
#ifndef ORB_RELEASE
    if (from > to || to > len) std_trapf("sub: [%zu, %zu) outside len %u", from, to, len);
#endif
    u32 count = (u32)(to - from);

    return (std__view) {elems ? (u8*)elems + from * size : elems, count, count};
}

// orb.h names its handle types here before including this header.
#ifndef STD_FIND_HANDLES
#define STD_FIND_HANDLES
#endif

// __builtin_classify_type: up to 5 are the integer kinds and pointers, 8 a real.
#define std__findable(e)                                                                           \
    _Generic(                                                                                      \
        (e),                                                                                       \
        long double: false,                                                                        \
        STD_FIND_HANDLES default: __builtin_classify_type(e) <= 5 ||                               \
            __builtin_classify_type(e) == 8                                                        \
    )

// bool push(T_array*, T value) or bool push(arena*, T_list*, T value)
#define push(...) std__pick3(__VA_ARGS__, std__push_list, std__push_array, )(__VA_ARGS__)
#define std__push_array(container, value)                                                          \
    ((void)sizeof((container)->owns),                                                              \
     std__assert(                                                                                  \
         std__is_array(*(container)), "push: a list takes an arena: push(arena, &list, value)"     \
     ),                                                                                            \
     std__assert(                                                                                  \
         std__converts((container)->elems[0], value), "push: the value is not of the element type" \
     ),                                                                                            \
     std__array_insert(                                                                            \
         &*(container)->elems, &(container)->len,                                                  \
         sizeof((container)->elems) / sizeof((container)->elems[0]),                               \
         sizeof((container)->elems[0]), (container)->len, (std__elem(*(container))[1]) {value}     \
     ))
#define std__push_list(region, container, value)                                                   \
    ((void)sizeof((container)->owns),                                                              \
     std__assert(                                                                                  \
         !std__is_array(*(container)), "push: an array takes no arena: push(&array, value)"        \
     ),                                                                                            \
     std__assert(                                                                                  \
         std__converts((container)->elems[0], value), "push: the value is not of the element type" \
     ),                                                                                            \
     std__list_insert(                                                                             \
         (region), &(container)->elems, &(container)->len, &(container)->cap,                      \
         sizeof((container)->elems[0]), alignof(std__elem(*(container))), (container)->len,        \
         (std__elem(*(container))[1]) {value}                                                      \
     ))

// bool insert(T_array*, usize i, T value) or bool insert(arena*, T_list*, usize i, T value)
#define insert(...) std__pick4(__VA_ARGS__, std__insert_list, std__insert_array, )(__VA_ARGS__)
#define std__insert_array(container, i, value)                                                     \
    ((void)sizeof((container)->owns),                                                              \
     std__assert(                                                                                  \
         std__is_array(*(container)),                                                              \
         "insert: a list takes an arena: insert(arena, &list, i, value)"                           \
     ),                                                                                            \
     std__assert(                                                                                  \
         std__converts((container)->elems[0], value),                                              \
         "insert: the value is not of the element type"                                            \
     ),                                                                                            \
     std__array_insert(                                                                            \
         &*(container)->elems, &(container)->len,                                                  \
         sizeof((container)->elems) / sizeof((container)->elems[0]),                               \
         sizeof((container)->elems[0]), (i), (std__elem(*(container))[1]) {value}                  \
     ))
#define std__insert_list(region, container, i, value)                                              \
    ((void)sizeof((container)->owns),                                                              \
     std__assert(                                                                                  \
         !std__is_array(*(container)), "insert: an array takes no arena: insert(&array, i, value)" \
     ),                                                                                            \
     std__assert(                                                                                  \
         std__converts((container)->elems[0], value),                                              \
         "insert: the value is not of the element type"                                            \
     ),                                                                                            \
     std__list_insert(                                                                             \
         (region), &(container)->elems, &(container)->len, &(container)->cap,                      \
         sizeof((container)->elems[0]), alignof(std__elem(*(container))), (i),                     \
         (std__elem(*(container))[1]) {value}                                                      \
     ))

// T pop(T_array* or T_list*): the last element, removed
#define pop(container)                                                                             \
    ((void)sizeof((container)->owns), (container)->elems[std__pop(&(container)->len)])

// T remove_at(T_array* or T_list*, usize i): element i, removed; order kept
#define remove_at(container, i)                                                                    \
    ((void)sizeof((container)->owns),                                                              \
     *(std__elem(*(container))*)std__remove_at(                                                    \
         &*(container)->elems, &(container)->len, sizeof((container)->elems[0]), (i),              \
         (std__elem(*(container))[1]) {}                                                           \
     ))

// T remove_swap(T_array* or T_list*, usize i): element i, removed; the last moves into i
#define remove_swap(container, i)                                                                  \
    ((void)sizeof((container)->owns),                                                              \
     *(std__elem(*(container))*)std__remove_swap(                                                  \
         &*(container)->elems, &(container)->len, sizeof((container)->elems[0]), (i),              \
         (std__elem(*(container))[1]) {}                                                           \
     ))

// void clear(T_array* or T_list*): len 0, storage kept
#define clear(container) ((void)sizeof((container)->owns), (void)((container)->len = 0))

// T get(container, usize i): element i as an lvalue
#define get(container, i) ((container).elems[std__index((i), (container).len, "get")])

// T* get_ptr(container, usize i)
#define get_ptr(container, i) (&(container).elems[std__index((i), (container).len, "get_ptr")])

// T last(container): the last element as an lvalue
#define last(container) ((container).elems[std__last((container).len, "last")])

// T* data(container): the first element; valid when len is 0
#define data(container) (&*(container).elems)

// i64 find(container, T value): the first index holding value, or -1
#define find(container, value)                                                                     \
    (std__assert(                                                                                  \
         std__findable((container).elems[0]),                                                      \
         "find: elements must be numbers, pointers, or handles"                                    \
     ),                                                                                            \
     std__assert(                                                                                  \
         std__converts((container).elems[0], value), "find: the value is not of the element type"  \
     ),                                                                                            \
     std__find(                                                                                    \
         &*(container).elems, (container).len, sizeof((container).elems[0]),                       \
         (std__elem(container)[1]) {value}                                                         \
     ))

// sub(container, usize from, usize to): [from, to) of a slice or span, as the same type
#define sub(container, from, to)                                                                   \
    (std__assert(!std__is_array(container), "sub: convert an array with as_slice first"),          \
     ((union {                                                                                     \
          std__view view;                                                                          \
          typeof_unqual(container) out;                                                            \
      }) {                                                                                         \
         .view = std__sub(                                                                         \
             (void*)&*(container).elems, (container).len, sizeof((container).elems[0]), (from),    \
             (to)                                                                                  \
         )                                                                                         \
     }                                                                                             \
          .out))

// stem_slice as_slice(stem, T_array), stem_span as_span(stem, T_array)
#define as_slice(stem, container)                                                                  \
    (std__assert(std__is_array(container), "as_slice: lists and slices convert by field"),         \
     (stem##_slice) {.elems = (container).elems, .len = (container).len})
#define as_span(stem, container)                                                                   \
    (std__assert(std__is_array(container), "as_span: lists and slices convert by field"),          \
     (stem##_span) {.elems = (container).elems, .len = (container).len})

list(u8);
list(u16);
list(u32);
list(i32);
list(char);

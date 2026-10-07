#pragma once

// orb__ names are this header's internals.

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef size_t usize;
typedef ptrdiff_t isize;
typedef float f32;
typedef double f64;

#define orb__pick2(_1, _2, which, ...) which
#define orb__pick3(_1, _2, _3, which, ...) which

#define orb__span(stem, T)                                                                         \
    typedef struct stem##_span {                                                                   \
        const typeof(T)* elems;                                                                    \
        u32 len;                                                                                   \
    } stem##_span
#define orb__slice(stem, T)                                                                        \
    typedef struct stem##_slice {                                                                  \
        typeof(T)* elems;                                                                          \
        u32 len;                                                                                   \
    } stem##_slice
#define orb__list(stem, T)                                                                         \
    typedef struct stem##_list {                                                                   \
        typeof(T)* elems;                                                                          \
        u32 len, cap;                                                                              \
    } stem##_list
#define orb__array(stem, T, N)                                                                     \
    typedef struct stem##_array {                                                                  \
        typeof(T) elems[N];                                                                        \
        u32 len;                                                                                   \
    } stem##_array
#define orb__span_short(T) orb__span(T, T)
#define orb__slice_short(T) orb__slice(T, T)
#define orb__list_short(T) orb__list(T, T)
#define orb__array_short(T, N) orb__array(T, T, N)

// The named form names its type from stem in place of T.
// orb_span(T) or orb_span(stem, T): T_span
#define orb_span(...) orb__pick2(__VA_ARGS__, orb__span, orb__span_short, )(__VA_ARGS__)
// orb_slice(T) or orb_slice(stem, T): T_slice
#define orb_slice(...) orb__pick2(__VA_ARGS__, orb__slice, orb__slice_short, )(__VA_ARGS__)
// orb_list(T) or orb_list(stem, T): T_list
#define orb_list(...) orb__pick2(__VA_ARGS__, orb__list, orb__list_short, )(__VA_ARGS__)
// orb_array(T, N) or orb_array(stem, T, N): T_array
#define orb_array(...) orb__pick3(__VA_ARGS__, orb__array, orb__array_short, )(__VA_ARGS__)

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        long long check_a = (long long)(a), check_b = (long long)(b);                              \
        if (check_a != check_b) {                                                                  \
            fprintf(                                                                               \
                stderr, "%s:%d: %s == %lld, expected %lld (%s)\n", __FILE__, __LINE__, #a,         \
                check_a, check_b, #b                                                               \
            );                                                                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

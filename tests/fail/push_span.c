// expect: no member named
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

void f(arena* region, u32_span items) {
    push(region, &items, 1u);
}

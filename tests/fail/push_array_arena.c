// expect: push: an array takes no arena
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

array(u32, 4);

void f(arena* region, u32_array items) {
    push(region, &items, 1u);
}

// expect: sub: convert an array with as_slice first
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

array(u32, 4);

void f(u32_array items) {
    (void)sub(items, 0, 1);
}

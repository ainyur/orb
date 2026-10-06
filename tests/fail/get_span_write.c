// expect: read-only
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

void f(u32_span items) {
    get(items, 0) = 1;
}

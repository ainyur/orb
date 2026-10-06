// expect: no member named
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

void f(u32_span items) {
    clear(&items);
}

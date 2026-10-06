// expect: push: a list takes an arena
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

void f(u32_list items) {
    push(&items, 1u);
}

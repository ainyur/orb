// expect: push: the value is not of the element type
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

list(member);

void f(arena* region, member_list items) {
    push(region, &items, 1u);
}

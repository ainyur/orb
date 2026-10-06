// expect: find: elements must be numbers, pointers, or handles
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

list(member);

void f(member_list items) {
    (void)find(items, ((member) {1}));
}

// expect: array: element type is void
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

array(void, 4);

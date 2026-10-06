// expect: redefinition of struct or union
// expect: has incompatible definitions
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

array(u8, 4);
array(u8, 8);

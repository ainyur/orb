// expect: span: element type is void
#include "orb_std.h"

typedef struct member {
    int hp;
} member;

span(void);

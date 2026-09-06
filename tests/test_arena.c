#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static uint8_t mem[1024];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    uint8_t* p = orb_arena_push(&a, 3, 1);

    CHECK(p == mem);
    CHECK_EQ(a.used, 3);

    uint32_t* q = orb_arena_push(&a, 4, 4);

    CHECK(((uintptr_t)q & 3) == 0);
    CHECK_EQ(a.used, 8);
    CHECK_EQ(*q, 0);

    orb_arena sub = orb_arena_carve(&a, "sub", 64);

    CHECK_EQ(sub.size, 64);
    CHECK(sub.base >= mem && sub.base + 64 <= mem + sizeof mem);
    CHECK(((uintptr_t)sub.base & 15) == 0);
    orb_arena_push(&sub, 64, 1);
    CHECK_EQ(sub.used, 64);

    orb_arena_reset(&a);
    CHECK_EQ(a.used, 0);
    return 0;
}

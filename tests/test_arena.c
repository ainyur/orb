#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static uint8_t mem[1024];
    orb_arena arena;
    orb_arena_init(&arena, "test", mem, sizeof mem);
    uint8_t* bytes = orb_arena_push(&arena, 3, 1);

    CHECK(bytes == mem);
    CHECK_EQ(arena.used, 3);

    uint32_t* aligned = orb_arena_push(&arena, 4, 4);

    CHECK(((uintptr_t)aligned & 3) == 0);
    CHECK_EQ(arena.used, 8);
    CHECK_EQ(*aligned, 0);

    orb_arena sub = orb_arena_carve(&arena, "sub", 64);

    CHECK_EQ(sub.size, 64);
    CHECK(sub.base >= mem && sub.base + 64 <= mem + sizeof mem);
    CHECK(((uintptr_t)sub.base & 15) == 0);
    orb_arena_push(&sub, 64, 1);
    CHECK_EQ(sub.used, 64);

    // restoring used hands the same bytes out again, zeroed; peak keeps the
    // high-water mark
    size_t mark = arena.used;
    uint8_t* block = orb_arena_push(&arena, 8, 1);

    block[0] = 7;
    arena.used = mark;
    CHECK(orb_arena_push(&arena, 8, 1) == block);
    CHECK_EQ(block[0], 0);
    CHECK_EQ(arena.peak, mark + 8);

    orb_arena_reset(&arena);
    CHECK_EQ(arena.used, 0);

    // a push whose start + size wraps past SIZE_MAX fails the same way an
    // over-full arena does
    jmp_buf recover;

    orb_arena_push(&arena, 1, 1);
    arena.recover = &recover;

    if (setjmp(recover) == 0) {
        orb_arena_push(&arena, SIZE_MAX, 1);
        CHECK(false);
    } else {
        CHECK_EQ(arena.overflow, SIZE_MAX);
    }

    // a count * element size overflow in orb_arena_push_array fails the same way
    if (setjmp(recover) == 0) {
        orb_arena_push_array(&arena, uint16_t, SIZE_MAX);
        CHECK(false);
    } else {
        CHECK_EQ(arena.overflow, SIZE_MAX);
    }

    arena.recover = nullptr;
    CHECK(!arena.refused); // a fixed arena is never refused a commit

    // a reserved arena commits in steps as it grows and keeps them across a reset
    static orb_arena grow;

    CHECK(orb_arena_reserve(&grow, "grow", 3 * ORB_COMMIT_STEP + 1));
    CHECK_EQ(grow.size, 4 * ORB_COMMIT_STEP); // rounded up to whole steps
    CHECK_EQ(grow.committed, 0);

    uint8_t* first = orb_arena_push(&grow, 16, 16);

    CHECK_EQ(grow.committed, ORB_COMMIT_STEP);
    first[15] = 1;

    // a push ending exactly on a step commits nothing more
    orb_arena_push(&grow, ORB_COMMIT_STEP - 16, 1);
    CHECK_EQ(grow.committed, ORB_COMMIT_STEP);

    // one push spanning two steps commits both, zeroed and writable
    uint8_t* big = orb_arena_push(&grow, 2 * ORB_COMMIT_STEP, 1);

    CHECK_EQ(grow.committed, 3 * ORB_COMMIT_STEP);
    CHECK_EQ(big[0] + big[2 * ORB_COMMIT_STEP - 1], 0);
    big[2 * ORB_COMMIT_STEP - 1] = 9;

    orb_arena_reset(&grow);
    CHECK_EQ(grow.used, 0);
    CHECK_EQ(grow.committed, 3 * ORB_COMMIT_STEP);
    CHECK_EQ(grow.peak, 3 * ORB_COMMIT_STEP);

    // the whole reserve is usable; one byte past it is exhaustion, not a second reserve
    grow.recover = &recover;

    if (setjmp(recover) == 0) {
        orb_arena_push(&grow, 4 * ORB_COMMIT_STEP, 1);
        orb_arena_push(&grow, 1, 1);
        CHECK(false);
    } else {
        CHECK_EQ(grow.overflow, 1);
        CHECK(!grow.refused);
        CHECK_EQ(grow.committed, 4 * ORB_COMMIT_STEP);
    }

    orb_arena_release(&grow);
    CHECK(grow.base == nullptr);
    orb_arena_release(&grow); // releasing a released arena is a no-op
    return 0;
}

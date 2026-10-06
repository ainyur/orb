#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    static u8 mem[1024];
    arena fixed;
    orb_arena_init(&fixed, "test", mem, sizeof mem);
    u8* bytes = orb_arena_push(&fixed, 3, 1);

    CHECK(bytes == mem);
    CHECK_EQ(fixed.used, 3);

    u32* aligned = orb_arena_push(&fixed, 4, 4);

    CHECK(((uintptr_t)aligned & 3) == 0);
    CHECK_EQ(fixed.used, 8);
    CHECK_EQ(*aligned, 0);

    arena sub;

    CHECK(arena_new(&sub, &fixed, "sub", 64));

    CHECK_EQ(sub.size, 64);
    CHECK(sub.base >= mem && sub.base + 64 <= mem + sizeof mem);
    CHECK(((uintptr_t)sub.base & 15) == 0);
    orb_arena_push(&sub, 64, 1);
    CHECK_EQ(sub.used, 64);

    // restoring used hands the same bytes out again, zeroed; peak keeps the
    // high-water mark
    usize mark = fixed.used;
    u8* block = orb_arena_push(&fixed, 8, 1);

    block[0] = 7;
    fixed.used = mark;
    CHECK(orb_arena_push(&fixed, 8, 1) == block);
    CHECK_EQ(block[0], 0);
    CHECK_EQ(fixed.peak, mark + 8);

    arena_clear(&fixed);
    CHECK_EQ(fixed.used, 0);

    // a push whose start + size wraps past SIZE_MAX fails the same way an
    // over-full arena does
    jmp_buf recover;

    orb_arena_push(&fixed, 1, 1);
    fixed.recover = &recover;

    if (setjmp(recover) == 0) {
        orb_arena_push(&fixed, SIZE_MAX, 1);
        CHECK(false);
    } else {
        CHECK_EQ(fixed.overflow, SIZE_MAX);
    }

    // a count * element size overflow in orb_arena_push_array fails the same way
    if (setjmp(recover) == 0) {
        orb_arena_push_array(&fixed, u16, SIZE_MAX);
        CHECK(false);
    } else {
        CHECK_EQ(fixed.overflow, SIZE_MAX);
    }

    fixed.recover = nullptr;
    CHECK(!fixed.refused); // a fixed arena is never refused a commit

    // a reserved arena commits in steps as it grows and keeps them across a reset
    static arena grow;

    CHECK(orb_arena_reserve(&grow, "grow", 3 * ORB_COMMIT_STEP + 1));
    CHECK_EQ(grow.size, 4 * ORB_COMMIT_STEP); // rounded up to whole steps
    CHECK_EQ(grow.committed, 0);

    u8* first = orb_arena_push(&grow, 16, 16);

    CHECK_EQ(grow.committed, ORB_COMMIT_STEP);
    first[15] = 1;

    // a push ending exactly on a step commits nothing more
    orb_arena_push(&grow, ORB_COMMIT_STEP - 16, 1);
    CHECK_EQ(grow.committed, ORB_COMMIT_STEP);

    // one push spanning two steps commits both, zeroed and writable
    u8* big = orb_arena_push(&grow, 2 * ORB_COMMIT_STEP, 1);

    CHECK_EQ(grow.committed, 3 * ORB_COMMIT_STEP);
    CHECK_EQ(big[0] + big[2 * ORB_COMMIT_STEP - 1], 0);
    big[2 * ORB_COMMIT_STEP - 1] = 9;

    arena_clear(&grow);
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
    // a reserved arena commits as it grows, and a child commits lazily inside it
    arena reserved;

    CHECK(orb_arena_reserve(&reserved, "reserved", 8 << 20));
    CHECK_EQ(reserved.reserved, 8 << 20);
    CHECK_EQ(reserved.committed, 0);
    CHECK(reserved.hooks == &orb_arena_hooks);

    arena child;

    CHECK(arena_new(&child, &reserved, "child", 3 << 20));
    CHECK_EQ(child.committed, 0);
    CHECK_EQ(reserved.committed, 0); // arena_new commits nothing

    u8* grown = alloc(&child, u8, 100);

    CHECK(grown != nullptr);
    CHECK_EQ(grown[99], 0);
    CHECK(child.committed >= 100);
    grown[99] = 1; // committed memory is writable

    // the parent's next allocation lands past the child and commits there
    u8* after = alloc(&reserved, u8, 10);

    CHECK(after == reserved.base + (3 << 20));
    CHECK(reserved.committed >= (3 << 20) + 10);

    // exhaustion logs once, with the text orb_arena_error writes
    CHECK(alloc(&child, u8, 4 << 20) == nullptr);
    CHECK(child.failed);
    CHECK(child.logged);
    arena_clear(&child);
    CHECK(!child.failed);
    CHECK(child.logged); // once per run

    // the 90% warning logs once
    CHECK(alloc(&child, u8, (3 << 20) - (3 << 20) / 20) != nullptr);
    CHECK(child.warned);

    orb_arena_release(&reserved);
    CHECK(reserved.base == nullptr);

    // the registry records arenas whose struct lies in state or global, not
    // those whose memory lies in frame, and a clear drops its children
    arena state, global, frame;
    static alignas(16) u8 state_mem[256], global_mem[4096], frame_mem[1024];

    orb_arena_init(&state, "state", state_mem, sizeof state_mem);
    orb_arena_init(&global, "global", global_mem, sizeof global_mem);
    orb_arena_init(&frame, "frame", frame_mem, sizeof frame_mem);
    state.hooks = global.hooks = frame.hooks = &orb_arena_hooks;
    orb_arena_homes(&state, &global, &frame);

    arena* in_state = (arena*)orb_arena_push(&state, sizeof(arena), alignof(arena));
    arena on_stack;

    CHECK(arena_new(in_state, &global, "battle", 512));
    CHECK(arena_new(in_state, &global, "battle", 512)); // remade in place: one entry
    CHECK(arena_new(&on_stack, &global, "local", 64));

    // a struct in global whose memory lies in state is dropped when global clears
    arena* inner = (arena*)orb_arena_push(&global, sizeof(arena), alignof(arena));

    CHECK(arena_new(inner, &state, "inner", 32));
    CHECK(orb_arena_recorded(0) == in_state);
    CHECK(orb_arena_recorded(1) == inner);
    CHECK(orb_arena_recorded(2) == nullptr);
    arena_clear(&global);
    CHECK(orb_arena_recorded(0) == nullptr);
    orb_arena_homes(nullptr, nullptr, nullptr);
    return 0;
}

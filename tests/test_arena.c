#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

orb_list(u32);

// alloc: zeroed, aligned to max_align_t, nullptr with failed set and the first
// failure logged when it does not fit, and the base for a count of 0
static int test_alloc(void) {
    static alignas(max_align_t) u8 mem[256];
    orb_arena small;

    memset(mem, 0xff, sizeof mem);
    orb_arena_init(&small, "small", mem, sizeof mem);

    u8* bytes = orb_arena_alloc(&small, 3, 1);

    CHECK(bytes == mem);
    CHECK_EQ(bytes[0] + bytes[1] + bytes[2], 0);

    u32* words = orb_arena_alloc(&small, 2, sizeof(u32));

    CHECK_EQ((uintptr_t)words % alignof(max_align_t), 0);
    CHECK_EQ(words[1], 0);

    orb_log_clear();
    CHECK(orb_arena_alloc(&small, 1000, 1) == nullptr);
    CHECK(small.failed);
    CHECK_EQ(small.overflow, 32 + 1000 - 256);
    CHECK(strstr(orb_log_line(0), "'small' exhausted") != nullptr);
    CHECK(orb_arena_alloc(&small, SIZE_MAX / 4, 8) == nullptr);
    CHECK_EQ(small.overflow, SIZE_MAX);
    CHECK_EQ(orb_log_line_count(), 1); // only the first failure logs

    orb_arena_clear(&small);
    CHECK(orb_arena_alloc(&small, 0, sizeof(u32)) == mem);
    CHECK(!small.failed);
    CHECK_EQ(small.used, 0);

    orb_arena none;

    orb_arena_init(&none, "none", nullptr, 0);
    CHECK(orb_arena_alloc(&none, 0, sizeof(u32)) == nullptr);
    CHECK(!none.failed);

    // only arena_new, alloc, and list_resize warn at 90%, and only when they
    // raise the peak
    orb_arena_push(&small, 240, 1);
    CHECK(!small.warned);
    small.used = 0;
    CHECK(orb_arena_alloc(&small, 16, 1) != nullptr);
    CHECK(!small.warned);
    small.used = 240;
    orb_log_clear();
    CHECK(orb_arena_alloc(&small, 8, 1) != nullptr);
    CHECK(small.warned);
    CHECK(strstr(orb_log_line(0), "'small' at 96% of") != nullptr);
    return 0;
}

// list_resize: from nothing, in place at the top, moved when not at the top,
// smaller in place, and false with the list unchanged when it cannot
static int test_list_resize(void) {
    static alignas(max_align_t) u8 mem[4096];
    orb_arena lists;

    orb_arena_init(&lists, "lists", mem, sizeof mem);

    u32_list grow = {};

    CHECK(orb_arena_list_resize(&lists, &grow, 8, sizeof(u32)));
    CHECK(grow.elems == (u32*)mem);
    CHECK_EQ(grow.cap, 8);
    CHECK_EQ(lists.used, 32);

    for (u32 i = 0; i < 8; i++)
        grow.elems[grow.len++] = i;

    // at the arena's top it extends in place
    CHECK(orb_arena_list_resize(&lists, &grow, 16, sizeof(u32)));
    CHECK(grow.elems == (u32*)mem);
    CHECK_EQ(lists.used, 64);

    // away from the top it moves, keeping its elements
    u32_list other = {};

    CHECK(orb_arena_list_resize(&lists, &other, 4, sizeof(u32)));
    CHECK(orb_arena_list_resize(&lists, &grow, 32, sizeof(u32)));
    CHECK(grow.elems == (u32*)(mem + 80));
    CHECK_EQ(grow.len, 8);
    CHECK_EQ(grow.elems[7], 7);
    CHECK_EQ(lists.used, 80 + 128);

    // a smaller cap gives bytes back at the top and keeps the storage elsewhere
    CHECK(orb_arena_list_resize(&lists, &grow, 8, sizeof(u32)));
    CHECK_EQ(lists.used, 80 + 32);
    CHECK(orb_arena_list_resize(&lists, &other, 0, sizeof(u32)));
    CHECK_EQ(other.cap, 0);
    CHECK(other.elems == (u32*)(mem + 64));
    CHECK_EQ(lists.used, 80 + 32);

    // two lists resized in turn stay correct
    u32_list evens = {}, odds = {};

    for (u32 i = 0; i < 100; i++) {
        if (evens.len == evens.cap)
            CHECK(
                orb_arena_list_resize(&lists, &evens, evens.cap ? evens.cap * 2 : 8, sizeof(u32))
            );

        evens.elems[evens.len++] = i * 2;

        if (odds.len == odds.cap)
            CHECK(orb_arena_list_resize(&lists, &odds, odds.cap ? odds.cap * 2 : 8, sizeof(u32)));

        odds.elems[odds.len++] = i * 2 + 1;
    }

    for (u32 i = 0; i < 100; i++) {
        CHECK_EQ(evens.elems[i], i * 2);
        CHECK_EQ(odds.elems[i], i * 2 + 1);
    }

    // false with the list unchanged: a cap below len, an arena that cannot fit
    // it, and a cap * size that overflows
    u32* kept = grow.elems;
    usize used = lists.used;

    CHECK(!orb_arena_list_resize(&lists, &grow, 4, sizeof(u32)));
    CHECK(!lists.failed);
    CHECK_EQ(lists.used, used);
    CHECK(!orb_arena_list_resize(&lists, &grow, 1 << 20, sizeof(u32)));
    CHECK(lists.failed);
    CHECK(!orb_arena_list_resize(&lists, &grow, UINT32_MAX, SIZE_MAX / 2));
    CHECK_EQ(lists.overflow, SIZE_MAX);
    CHECK(grow.elems == kept);
    CHECK_EQ(grow.len, 8);
    CHECK_EQ(grow.cap, 8);
    CHECK_EQ(lists.used, used);
    return 0;
}

// arena_new: the name is cut to 23 bytes, the child starts at a max_align_t
// boundary, and a parent that cannot fit it is marked failed
static int test_arena_new(void) {
    static alignas(max_align_t) u8 mem[256];
    orb_arena parent, child;

    orb_arena_init(&parent, "parent", mem, sizeof mem);
    CHECK(orb_arena_alloc(&parent, 3, 1) != nullptr);
    CHECK(orb_arena_new(&child, &parent, "a name longer than twenty-three bytes", 64));
    CHECK(strcmp(child.name, "a name longer than twen") == 0);
    CHECK(child.base == mem + alignof(max_align_t));
    CHECK_EQ(child.size, 64);
    CHECK_EQ(child.committed, 64);
    CHECK(!parent.failed);

    CHECK(!orb_arena_new(&child, &parent, "big", 1 << 20));
    CHECK(strcmp(child.name, "big") == 0);
    CHECK(child.base == nullptr);
    CHECK_EQ(child.size, 0);
    CHECK(parent.failed);
    return 0;
}

int main(void) {
    CHECK_EQ(test_alloc(), 0);
    CHECK_EQ(test_list_resize(), 0);
    CHECK_EQ(test_arena_new(), 0);

    static u8 mem[1024];
    orb_arena fixed;
    orb_arena_init(&fixed, "test", mem, sizeof mem);
    u8* bytes = orb_arena_push(&fixed, 3, 1);

    CHECK(bytes == mem);
    CHECK_EQ(fixed.used, 3);

    u32* aligned = orb_arena_push(&fixed, 4, 4);

    CHECK(((uintptr_t)aligned & 3) == 0);
    CHECK_EQ(fixed.used, 8);
    CHECK_EQ(*aligned, 0);

    orb_arena sub;

    CHECK(orb_arena_new(&sub, &fixed, "sub", 64));

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

    orb_arena_clear(&fixed);
    CHECK_EQ(fixed.used, 0);

    // a push whose start + size wraps past SIZE_MAX fails the same way an
    // over-full arena does
    jmp_buf recover;

    orb_arena_push(&fixed, 1, 1);
    orb_arena_recover(&recover);

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

    orb_arena_recover(nullptr);
    CHECK(!fixed.refused); // a fixed arena is never refused a commit

    // a reserved arena commits in steps as it grows and keeps them across a reset
    static orb_arena grow;

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

    orb_arena_clear(&grow);
    CHECK_EQ(grow.used, 0);
    CHECK_EQ(grow.committed, 3 * ORB_COMMIT_STEP);
    CHECK_EQ(grow.peak, 3 * ORB_COMMIT_STEP);

    // the whole reserve is usable; one byte past it is exhaustion, not a second reserve
    orb_arena_recover(&recover);

    if (setjmp(recover) == 0) {
        orb_arena_push(&grow, 4 * ORB_COMMIT_STEP, 1);
        orb_arena_push(&grow, 1, 1);
        CHECK(false);
    } else {
        CHECK_EQ(grow.overflow, 1);
        CHECK(!grow.refused);
        CHECK_EQ(grow.committed, 4 * ORB_COMMIT_STEP);
    }

    orb_arena_recover(nullptr);

    orb_arena_release(&grow);
    CHECK(grow.base == nullptr);
    orb_arena_release(&grow); // releasing a released arena is a no-op
    // a reserved arena commits as it grows, and a child commits lazily inside it
    orb_arena reserved;

    CHECK(orb_arena_reserve(&reserved, "reserved", 8 << 20));
    CHECK_EQ(reserved.reserved, 8 << 20);
    CHECK_EQ(reserved.committed, 0);

    orb_arena child;

    CHECK(orb_arena_new(&child, &reserved, "child", 3 << 20));
    CHECK_EQ(child.committed, 0);
    CHECK_EQ(reserved.committed, 0); // arena_new commits nothing

    u8* grown = orb_arena_alloc(&child, 100, 1);

    CHECK(grown != nullptr);
    CHECK_EQ(grown[99], 0);
    CHECK(child.committed >= 100);
    grown[99] = 1; // committed memory is writable

    // the parent's next allocation lands past the child and commits there
    u8* after = orb_arena_alloc(&reserved, 10, 1);

    CHECK(after == reserved.base + (3 << 20));
    CHECK(reserved.committed >= (3 << 20) + 10);

    // exhaustion logs once, with the text orb_arena_error writes
    CHECK(orb_arena_alloc(&child, 4 << 20, 1) == nullptr);
    CHECK(child.failed);
    CHECK(child.logged);
    orb_arena_clear(&child);
    CHECK(!child.failed);
    CHECK(child.logged); // once per run

    // the 90% warning logs once
    CHECK(orb_arena_alloc(&child, (3 << 20) - (3 << 20) / 20, 1) != nullptr);
    CHECK(child.warned);

    orb_arena_release(&reserved);
    CHECK(reserved.base == nullptr);

    // the registry records arenas whose struct lies in state or global, and a
    // clear drops those whose struct or memory lies in the cleared arena
    orb_arena state, global, frame;
    static alignas(16) u8 state_mem[512], global_mem[4096], frame_mem[1024];

    orb_arena_init(&state, "state", state_mem, sizeof state_mem);
    orb_arena_init(&global, "global", global_mem, sizeof global_mem);
    orb_arena_init(&frame, "frame", frame_mem, sizeof frame_mem);
    orb_arena_homes(&state, &global);

    orb_arena* in_state = (orb_arena*)orb_arena_push(&state, sizeof(orb_arena), alignof(orb_arena));
    orb_arena on_stack;

    CHECK(orb_arena_new(in_state, &global, "battle", 512));
    CHECK(orb_arena_new(in_state, &global, "battle", 512)); // remade in place: one entry
    CHECK(orb_arena_new(&on_stack, &global, "local", 64));

    // a struct in global whose memory lies in state is dropped when global clears
    orb_arena* inner = (orb_arena*)orb_arena_push(&global, sizeof(orb_arena), alignof(orb_arena));

    CHECK(orb_arena_new(inner, &state, "inner", 32));

    // a struct in state whose memory lies in frame is dropped when frame clears
    orb_arena* per_frame =
        (orb_arena*)orb_arena_push(&state, sizeof(orb_arena), alignof(orb_arena));

    CHECK(orb_arena_new(per_frame, &frame, "per frame", 64));
    CHECK(orb_arena_recorded(0) == in_state);
    CHECK(orb_arena_recorded(1) == inner);
    CHECK(orb_arena_recorded(2) == per_frame);
    CHECK(orb_arena_recorded(3) == nullptr);
    orb_arena_clear(&frame);
    CHECK(orb_arena_recorded(2) == nullptr);
    orb_arena_clear(&global);
    CHECK(orb_arena_recorded(0) == nullptr);
    orb_arena_homes(nullptr, nullptr);
    return 0;
}

#include "test.h"

#include <setjmp.h>

static jmp_buf test_trap_jump;
static char test_trap_text[128];

#define std_trap(message)                                                                          \
    (snprintf(test_trap_text, sizeof test_trap_text, "%s", (message)), longjmp(test_trap_jump, 1))

typedef struct {
    unsigned v;
} test_handle;

#define STD_FIND_HANDLES                                                                           \
    test_handle:                                                                                   \
    true,
#include "../src/orb_std.h"

// The expression must trap with exactly this message.
#define CHECK_TRAP(expr, text)                                                                     \
    do {                                                                                           \
        test_trap_text[0] = 0;                                                                     \
        if (!setjmp(test_trap_jump)) {                                                             \
            (void)(expr);                                                                          \
            fprintf(stderr, "%s:%d: %s did not trap\n", __FILE__, __LINE__, #expr);                \
            return 1;                                                                              \
        }                                                                                          \
        if (strcmp(test_trap_text, text) != 0) {                                                   \
            fprintf(stderr, "%s:%d: trapped \"%s\"\n", __FILE__, __LINE__, test_trap_text);        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct member {
    int hp;
    char name[3];
} member;

array(member, 4);
array(member, 4); // a repeated definition is the same type
list(member);
array(stock, int, 2 * 3);
list(text, char*);
list(test_handle);

static int test_hits;

static int counted(int value) {
    test_hits++;
    return value;
}

static arena* counted_arena(arena* region) {
    test_hits++;
    return region;
}

static int heal(member_slice members) {
    for (u32 i = 0; i < members.len; i++)
        members.elems[i].hp += 10;

    return (int)members.len;
}

static int total(member_span members) {
    int sum = 0;

    for (u32 i = 0; i < members.len; i++)
        sum += members.elems[i].hp;

    return sum;
}

int main(void) {
    CHECK_EQ(sizeof(u8_list), 16);
    CHECK_EQ(sizeof(u8_slice), 16);
    CHECK_EQ(sizeof(u8_span), 16);
    CHECK_EQ(sizeof(member_array), 4 * sizeof(member) + 4);
    CHECK_EQ(sizeof(stock_array), 6 * sizeof(int) + 4);

    // arrays: push, insert, full, get, remove, pop, clear
    member_array party = {};

    CHECK(push(&party, ((member) {1, "a"})));
    CHECK(push(&party, ((member) {2, "b"})));
    CHECK(insert(&party, 0, ((member) {3, "c"})));
    CHECK_EQ(party.len, 3);
    CHECK_EQ(get(party, 0).hp, 3);
    CHECK_EQ(last(party).hp, 2);
    CHECK(push(&party, ((member) {4, "d"})));
    CHECK(!push(&party, ((member) {5, "e"})));
    CHECK(!insert(&party, 1, ((member) {5, "e"})));
    CHECK_EQ(party.len, 4);
    get(party, 1).hp = 9;
    CHECK_EQ(get_ptr(party, 1)->hp, 9);
    CHECK(data(party) == &party.elems[0]);

    member removed = remove_at(&party, 1);

    CHECK_EQ(removed.hp, 9);
    CHECK_EQ(party.len, 3);
    CHECK_EQ(get(party, 1).hp, 2);
    removed = remove_swap(&party, 0);
    CHECK_EQ(removed.hp, 3);
    CHECK_EQ(party.len, 2);
    CHECK_EQ(get(party, 0).hp, 4);
    removed = pop(&party);
    CHECK_EQ(removed.hp, 2);
    CHECK_EQ(party.len, 1);
    clear(&party);
    CHECK_EQ(party.len, 0);

    // lists grow in an arena; conversions by field see the same elements
    static alignas(16) u8 mem[4096];
    arena region = {.base = mem, .size = sizeof mem, .committed = sizeof mem};
    member_list units = {};

    for (int i = 0; i < 20; i++)
        CHECK(push(&region, &units, ((member) {i, "u"})));

    CHECK_EQ(units.len, 20);
    CHECK_EQ(units.cap, 32);
    CHECK_EQ(get(units, 19).hp, 19);
    CHECK_EQ(heal(units.slice), 20);
    CHECK_EQ(total(units.span), 190 + 200);
    CHECK_EQ(total(units.slice.span), 390);
    CHECK_EQ(total(as_span(member, party)), 0);
    push(&party, ((member) {7, "x"}));
    CHECK_EQ(heal(as_slice(member, party)), 1);
    CHECK_EQ(get(party, 0).hp, 17);

    // 8 on the first push, then doubling in place while the list is at the top
    u32_list grow = {};

    push(&region, &grow, 1u);
    CHECK_EQ(grow.cap, 8);

    u32* before = grow.elems;

    for (u32 i = 0; i < 40; i++)
        push(&region, &grow, i);

    CHECK(grow.elems == before);
    CHECK_EQ(grow.cap, 64);

    // two lists growing in turn move to the top and stay correct
    u32_list evens = {}, odds = {};

    for (u32 i = 0; i < 100; i++) {
        CHECK(push(&region, &evens, i * 2));
        CHECK(push(&region, &odds, i * 2 + 1));
    }

    for (u32 i = 0; i < 100; i++) {
        CHECK_EQ(get(evens, i), i * 2);
        CHECK_EQ(get(odds, i), i * 2 + 1);
    }

    CHECK(insert(&region, &evens, 0, 7u));
    CHECK_EQ(get(evens, 0), 7);
    CHECK_EQ(get(evens, 1), 0);
    CHECK_EQ(evens.len, 101);

    // exhaustion: false, the list unchanged, failed set until a clear
    arena tiny = {.base = mem, .size = 48, .committed = 48};
    u32_list full = {};

    for (int i = 0; i < 8; i++)
        CHECK(push(&tiny, &full, 1u));

    CHECK(!push(&tiny, &full, 2u));
    CHECK_EQ(full.len, 8);
    CHECK(tiny.failed);
    arena_clear(&tiny);
    CHECK(!tiny.failed);
    CHECK_EQ(tiny.used, 0);
    CHECK(alloc(&tiny, u8, 100) == nullptr);
    CHECK(tiny.failed);
    CHECK(alloc(&tiny, u64, SIZE_MAX / 4) == nullptr);

    // a zero-count allocation from an arena with a base is a valid, empty block
    arena roomy = {.base = mem, .size = sizeof mem, .committed = sizeof mem};

    CHECK(alloc(&roomy, u8, 0) != nullptr);
    CHECK(!roomy.failed);

    // a fixed arena with uncommitted bytes fails the commit without hooks
    arena uncommitted = {.base = mem, .size = sizeof mem, .committed = 16};

    CHECK(alloc(&uncommitted, u8, 32) == nullptr);
    CHECK(uncommitted.refused);

    // find: -1 when missing, the first of duplicates, pointers, and handles
    CHECK_EQ(find(evens, 7u), 0);
    CHECK_EQ(find(evens, 198u), 100);
    CHECK_EQ(find(evens, 12345u), -1);
    push(&region, &evens, 7u);
    CHECK_EQ(find(evens, 7u), 0);

    test_handle_list handles = {};

    push(&region, &handles, ((test_handle) {5}));
    push(&region, &handles, ((test_handle) {6}));
    CHECK_EQ(find(handles, ((test_handle) {6})), 1);

    char* words[] = {"a", "b"};
    text_list texts = {};

    push(&region, &texts, words[0]);
    push(&region, &texts, words[1]);
    CHECK_EQ(find(texts, words[1]), 1);

    // sub keeps the type; a list from sub copies on its first push
    u32_slice part = sub(evens.slice, 1, 4);

    CHECK_EQ(part.len, 3);
    CHECK_EQ(get(part, 0), 0);
    CHECK_EQ(get(part, 2), 4);
    CHECK_EQ(sub(evens.span, 2, 2).len, 0);

    u32_list copy = sub(evens, 0, 2);

    CHECK_EQ(copy.cap, 2);
    CHECK(push(&region, &copy, 5u));
    CHECK(copy.elems != evens.elems);
    CHECK_EQ(get(evens, 2), 2);

    // the region, index, value, from, and to arguments run once
    test_hits = 0;
    push(&party, ((member) {counted(1), "z"}));
    push(counted_arena(&region), &units, ((member) {counted(1), "z"}));
    (void)get(units, counted(0));
    (void)sub(units.slice, counted(0), counted(1));
    (void)find(evens, (u32)counted(3));
    (void)insert(counted_arena(&region), &evens, counted(0), (u32)counted(1));
    (void)remove_at(&evens, counted(0));
    CHECK_EQ(test_hits, 11);

    // traps
    u32_list empty = {};

    CHECK_TRAP(get(evens, evens.len), "get: index 102 past len 102");
    CHECK_TRAP(get(evens, -1), "get: index 18446744073709551615 past len 102");
    CHECK_TRAP(pop(&empty), "pop: empty");
    CHECK_TRAP(last(empty), "last: empty");
    CHECK_TRAP(insert(&region, &evens, 500, 1u), "insert: index 500 past len 102");
    CHECK_TRAP(sub(evens.slice, 3, 2), "sub: [3, 2) outside len 102");
    CHECK_TRAP(remove_at(&evens, 200), "remove_at: index 200 past len 102");

    // arena_new: the name is copied and cut to 23 bytes, the committed part is
    // inherited, and a parent that cannot fit it fails cleanly
    arena parent = {.base = mem, .size = sizeof mem, .committed = 100};
    arena child;

    CHECK(arena_new(&child, &parent, "a name longer than twenty-three bytes", 200));
    CHECK(strcmp(child.name, "a name longer than twen") == 0);
    CHECK_EQ(child.committed, 100);
    CHECK_EQ(child.size, 200);
    CHECK(!arena_new(&child, &parent, "big", 1 << 20));
    CHECK_EQ(child.size, 0);
    CHECK(parent.failed);

    return 0;
}

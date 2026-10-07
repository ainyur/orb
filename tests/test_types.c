#include "test.h"

#include "../src/orb_types.h"

typedef struct member {
    int hp;
    char name[3];
} member;

orb_span(u8);
orb_slice(u8);
orb_list(u8);
orb_list(u32);
orb_span(member);
orb_slice(member);
orb_list(member);
orb_array(member, 4);
orb_array(stock, int, 2 * 3);

// a repeat definition, in either form, is the same type
orb_list(u32);
orb_list(u32, u32);
orb_span(member, member);

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

static int test_sizes(void) {
    CHECK_EQ(sizeof(u8_list), 16);
    CHECK_EQ(sizeof(u8_slice), 16);
    CHECK_EQ(sizeof(u8_span), 16);
    CHECK_EQ(sizeof(member_array), 4 * sizeof(member) + 4);
    CHECK_EQ(sizeof(stock_array), 6 * sizeof(int) + 4);
    return 0;
}

static int test_views(void) {
    static member storage[8];
    member_list units = {.elems = storage, .cap = 8};

    for (int i = 0; i < 8; i++)
        units.elems[units.len++] = (member) {i, "u"};

    CHECK_EQ(heal((member_slice) {.elems = units.elems, .len = units.len}), 8);
    CHECK_EQ(units.elems[7].hp, 17);
    CHECK_EQ(total((member_span) {.elems = units.elems, .len = units.len}), 108);

    member_slice part = {.elems = units.elems + 2, .len = 3};

    CHECK_EQ(total((member_span) {.elems = part.elems, .len = part.len}), 12 + 13 + 14);

    member_array party = {};

    CHECK_EQ(total((member_span) {.elems = party.elems, .len = party.len}), 0);

    if (party.len < 4) party.elems[party.len++] = (member) {7, "x"};

    CHECK_EQ(party.len, 1);
    CHECK_EQ(heal((member_slice) {.elems = party.elems, .len = party.len}), 1);
    CHECK_EQ(party.elems[0].hp, 17);
    CHECK_EQ(total((member_span) {.elems = party.elems, .len = party.len}), 17);
    return 0;
}

static int test_repeats(void) {
    u32_list numbers = {};
    member_span view = {};

    static_assert(_Generic(numbers, u32_list: true, default: false));
    static_assert(_Generic(view, member_span: true, default: false));
    CHECK_EQ(numbers.len + view.len, 0);
    return 0;
}

int main(void) {
    CHECK_EQ(test_sizes(), 0);
    CHECK_EQ(test_views(), 0);
    CHECK_EQ(test_repeats(), 0);
    return 0;
}

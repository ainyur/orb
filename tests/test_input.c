#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    // the default binding, through the headless layout
    orb_input_resolve(&(orb_assets) {0});

    // edges across two snapshots, by key
    orb_input input = {0};

    orb_input_step(&input);
    input.keys[ORB_KEY_M] = true;
    orb_input_step(&input);
    CHECK(orb_key_down(ORB_KEY_M));
    CHECK(orb_key_pressed(ORB_KEY_M));
    CHECK(!orb_key_released(ORB_KEY_M));
    orb_input_step(&input);
    CHECK(orb_key_down(ORB_KEY_M));
    CHECK(!orb_key_pressed(ORB_KEY_M));
    input.keys[ORB_KEY_M] = false;
    orb_input_step(&input);
    CHECK(!orb_key_down(ORB_KEY_M));
    CHECK(orb_key_released(ORB_KEY_M));

    // buttons go through the default binding
    input.keys[ORB_KEY_Z] = true;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(orb_button_pressed(ORB_BTN_A));
    CHECK_EQ(orb_button_source(ORB_BTN_A), ORB_KEY_Z);
    CHECK_EQ(orb_button_source(ORB_BTN_SELECT), ORB_KEY_TAB);

    // rebinding: M drives select, and two buttons may share a source
    orb_button_bind(ORB_BTN_SELECT, ORB_KEY_M);
    orb_button_bind(ORB_BTN_START, ORB_KEY_M);
    input.keys[ORB_KEY_M] = true;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_SELECT));
    CHECK(orb_button_down(ORB_BTN_START));
    CHECK_EQ(orb_button_source(ORB_BTN_SELECT), ORB_KEY_M);

    // an unbound button is never down
    orb_button_bind(ORB_BTN_SELECT, ORB_SOURCE_NONE);
    orb_input_step(&input);
    CHECK(!orb_button_down(ORB_BTN_SELECT));
    CHECK(!orb_button_released(ORB_BTN_SELECT));

    // capture: the lowest position that went down this tick
    input = (orb_input) {0};
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_NONE);
    input.keys[ORB_KEY_X] = true;
    input.keys[ORB_KEY_C] = true;
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_C); // 6 before 27
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_NONE); // still down, not new

    // out of range is a no-op
    CHECK(!orb_key_down(0));
    CHECK(!orb_key_down(ORB_KEY_COUNT));
    CHECK(!orb_key_down(-1));
    CHECK(!orb_key_pressed(ORB_KEY_COUNT));
    CHECK(!orb_key_released(-1));
    CHECK(!orb_button_down(ORB_BTN_COUNT));
    CHECK(!orb_button_pressed(-1));
    CHECK(!orb_button_released(ORB_BTN_COUNT));
    CHECK_EQ(orb_button_source(ORB_BTN_COUNT), ORB_SOURCE_NONE);
    orb_button_bind(ORB_BTN_A, ORB_SOURCE_PAD);
    CHECK_EQ(orb_button_source(ORB_BTN_A), ORB_KEY_Z);
    orb_button_bind(ORB_BTN_A, ORB_KEY_COUNT);
    CHECK_EQ(orb_button_source(ORB_BTN_A), ORB_KEY_Z);
    orb_button_bind(ORB_BTN_COUNT, ORB_KEY_M);
    CHECK_EQ(orb_button_source(ORB_BTN_A), ORB_KEY_Z);

    // names: orb's for named keys, the layout's for printable ones, "" otherwise
    CHECK(strcmp(orb_key_name(ORB_KEY_TAB), "tab") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_PAGE_UP), "page_up") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_LEFT_SHIFT), "left_shift") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_KP_PERIOD), "kp_period") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_M), "m") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_COMMA), ",") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_0), "0") == 0);
    CHECK(strcmp(orb_key_name(3), "") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_COUNT), "") == 0);
    CHECK(strcmp(orb_key_name(ORB_KEY_NONE), "") == 0);

    // symbols: a name, or one codepoint above space
    CHECK(orb_input_symbol_valid("tab"));
    CHECK(orb_input_symbol_valid("m"));
    CHECK(orb_input_symbol_valid("é"));
    CHECK(!orb_input_symbol_valid(""));
    CHECK(!orb_input_symbol_valid(" "));
    CHECK(!orb_input_symbol_valid("mm"));
    CHECK(!orb_input_symbol_valid("meta"));
    CHECK(!orb_input_symbol_valid("\xc1\xbf"));     // overlong
    CHECK(!orb_input_symbol_valid("\xed\xa0\x80")); // surrogate
    CHECK_EQ(orb_input_symbol_position("tab"), ORB_KEY_TAB);
    CHECK_EQ(orb_input_symbol_position("m"), ORB_KEY_M);
    CHECK_EQ(orb_input_symbol_position(","), ORB_KEY_COMMA);
    CHECK_EQ(orb_input_symbol_position("é"), ORB_KEY_NONE); // not on the US layout
    CHECK_EQ(orb_input_symbol_position("meta"), ORB_KEY_NONE);
    CHECK_EQ(orb_input_symbol_position("M"), ORB_KEY_NONE); // shifted: not the unshifted symbol

    // resolving: names and symbols become positions, a missing symbol unbinds, zero
    // records restore the default
    orb_binding_desc records[ORB_BTN_COUNT] = {};

    for (int i = 0; i < ORB_BTN_COUNT; i++)
        snprintf(records[i].symbol, sizeof records[i].symbol, "%s", orb_button_defaults[i]);

    snprintf(records[ORB_BTN_SELECT].symbol, sizeof records[0].symbol, "m");
    snprintf(records[ORB_BTN_START].symbol, sizeof records[0].symbol, "é");
    orb_input_resolve(&(orb_assets) {.bindings = records, .binding_count = ORB_BTN_COUNT});
    CHECK_EQ(orb_button_source(ORB_BTN_SELECT), ORB_KEY_M);
    CHECK_EQ(orb_button_source(ORB_BTN_START), ORB_SOURCE_NONE);
    CHECK_EQ(orb_button_source(ORB_BTN_UP), ORB_KEY_UP);
    CHECK_EQ(orb_button_source(ORB_BTN_A), ORB_KEY_Z);
    orb_input_resolve(&(orb_assets) {0});
    CHECK_EQ(orb_button_source(ORB_BTN_SELECT), ORB_KEY_TAB);
    CHECK_EQ(orb_button_source(ORB_BTN_START), ORB_KEY_RETURN);

    return 0;
}

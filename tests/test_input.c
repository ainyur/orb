#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static bool close_to(float value, float want) {
    return value - want < 0.001f && want - value < 0.001f;
}

static bool* pad_button(orb_input* input, orb_pad pad) {
    return &input->pad.buttons[pad - ORB_PAD_NONE];
}

int main(void) {
    // the default binding, through the headless layout
    orb_input_boot();

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

    // the defaults: a key and a pad button per button
    CHECK_EQ(orb_button_key(ORB_BTN_UP), ORB_KEY_UP);
    CHECK_EQ(orb_button_key(ORB_BTN_A), ORB_KEY_Z);
    CHECK_EQ(orb_button_key(ORB_BTN_SELECT), ORB_KEY_TAB);
    CHECK_EQ(orb_button_pad(ORB_BTN_UP), ORB_PAD_UP);
    CHECK_EQ(orb_button_pad(ORB_BTN_RIGHT), ORB_PAD_RIGHT);
    CHECK_EQ(orb_button_pad(ORB_BTN_A), ORB_PAD_SOUTH);
    CHECK_EQ(orb_button_pad(ORB_BTN_B), ORB_PAD_EAST);
    CHECK_EQ(orb_button_pad(ORB_BTN_X), ORB_PAD_WEST);
    CHECK_EQ(orb_button_pad(ORB_BTN_Y), ORB_PAD_NORTH);
    CHECK_EQ(orb_button_pad(ORB_BTN_L), ORB_PAD_LEFT_SHOULDER);
    CHECK_EQ(orb_button_pad(ORB_BTN_R), ORB_PAD_RIGHT_SHOULDER);
    CHECK_EQ(orb_button_pad(ORB_BTN_START), ORB_PAD_START);
    CHECK_EQ(orb_button_pad(ORB_BTN_SELECT), ORB_PAD_BACK);

    // a button goes down through either slot
    input.keys[ORB_KEY_Z] = true;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(orb_button_pressed(ORB_BTN_A));
    input = (orb_input) {0};
    orb_input_step(&input);
    CHECK(orb_button_released(ORB_BTN_A));
    *pad_button(&input, ORB_PAD_SOUTH) = true;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(orb_button_pressed(ORB_BTN_A));
    CHECK(orb_pad_down(ORB_PAD_SOUTH));
    CHECK(orb_pad_pressed(ORB_PAD_SOUTH));
    CHECK(!orb_pad_released(ORB_PAD_SOUTH));

    // one level over both slots: the key joining and leaving while the pad holds is no edge
    input.keys[ORB_KEY_Z] = true;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(!orb_button_pressed(ORB_BTN_A));
    CHECK(!orb_pad_pressed(ORB_PAD_SOUTH));
    input.keys[ORB_KEY_Z] = false;
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_A));
    CHECK(!orb_button_released(ORB_BTN_A));

    // a pad unplugged with a button held reads as a release, and nothing stays down
    input = (orb_input) {0};
    orb_input_step(&input);
    CHECK(orb_button_released(ORB_BTN_A));
    CHECK(orb_pad_released(ORB_PAD_SOUTH));
    orb_input_step(&input);
    CHECK(!orb_button_down(ORB_BTN_A));
    CHECK(!orb_button_released(ORB_BTN_A));
    CHECK(!orb_pad_released(ORB_PAD_SOUTH));

    // rebinding: each source goes to its own slot, and each NONE empties only its own
    orb_button_bind(ORB_BTN_SELECT, ORB_KEY_M);
    orb_button_bind(ORB_BTN_SELECT, ORB_PAD_NORTH);
    CHECK_EQ(orb_button_key(ORB_BTN_SELECT), ORB_KEY_M);
    CHECK_EQ(orb_button_pad(ORB_BTN_SELECT), ORB_PAD_NORTH);
    orb_button_bind(ORB_BTN_SELECT, ORB_PAD_NONE);
    CHECK_EQ(orb_button_key(ORB_BTN_SELECT), ORB_KEY_M);
    CHECK_EQ(orb_button_pad(ORB_BTN_SELECT), ORB_PAD_NONE);
    orb_button_bind(ORB_BTN_SELECT, ORB_KEY_NONE);
    CHECK_EQ(orb_button_key(ORB_BTN_SELECT), ORB_KEY_NONE);
    CHECK_EQ(orb_button_pad(ORB_BTN_SELECT), ORB_PAD_NONE);

    // an empty button is never down, whatever is held
    input.keys[ORB_KEY_M] = true;
    input.keys[ORB_KEY_TAB] = true;
    *pad_button(&input, ORB_PAD_BACK) = true;
    orb_input_step(&input);
    CHECK(!orb_button_down(ORB_BTN_SELECT));

    // two buttons may share a source
    orb_button_bind(ORB_BTN_START, ORB_PAD_BACK);
    orb_button_bind(ORB_BTN_L, ORB_PAD_BACK);
    orb_input_step(&input);
    CHECK(orb_button_down(ORB_BTN_START));
    CHECK(orb_button_down(ORB_BTN_L));

    // capture: the lowest position and the lowest pad button that went down this tick
    input = (orb_input) {0};
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_NONE);
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_NONE);
    input.keys[ORB_KEY_X] = true;
    input.keys[ORB_KEY_C] = true;
    *pad_button(&input, ORB_PAD_START) = true;
    *pad_button(&input, ORB_PAD_EAST) = true;
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_C);    // 6 before 27
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_EAST); // before start
    orb_input_step(&input);
    CHECK_EQ(orb_key_pressed_any(), ORB_KEY_NONE); // still down, not new
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_NONE);

    // triggers read as buttons from half their travel
    input = (orb_input) {0};
    input.pad.left_trigger = 16383;
    input.pad.right_trigger = 16384;
    orb_input_step(&input);
    CHECK(!orb_pad_down(ORB_PAD_LEFT_TRIGGER));
    CHECK(orb_pad_down(ORB_PAD_RIGHT_TRIGGER));
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_RIGHT_TRIGGER);

    input = (orb_input) {0};
    input.pad.left_stick.x = 32767;
    input.pad.left_stick.y = -32767;
    input.pad.right_stick.x = -32767;
    orb_input_step(&input);
    CHECK(!orb_pad_down(ORB_PAD_RIGHT));
    CHECK(!orb_pad_down(ORB_PAD_UP));
    CHECK(!orb_pad_down(ORB_PAD_LEFT));
    CHECK(!orb_button_down(ORB_BTN_RIGHT));
    CHECK(!orb_button_down(ORB_BTN_UP));
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_NONE);

    // a worn stick resting off center reads as centered
    input = (orb_input) {0};
    input.pad.left_stick.x = -4141;
    input.pad.left_stick.y = 3000;
    orb_input_step(&input);
    CHECK(orb_pad_stick(ORB_PAD_LEFT_STICK).x == 0);
    CHECK(orb_pad_stick(ORB_PAD_LEFT_STICK).y == 0);

    // the dead zone: zero through 7849, rising from just past it to 1 at full, y down
    input = (orb_input) {0};
    input.pad.left_stick.x = 7849;
    orb_input_step(&input);
    CHECK(orb_pad_stick(ORB_PAD_LEFT_STICK).x == 0);
    input.pad.left_stick.x = 7850;
    orb_input_step(&input);
    CHECK(orb_pad_stick(ORB_PAD_LEFT_STICK).x > 0);
    CHECK(orb_pad_stick(ORB_PAD_LEFT_STICK).x < 0.001f);
    input.pad.left_stick.x = 32767;
    input.pad.right_stick.y = -32767;
    orb_input_step(&input);
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).x, 1));
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).y, 0));
    CHECK(close_to(orb_pad_stick(ORB_PAD_RIGHT_STICK).x, 0));
    CHECK(close_to(orb_pad_stick(ORB_PAD_RIGHT_STICK).y, -1));
    input.pad.left_stick.y = 32767;
    orb_input_step(&input);
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).x, 0.7071f)); // length 1 on a diagonal
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).y, 0.7071f));
    input.pad.left_stick.x = -32768; // past the backends' range, without overflow
    input.pad.left_stick.y = -32768;
    orb_input_step(&input);
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).x, -0.7071f));
    CHECK(close_to(orb_pad_stick(ORB_PAD_LEFT_STICK).y, -0.7071f));

    // triggers: zero through 3855, 1 at full
    input.pad.left_trigger = 3855;
    input.pad.right_trigger = 32767;
    orb_input_step(&input);
    CHECK(orb_pad_trigger(ORB_PAD_LEFT_TRIGGER) == 0);
    CHECK(close_to(orb_pad_trigger(ORB_PAD_RIGHT_TRIGGER), 1));

    // the make passes through
    CHECK_EQ(orb_input_pad_make(), ORB_PAD_MAKE_NONE);
    input.pad.make = ORB_PAD_MAKE_PLAYSTATION;
    orb_input_step(&input);
    CHECK_EQ(orb_input_pad_make(), ORB_PAD_MAKE_PLAYSTATION);

    // out of range is a no-op
    CHECK(!orb_key_down(0));
    CHECK(!orb_key_down(ORB_KEY_COUNT));
    CHECK(!orb_key_down(-1));
    CHECK(!orb_key_pressed(ORB_KEY_COUNT));
    CHECK(!orb_key_released(-1));
    CHECK(!orb_pad_down(ORB_PAD_NONE));
    CHECK(!orb_pad_down(ORB_PAD_END));
    CHECK(!orb_pad_pressed(ORB_PAD_END));
    CHECK(!orb_pad_released(ORB_PAD_NONE));
    CHECK(!orb_button_down(ORB_BTN_COUNT));
    CHECK(!orb_button_pressed(-1));
    CHECK(!orb_button_released(ORB_BTN_COUNT));
    CHECK_EQ(orb_button_key(ORB_BTN_COUNT), ORB_KEY_NONE);
    CHECK_EQ(orb_button_pad(-1), ORB_PAD_NONE);
    CHECK(orb_pad_stick(ORB_PAD_RIGHT_TRIGGER).x == 0); // not a stick
    CHECK(orb_pad_stick(ORB_PAD_RIGHT_TRIGGER).y == 0);
    CHECK(orb_pad_trigger(ORB_PAD_LEFT_STICK) == 0); // not a trigger
    orb_button_bind(ORB_BTN_A, ORB_KEY_COUNT);
    orb_button_bind(ORB_BTN_A, 511);
    orb_button_bind(ORB_BTN_A, ORB_PAD_END);
    orb_button_bind(ORB_BTN_A, -1);
    orb_button_bind(ORB_BTN_COUNT, ORB_KEY_M);
    orb_button_bind(-1, ORB_PAD_EAST);
    CHECK_EQ(orb_button_key(ORB_BTN_A), ORB_KEY_Z);
    CHECK_EQ(orb_button_pad(ORB_BTN_A), ORB_PAD_SOUTH);

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

    // key_find: a name, or one codepoint the layout produces unshifted
    CHECK_EQ(orb_key_find("tab"), ORB_KEY_TAB);
    CHECK_EQ(orb_key_find("m"), ORB_KEY_M);
    CHECK_EQ(orb_key_find(","), ORB_KEY_COMMA);
    CHECK_EQ(orb_key_find("é"), ORB_KEY_NONE); // not on the US layout
    CHECK_EQ(orb_key_find("M"), ORB_KEY_NONE); // shifted: not the unshifted symbol
    CHECK_EQ(orb_key_find("meta"), ORB_KEY_NONE);
    CHECK_EQ(orb_key_find(""), ORB_KEY_NONE);
    CHECK_EQ(orb_key_find(" "), ORB_KEY_NONE);
    CHECK_EQ(orb_key_find("mm"), ORB_KEY_NONE);
    CHECK_EQ(orb_key_find("\xc1\xbf"), ORB_KEY_NONE);     // overlong
    CHECK_EQ(orb_key_find("\xed\xa0\x80"), ORB_KEY_NONE); // surrogate

    // boot restores both slots' defaults and empties both snapshots
    orb_button_bind(ORB_BTN_SELECT, ORB_KEY_M);
    orb_button_bind(ORB_BTN_SELECT, ORB_PAD_NORTH);
    input = (orb_input) {0};
    input.keys[ORB_KEY_M] = true;
    orb_input_step(&input);
    orb_input_boot();
    CHECK(!orb_key_down(ORB_KEY_M));
    CHECK(!orb_key_released(ORB_KEY_M));
    CHECK_EQ(orb_button_key(ORB_BTN_SELECT), ORB_KEY_TAB);
    CHECK_EQ(orb_button_pad(ORB_BTN_SELECT), ORB_PAD_BACK);
    CHECK_EQ(orb_button_key(ORB_BTN_START), ORB_KEY_RETURN);
    CHECK_EQ(orb_button_pad(ORB_BTN_START), ORB_PAD_START);

    return 0;
}

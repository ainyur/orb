#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static bool close_to(float value, float want) {
    return value - want < 0.001f && want - value < 0.001f;
}

static bool* pad_button(orb_input* input, orb_pad pad) {
    return &input->pad.buttons[pad - ORB_PAD_NONE];
}

enum { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8 };

static int dpad_down(void) {
    int down = 0;

    for (int pad = ORB_PAD_UP; pad <= ORB_PAD_RIGHT; pad++)
        if (orb_pad_down(pad)) down |= 1 << (pad - ORB_PAD_UP);

    return down;
}

// Steps with only the left stick at x, y.
static int stick_step(int x, int y) {
    orb_input input = {0};

    input.pad.left_stick.x = (int16_t)x;
    input.pad.left_stick.y = (int16_t)y;
    orb_input_step(&input);
    return dpad_down();
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

    // the left stick as the d-pad: pressed from half its length, with the usual edges
    orb_pad_stick_dpad(true);
    CHECK_EQ(stick_step(16383, 0), 0);
    CHECK_EQ(stick_step(16384, 0), RIGHT);
    CHECK(orb_pad_pressed(ORB_PAD_RIGHT));
    CHECK(orb_button_pressed(ORB_BTN_RIGHT));
    CHECK_EQ(orb_pad_pressed_any(), ORB_PAD_RIGHT);
    CHECK_EQ(stick_step(16384, 0), RIGHT);
    CHECK(!orb_pad_pressed(ORB_PAD_RIGHT));
    CHECK_EQ(stick_step(0, 0), 0);
    CHECK(orb_pad_released(ORB_PAD_RIGHT));
    CHECK(orb_button_released(ORB_BTN_RIGHT));
    CHECK_EQ(stick_step(11585, 11585), 0);            // length 16383
    CHECK_EQ(stick_step(11586, 11586), DOWN | RIGHT); // length 16385
    CHECK_EQ(stick_step(0, 0), 0);

    // hysteresis: held down to 11468, following the sector, and pressed again only from half
    CHECK_EQ(stick_step(0, -16384), UP);
    CHECK_EQ(stick_step(0, -11468), UP);
    CHECK(!orb_pad_pressed(ORB_PAD_UP));
    CHECK_EQ(stick_step(-12000, 0), LEFT);
    CHECK(orb_pad_released(ORB_PAD_UP));
    CHECK(orb_pad_pressed(ORB_PAD_LEFT));
    CHECK_EQ(stick_step(0, -16383), UP);
    CHECK_EQ(stick_step(0, -11467), 0);
    CHECK(orb_pad_released(ORB_PAD_UP));
    CHECK_EQ(stick_step(0, -16383), 0);
    CHECK_EQ(stick_step(0, -16384), UP);

    // eight sectors of 45°: each direction spans three, so a diagonal holds two
    CHECK_EQ(stick_step(32767, 0), RIGHT);
    CHECK_EQ(stick_step(23170, 23170), DOWN | RIGHT);
    CHECK_EQ(stick_step(0, 32767), DOWN);
    CHECK_EQ(stick_step(-23170, 23170), DOWN | LEFT);
    CHECK_EQ(stick_step(-32767, 0), LEFT);
    CHECK_EQ(stick_step(-23170, -23170), UP | LEFT);
    CHECK_EQ(stick_step(0, -32767), UP);
    CHECK_EQ(stick_step(23170, -23170), UP | RIGHT);
    CHECK_EQ(stick_step(-32768, -32768), UP | LEFT); // past the backends' range

    // the boundaries at 22.5° and 67.5°: 24000 × tan 22.5° is 9941.1
    CHECK_EQ(stick_step(24000, 9941), RIGHT);
    CHECK_EQ(stick_step(24000, 9942), DOWN | RIGHT);
    CHECK_EQ(stick_step(9942, 24000), DOWN | RIGHT);
    CHECK_EQ(stick_step(9941, 24000), DOWN);
    CHECK_EQ(stick_step(-24000, -9941), LEFT);
    CHECK_EQ(stick_step(-24000, -9942), UP | LEFT);
    CHECK_EQ(stick_step(-9942, -24000), UP | LEFT);
    CHECK_EQ(stick_step(-9941, -24000), UP);

    // the stick and the d-pad make one level per direction: no edge until both let go
    input = (orb_input) {0};
    input.pad.left_stick.x = 32767;
    orb_input_step(&input);
    *pad_button(&input, ORB_PAD_RIGHT) = true;
    orb_input_step(&input);
    CHECK(orb_pad_down(ORB_PAD_RIGHT));
    CHECK(!orb_pad_pressed(ORB_PAD_RIGHT));
    CHECK(!orb_button_pressed(ORB_BTN_RIGHT));
    input.pad.left_stick.x = 0;
    orb_input_step(&input);
    CHECK(orb_pad_down(ORB_PAD_RIGHT));
    CHECK(!orb_pad_released(ORB_PAD_RIGHT));
    CHECK(!orb_button_released(ORB_BTN_RIGHT));
    input.pad.left_stick.y = -32767;
    orb_input_step(&input);
    CHECK(orb_pad_down(ORB_PAD_RIGHT));
    CHECK(orb_pad_pressed(ORB_PAD_UP));
    *pad_button(&input, ORB_PAD_RIGHT) = false;
    orb_input_step(&input);
    CHECK(orb_pad_released(ORB_PAD_RIGHT));
    CHECK(orb_pad_down(ORB_PAD_UP));

    for (int pad = ORB_PAD_UP; pad <= ORB_PAD_RIGHT; pad++)
        *pad_button(&input, pad) = true;

    orb_input_step(&input);
    CHECK_EQ(dpad_down(), UP | DOWN | LEFT | RIGHT);
    input.pad.left_stick.y = 32767;
    orb_input_step(&input);
    CHECK_EQ(dpad_down(), UP | DOWN | LEFT | RIGHT);

    // switched off, the stick presses nothing, and switched back on it starts released
    orb_pad_stick_dpad(false);
    CHECK_EQ(stick_step(32767, 0), 0);
    orb_pad_stick_dpad(true);
    CHECK_EQ(stick_step(12000, 0), 0);
    orb_pad_stick_dpad(false);

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

    // boot restores both slots' defaults, turns the stick d-pad off, and empties both snapshots
    orb_button_bind(ORB_BTN_SELECT, ORB_KEY_M);
    orb_button_bind(ORB_BTN_SELECT, ORB_PAD_NORTH);
    orb_pad_stick_dpad(true);
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
    CHECK_EQ(stick_step(32767, 0), 0);

    return 0;
}

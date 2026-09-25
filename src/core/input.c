#include "input.h"

#include "../os/os.h"
#include "bytes.h"
#include "log.h"
#include "macros.h"

#include <stdlib.h>
#include <string.h>

constexpr int INPUT_AXIS_MAX = 32767;
constexpr int INPUT_HALF = 16384; // a trigger this far down, or a stick this far out, is a button
constexpr int INPUT_STICK_DEAD = 7849;
constexpr int INPUT_STICK_OFF = 11468; // a stick holding the d-pad lets go under this
constexpr int INPUT_TRIGGER_DEAD = 3855;

static const char* const input_button_names[ORB_BTN_COUNT] = {"up", "down", "left",  "right",
                                                              "a",  "b",    "x",     "y",
                                                              "l",  "r",    "start", "select"};
static const char* const input_key_defaults[ORB_BTN_COUNT] = {"up", "down", "left",   "right",
                                                              "z",  "x",    "a",      "s",
                                                              "q",  "w",    "return", "tab"};
static const orb_pad input_pad_defaults[ORB_BTN_COUNT] = {
    ORB_PAD_UP,    ORB_PAD_DOWN, ORB_PAD_LEFT,  ORB_PAD_RIGHT,         ORB_PAD_SOUTH,
    ORB_PAD_EAST,  ORB_PAD_WEST, ORB_PAD_NORTH, ORB_PAD_LEFT_SHOULDER, ORB_PAD_RIGHT_SHOULDER,
    ORB_PAD_START, ORB_PAD_BACK,
};

// Named keys hold their position on every layout; the enumerator, lowercased.
static const char* const input_names[ORB_KEY_COUNT] = {
    [ORB_KEY_RETURN] = "return",
    [ORB_KEY_ESCAPE] = "escape",
    [ORB_KEY_BACKSPACE] = "backspace",
    [ORB_KEY_TAB] = "tab",
    [ORB_KEY_SPACE] = "space",
    [ORB_KEY_CAPS_LOCK] = "caps_lock",
    [ORB_KEY_F1] = "f1",
    "f2",
    "f3",
    "f4",
    "f5",
    "f6",
    "f7",
    "f8",
    "f9",
    "f10",
    "f11",
    "f12",
    [ORB_KEY_PRINT_SCREEN] = "print_screen",
    "scroll_lock",
    "pause",
    "insert",
    "home",
    "page_up",
    "delete",
    "end",
    "page_down",
    "right",
    "left",
    "down",
    "up",
    "num_lock",
    "kp_divide",
    "kp_multiply",
    "kp_minus",
    "kp_plus",
    "kp_enter",
    "kp_1",
    "kp_2",
    "kp_3",
    "kp_4",
    "kp_5",
    "kp_6",
    "kp_7",
    "kp_8",
    "kp_9",
    "kp_0",
    "kp_period",
    [ORB_KEY_NON_US_BACKSLASH] = "non_us_backslash",
    "application",
    [ORB_KEY_LEFT_CTRL] = "left_ctrl",
    "left_shift",
    "left_alt",
    "left_gui",
    "right_ctrl",
    "right_shift",
    "right_alt",
    "right_gui",
};

static orb_input input_now, input_before;
static int input_keys[ORB_BTN_COUNT]; // both slots set by orb_input_boot before any query
static orb_pad input_pads[ORB_BTN_COUNT];
static bool input_stick_dpad, input_stick_held;
static char input_name[8]; // orb_key_name's buffer for a printable key's UTF-8

static bool input_key_ok(int key) {
    return key > ORB_KEY_NONE && key < ORB_KEY_COUNT;
}

static bool input_pad_ok(int pad) {
    return pad > ORB_PAD_NONE && pad < ORB_PAD_END;
}

static bool input_button_ok(orb_button button) {
    return button >= 0 && button < ORB_BTN_COUNT;
}

// An empty slot's source is neither a key nor a pad button, so it is never down.
static bool input_source_down(const orb_input* input, int source) {
    if (input_key_ok(source)) return input->keys[source];
    if (input_pad_ok(source)) return input->pad.buttons[source - ORB_PAD_NONE];

    return false;
}

static bool input_button_level(const orb_input* input, orb_button button) {
    return input_source_down(input, input_keys[button]) ||
           input_source_down(input, input_pads[button]);
}

// The integer square root, rounded down.
static uint32_t input_sqrt(uint32_t n) {
    uint32_t root = 0;

    for (uint32_t bit = 1u << 30; bit; bit >>= 2) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }

    return root;
}

static int input_length(int x, int y) {
    return (int)input_sqrt((uint32_t)(x * x) + (uint32_t)(y * y));
}

// Whether a stick points within 67.5° of an axis, past which it leaves that axis's three
// sectors; 985/2378 is tan 22.5°.
static bool input_toward(int along, int across) {
    return along * 2378 > abs(across) * 985;
}

// The left stick holds the d-pad directions of its sector from half its travel until it
// falls under INPUT_STICK_OFF.
static void input_stick_dpad_step(orb_pad_input* pad) {
    int x = pad->left_stick.x;
    int y = pad->left_stick.y;
    int threshold = input_stick_held ? INPUT_STICK_OFF : INPUT_HALF;

    input_stick_held = input_stick_dpad && input_length(x, y) >= threshold;

    if (!input_stick_held) return;

    pad->buttons[ORB_PAD_UP - ORB_PAD_NONE] |= input_toward(-y, x);
    pad->buttons[ORB_PAD_DOWN - ORB_PAD_NONE] |= input_toward(y, x);
    pad->buttons[ORB_PAD_LEFT - ORB_PAD_NONE] |= input_toward(-x, y);
    pad->buttons[ORB_PAD_RIGHT - ORB_PAD_NONE] |= input_toward(x, y);
}

// A symbol is a named key's name, else one codepoint above space; returns the name's
// position, 0 for a lone codepoint (with it in *codepoint), or -1 for neither.
static int input_symbol_parse(const char* symbol, uint32_t* codepoint) {
    for (int key = 1; key < ORB_KEY_COUNT; key++)
        if (input_names[key] && strcmp(input_names[key], symbol) == 0) return key;

    int n = orb_bytes_utf8_decode(symbol, codepoint);

    return n && symbol[n] == 0 && *codepoint > 0x20 ? 0 : -1;
}

bool orb_key_down(int key) {
    return input_key_ok(key) && input_now.keys[key];
}

bool orb_key_pressed(int key) {
    return input_key_ok(key) && input_now.keys[key] && !input_before.keys[key];
}

bool orb_key_released(int key) {
    return input_key_ok(key) && !input_now.keys[key] && input_before.keys[key];
}

int orb_key_pressed_any(void) {
    for (int key = 1; key < ORB_KEY_COUNT; key++)
        if (input_now.keys[key] && !input_before.keys[key]) return key;

    return ORB_KEY_NONE;
}

const char* orb_key_name(int key) {
    if (!input_key_ok(key)) return "";
    if (input_names[key]) return input_names[key];

    uint32_t codepoint = orb_os_key_symbol(key);

    if (!codepoint) return "";

    input_name[orb_bytes_utf8(codepoint, input_name)] = 0;
    return input_name;
}

int orb_key_find(const char* symbol) {
    uint32_t codepoint = 0;
    int key = input_symbol_parse(symbol, &codepoint);

    if (key != 0) return key > 0 ? key : ORB_KEY_NONE;

    // A shifted symbol such as "M" finds nothing: the position must give it back unshifted.
    int position = orb_os_key_position(codepoint);

    return orb_os_key_symbol(position) == codepoint ? position : ORB_KEY_NONE;
}

bool orb_pad_down(orb_pad pad) {
    return input_pad_ok(pad) && input_now.pad.buttons[pad - ORB_PAD_NONE];
}

bool orb_pad_pressed(orb_pad pad) {
    return input_pad_ok(pad) && input_now.pad.buttons[pad - ORB_PAD_NONE] &&
           !input_before.pad.buttons[pad - ORB_PAD_NONE];
}

bool orb_pad_released(orb_pad pad) {
    return input_pad_ok(pad) && !input_now.pad.buttons[pad - ORB_PAD_NONE] &&
           input_before.pad.buttons[pad - ORB_PAD_NONE];
}

orb_pad orb_pad_pressed_any(void) {
    for (int pad = ORB_PAD_NONE + 1; pad < ORB_PAD_END; pad++)
        if (orb_pad_pressed(pad)) return pad;

    return ORB_PAD_NONE;
}

orb_vec2f orb_pad_stick(orb_pad stick) {
    if (stick != ORB_PAD_LEFT_STICK && stick != ORB_PAD_RIGHT_STICK) return (orb_vec2f) {};

    const orb_pad_input* pad = &input_now.pad;
    int x = stick == ORB_PAD_LEFT_STICK ? pad->left_stick.x : pad->right_stick.x;
    int y = stick == ORB_PAD_LEFT_STICK ? pad->left_stick.y : pad->right_stick.y;
    int length = input_length(x, y);

    if (length <= INPUT_STICK_DEAD) return (orb_vec2f) {};

    float scale = (float)(orb_min(length, INPUT_AXIS_MAX) - INPUT_STICK_DEAD) /
                  (INPUT_AXIS_MAX - INPUT_STICK_DEAD) / (float)length;

    return (orb_vec2f) {(float)x * scale, (float)y * scale};
}

void orb_pad_stick_dpad(bool on) {
    input_stick_dpad = on;
}

float orb_pad_trigger(orb_pad trigger) {
    if (trigger != ORB_PAD_LEFT_TRIGGER && trigger != ORB_PAD_RIGHT_TRIGGER) return 0;

    const orb_pad_input* pad = &input_now.pad;
    int value = trigger == ORB_PAD_LEFT_TRIGGER ? pad->left_trigger : pad->right_trigger;

    if (value <= INPUT_TRIGGER_DEAD) return 0;

    return (float)(value - INPUT_TRIGGER_DEAD) / (INPUT_AXIS_MAX - INPUT_TRIGGER_DEAD);
}

void orb_button_bind(orb_button button, int source) {
    if (!input_button_ok(button)) return;
    if (source >= ORB_KEY_NONE && source < ORB_KEY_COUNT) input_keys[button] = source;
    if (source >= ORB_PAD_NONE && source < ORB_PAD_END) input_pads[button] = source;
}

bool orb_button_down(orb_button button) {
    return input_button_ok(button) && input_button_level(&input_now, button);
}

bool orb_button_pressed(orb_button button) {
    return input_button_ok(button) && input_button_level(&input_now, button) &&
           !input_button_level(&input_before, button);
}

bool orb_button_released(orb_button button) {
    return input_button_ok(button) && !input_button_level(&input_now, button) &&
           input_button_level(&input_before, button);
}

int orb_button_key(orb_button button) {
    return input_button_ok(button) ? input_keys[button] : ORB_KEY_NONE;
}

orb_pad orb_button_pad(orb_button button) {
    return input_button_ok(button) ? input_pads[button] : ORB_PAD_NONE;
}

orb_pad_make orb_input_pad_make(void) {
    return input_now.pad.make;
}

void orb_input_boot(void) {
    input_now = (orb_input) {};
    input_before = (orb_input) {};
    input_stick_dpad = false;
    input_stick_held = false;

    for (int i = 0; i < ORB_BTN_COUNT; i++) {
        input_keys[i] = orb_key_find(input_key_defaults[i]);
        input_pads[i] = input_pad_defaults[i];

        if (input_keys[i] == ORB_KEY_NONE)
            orb_log(
                "no key \"%s\" for %s on this layout", input_key_defaults[i], input_button_names[i]
            );
    }
}

void orb_input_step(const orb_input* next) {
    orb_pad_input* pad = &input_now.pad;

    input_before = input_now;
    input_now = *next;
    pad->buttons[ORB_PAD_LEFT_TRIGGER - ORB_PAD_NONE] = pad->left_trigger >= INPUT_HALF;
    pad->buttons[ORB_PAD_RIGHT_TRIGGER - ORB_PAD_NONE] = pad->right_trigger >= INPUT_HALF;
    input_stick_dpad_step(pad);
}

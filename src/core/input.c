#include "input.h"

#include "../os/os.h"
#include "bytes.h"
#include "log.h"

#include <string.h>

const char* const orb_button_names[ORB_BTN_COUNT] = {"up", "down", "left",  "right",
                                                     "a",  "b",    "x",     "y",
                                                     "l",  "r",    "start", "select"};
const char* const orb_button_defaults[ORB_BTN_COUNT] = {"up", "down", "left",   "right",
                                                        "z",  "x",    "a",      "s",
                                                        "q",  "w",    "return", "tab"};

static orb_input input_now, input_before;
static int input_bindings[ORB_BTN_COUNT]; // set by orb_input_resolve before any query

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
static char input_name[8]; // orb_key_name's buffer for a printable key's UTF-8

static int input_utf8_decode(const char* s, uint32_t* out);

static bool input_button_ok(int button) {
    return button >= 0 && button < ORB_BTN_COUNT;
}

static bool input_key_ok(int key) {
    return key > ORB_KEY_NONE && key < ORB_KEY_COUNT;
}

// A symbol is a named key's name, else one codepoint above space; returns the name's
// position, 0 for a lone codepoint (with it in *codepoint), or -1 for neither.
static int input_symbol_parse(const char* symbol, uint32_t* codepoint) {
    for (int key = 1; key < ORB_KEY_COUNT; key++)
        if (input_names[key] && strcmp(input_names[key], symbol) == 0) return key;

    int n = input_utf8_decode(symbol, codepoint);

    return n && symbol[n] == 0 && *codepoint > 0x20 ? 0 : -1;
}

// Bytes consumed by one well-formed codepoint at s, 0 for none or a bad sequence.
static int input_utf8_decode(const char* s, uint32_t* out) {
    const unsigned char* p = (const unsigned char*)s;
    int n = p[0] < 0x80             ? 1
            : (p[0] & 0xe0) == 0xc0 ? 2
            : (p[0] & 0xf0) == 0xe0 ? 3
            : (p[0] & 0xf8) == 0xf0 ? 4
                                    : 0;

    if (n == 0 || p[0] == 0) return 0;

    uint32_t cp = n == 1 ? p[0] : p[0] & (0x7f >> n);

    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;

        cp = cp << 6 | (p[i] & 0x3f);
    }

    if (cp < (uint32_t[]) {0, 0, 0x80, 0x800, 0x10000}[n]) return 0; // overlong
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;

    *out = cp;
    return n;
}

void orb_button_bind(int button, int source) {
    if (!input_button_ok(button) || (source != ORB_SOURCE_NONE && !input_key_ok(source))) return;

    input_bindings[button] = source;
}

bool orb_button_down(int button) {
    return input_button_ok(button) && orb_key_down(input_bindings[button]);
}

bool orb_button_pressed(int button) {
    return input_button_ok(button) && orb_key_pressed(input_bindings[button]);
}

bool orb_button_released(int button) {
    return input_button_ok(button) && orb_key_released(input_bindings[button]);
}

int orb_button_source(int button) {
    return input_button_ok(button) ? input_bindings[button] : ORB_SOURCE_NONE;
}

void orb_input_resolve(const orb_assets* assets) {
    bool sealed = assets->binding_count == ORB_BTN_COUNT;

    for (int i = 0; i < ORB_BTN_COUNT; i++) {
        const char* symbol = sealed ? assets->bindings[i].symbol : orb_button_defaults[i];
        int key = orb_input_symbol_position(symbol);

        if (key == ORB_KEY_NONE)
            orb_log("no key \"%s\" for %s on this layout", symbol, orb_button_names[i]);

        input_bindings[i] = key;
    }
}

void orb_input_step(const orb_input* next) {
    input_before = input_now;
    input_now = *next;
}

int orb_input_symbol_position(const char* symbol) {
    uint32_t codepoint;
    int key = input_symbol_parse(symbol, &codepoint);

    if (key != 0) return key > 0 ? key : ORB_KEY_NONE;

    // A shifted symbol such as "M" binds nowhere: the position must give it back unshifted.
    int position = orb_os_key_position(codepoint);

    return orb_os_key_symbol(position) == codepoint ? position : ORB_KEY_NONE;
}

bool orb_input_symbol_valid(const char* symbol) {
    uint32_t codepoint;

    return input_symbol_parse(symbol, &codepoint) >= 0;
}

bool orb_key_down(int key) {
    return input_key_ok(key) && input_now.keys[key];
}

const char* orb_key_name(int key) {
    if (!input_key_ok(key)) return "";
    if (input_names[key]) return input_names[key];

    uint32_t codepoint = orb_os_key_symbol(key);

    if (!codepoint) return "";

    input_name[orb_bytes_utf8(codepoint, input_name)] = 0;
    return input_name;
}

bool orb_key_pressed(int key) {
    return input_key_ok(key) && input_now.keys[key] && !input_before.keys[key];
}

int orb_key_pressed_any(void) {
    for (int key = 1; key < ORB_KEY_COUNT; key++)
        if (input_now.keys[key] && !input_before.keys[key]) return key;

    return ORB_KEY_NONE;
}

bool orb_key_released(int key) {
    return input_key_ok(key) && !input_now.keys[key] && input_before.keys[key];
}

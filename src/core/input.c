#include "input.h"

static orb_input input_now, input_before;

void orb_input_step(const orb_input* next) {
    input_before = input_now;
    input_now = *next;
}

bool orb_button_down(int button) {
    return input_now.down[button];
}

bool orb_button_pressed(int button) {
    return input_now.down[button] && !input_before.down[button];
}

bool orb_button_released(int button) {
    return !input_now.down[button] && input_before.down[button];
}

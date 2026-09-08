#pragma once

#include "../orb.h"

typedef struct orb_input {
    bool down[ORB_BTN_COUNT];
} orb_input;

void orb_input_step(const orb_input* next);
bool orb_button_down(int button);
bool orb_button_pressed(int button);
bool orb_button_released(int button);

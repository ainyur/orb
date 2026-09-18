#pragma once

#include "../orb.h"
#include "asset.h"

extern const char* const orb_button_names[ORB_BTN_COUNT]; // "up" .. "select", ORB_BTN order
extern const char* const orb_button_defaults[ORB_BTN_COUNT];

bool orb_key_down(int key);
bool orb_key_pressed(int key);
bool orb_key_released(int key);
int orb_key_pressed_any(void);
const char* orb_key_name(int key);

void orb_button_bind(int button, int source);
bool orb_button_down(int button);
bool orb_button_pressed(int button);
bool orb_button_released(int button);
int orb_button_source(int button);

int orb_input_symbol_position(const char* symbol);
bool orb_input_symbol_valid(const char* symbol);

void orb_input_resolve(const orb_assets* assets);
void orb_input_step(const orb_input* next);

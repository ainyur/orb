#pragma once

#include "../orb.h"

bool orb_key_down(int key);
bool orb_key_pressed(int key);
bool orb_key_released(int key);
int orb_key_pressed_any(void);
int orb_key_find(const char* symbol);
const char* orb_key_name(int key);

bool orb_pad_down(orb_pad pad);
bool orb_pad_pressed(orb_pad pad);
bool orb_pad_released(orb_pad pad);
orb_pad orb_pad_pressed_any(void);
orb_vec2f orb_pad_stick(orb_pad stick);
void orb_pad_stick_dpad(bool on);
float orb_pad_trigger(orb_pad trigger);

void orb_button_bind(orb_button button, int source);
bool orb_button_down(orb_button button);
bool orb_button_pressed(orb_button button);
bool orb_button_released(orb_button button);
int orb_button_key(orb_button button);
orb_pad orb_button_pad(orb_button button);

orb_pad_make orb_input_pad_make(void);
void orb_input_boot(void);
void orb_input_step(const orb_input* next);

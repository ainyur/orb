#pragma once

#include "../orb.h"

constexpr int ORB_CONSOLE_ARGS = 16;
constexpr int ORB_CONSOLE_BINDS = 16;
constexpr int ORB_CONSOLE_COMMANDS = 64;
constexpr int ORB_CONSOLE_HELP = 64;
constexpr int ORB_CONSOLE_HISTORY = 32;
constexpr int ORB_CONSOLE_LINE = 128;
constexpr int ORB_CONSOLE_NAME = 32;
constexpr int ORB_CONSOLE_VARS = 256;

void orb_console_boot(void* state, const orb_api* api);
void orb_console_step(orb_input* input);
void orb_console_draw(u32* rgb, orb_size size);
void orb_console_clear(void);

void orb_console_run(const char* line);
bool orb_console_open(void);

// Legal before boot (orb's own, kept across clears) and between a clear and the next
// step (the game's, during reload).
void orb_console_var_int(const char* name, i32* at, const char* help);
void orb_console_var_float(const char* name, f32* at, const char* help);
void orb_console_var_bool(const char* name, bool* at, const char* help);
void orb_console_command(const char* name, orb_command_fn fn, const char* help);

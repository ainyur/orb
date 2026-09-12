#pragma once

typedef struct orb_error {
    char text[256];
} orb_error;

// Returns false, so a check reads "if (bad) return orb_error_set(err, ...)".
bool orb_error_set(orb_error* e, const char* fmt, ...);
[[noreturn]] void orb_fatal(const char* fmt, ...);
void orb_log(const char* fmt, ...);

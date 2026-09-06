#pragma once

// A failure message on its way to a person: the arena names, the file, the
// field to raise. Passed by pointer through anything that can fail softly.
typedef struct orb_error {
    char text[256];
} orb_error;

void orb_error_set(orb_error* e, const char* fmt, ...);
[[noreturn]] void orb_fatal(const char* fmt, ...);
void orb_log(const char* fmt, ...);

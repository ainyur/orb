#pragma once

constexpr int ORB_LOG_LINE_MAX = 160;
constexpr int ORB_LOG_LINES = 128;

typedef struct orb_error {
    char text[256];
} orb_error;

extern thread_local bool orb_log_off_main; // the audio thread sets it: its lines skip the ring

[[gnu::format(gnu_printf, 1, 2)]] void orb_log(const char* fmt, ...);
void orb_log_clear(void);
int orb_log_line_count(void);
const char* orb_log_line(int back); // 0 the newest; "" past the count

// Returns false, so a check reads "if (bad) return orb_error_set(err, ...)".
bool orb_error_set(orb_error* e, const char* fmt, ...);
[[noreturn]] void orb_fatal(const char* fmt, ...);

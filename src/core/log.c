#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_local bool orb_log_off_main;
static char log_ring[ORB_LOG_LINES][ORB_LOG_LINE_MAX];
static int log_total;

// Cut at the last complete UTF-8 sequence that fits.
static void log_append(const char* line, size_t len) {
    if (len > ORB_LOG_LINE_MAX - 1) {
        len = ORB_LOG_LINE_MAX - 1;

        while (len > 0 && ((unsigned char)line[len] & 0xc0) == 0x80)
            len--;
    }

    char* slot = log_ring[log_total % ORB_LOG_LINES];

    memcpy(slot, line, len);
    slot[len] = 0;
    log_total++;
}

// Formats into a local buffer and writes it with one fputs, so a line from the
// audio thread cannot interleave with a main-thread line between the text and
// its newline.
void orb_log(const char* fmt, ...) {
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    size_t len = n < 0 ? 0 : (size_t)n;

    if (len > sizeof line - 2) len = sizeof line - 2;
    if (!orb_log_off_main) log_append(line, len);

    line[len] = '\n';
    line[len + 1] = '\0';
    fputs(line, stderr);
}

void orb_log_clear(void) {
    log_total = 0;
}

int orb_log_line_count(void) {
    return log_total < ORB_LOG_LINES ? log_total : ORB_LOG_LINES;
}

const char* orb_log_line(int back) {
    if (back < 0 || back >= orb_log_line_count()) return "";

    return log_ring[(log_total - 1 - back) % ORB_LOG_LINES];
}

bool orb_error_set(orb_error* err, const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(err->text, sizeof err->text, fmt, ap);
    va_end(ap);
    return false;
}

orb_bytes orb_bytes_format(size_t bytes) {
    static const char* units[] = {"KB", "MB", "GB", "TB"};
    orb_bytes out;

    if (bytes < 1024) {
        snprintf(out.text, sizeof out.text, "%zu B", bytes);
        return out;
    }

    double value = (double)bytes / 1024;
    int unit = 0;

    // 1023.95 and up would print as "1024.0", so it moves to the next unit.
    while (value >= 1023.95 && unit < 3) {
        value /= 1024;
        unit++;
    }

    snprintf(out.text, sizeof out.text, "%.1f %s", value, units[unit]);
    return out;
}

[[noreturn]] void orb_fatal(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    fputs("fatal: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

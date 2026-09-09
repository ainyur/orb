#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void orb_error_set(orb_error* e, const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(e->text, sizeof e->text, fmt, ap);
    va_end(ap);
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

    line[len] = '\n';
    line[len + 1] = '\0';
    fputs(line, stderr);
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

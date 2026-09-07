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

void orb_log(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
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

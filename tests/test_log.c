#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static bool format_is(size_t bytes, const char* want) {
    return strcmp(orb_bytes_format(bytes).text, want) == 0;
}

int main(void) {
    CHECK(format_is(0, "0 B"));
    CHECK(format_is(1023, "1023 B"));
    CHECK(format_is(1024, "1.0 KB"));
    CHECK(format_is(1536, "1.5 KB"));
    CHECK(format_is(1048575, "1.0 MB")); // never "1024.0 KB"
    CHECK(format_is(12834353, "12.2 MB"));
    CHECK(format_is((size_t)1 << 30, "1.0 GB"));
    CHECK(format_is((size_t)3 << 40, "3.0 TB"));
    CHECK(format_is(SIZE_MAX, "16777216.0 TB")); // the largest count still fits the text
    return 0;
}

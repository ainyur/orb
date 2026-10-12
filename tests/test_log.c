#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static bool format_is(usize bytes, const char* want) {
    return strcmp(orb_bytes_format(bytes).text, want) == 0;
}

int main(void) {
    CHECK(format_is(0, "0 B"));
    CHECK(format_is(1023, "1023 B"));
    CHECK(format_is(1024, "1.0 KB"));
    CHECK(format_is(1536, "1.5 KB"));
    CHECK(format_is(1048575, "1.0 MB")); // never "1024.0 KB"
    CHECK(format_is(12834353, "12.2 MB"));
    CHECK(format_is((usize)1 << 30, "1.0 GB"));
    CHECK(format_is((usize)3 << 40, "3.0 TB"));
    CHECK(format_is(SIZE_MAX, "16777216.0 TB")); // the largest count still fits the text

    // past twice the ring the newest lines are still in order and the count stays full
    orb_log_clear();

    for (int i = 0; i < 300; i++) {
        char line[16];
        int n = snprintf(line, sizeof line, "line %d", i);

        log_append(line, (usize)n); // the ring alone, so the test prints nothing
    }

    CHECK_EQ(orb_log_line_count(), ORB_LOG_LINES);
    CHECK(strcmp(orb_log_line(0), "line 299") == 0);
    CHECK(strcmp(orb_log_line(ORB_LOG_LINES - 1), "line 172") == 0);
    CHECK(strcmp(orb_log_line(ORB_LOG_LINES), "") == 0);
    return 0;
}

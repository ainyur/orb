#include "os.h"
#ifdef _WIN32
#include "win32.c"
#else
#include "posix.c"
#endif
#include <stdlib.h>
#include <string.h>

static orb_input headless_input;
static uint32_t* headless_frame;
static size_t headless_pixels;

// The US layout, unshifted: ORB_KEY_A..ORB_KEY_0 and ORB_KEY_MINUS..ORB_KEY_SLASH.
static const char headless_letters[] = "abcdefghijklmnopqrstuvwxyz1234567890";
static const char headless_marks[] = "-=[]\\#;'`,./";

bool orb_os_open(const orb_os_config* cfg) {
    headless_pixels = (size_t)cfg->size.width * cfg->size.height;
    headless_frame = calloc(headless_pixels, sizeof *headless_frame);

    return headless_frame != nullptr;
}

bool orb_os_pump(orb_input* out) {
    *out = headless_input;
    headless_input.text[0] = 0;

    return true;
}

void orb_os_present(const uint32_t* rgb) {
    memcpy(headless_frame, rgb, headless_pixels * sizeof *rgb);
}

uint32_t orb_os_key_symbol(int key) {
    if (key >= ORB_KEY_A && key <= ORB_KEY_0) return (uint32_t)headless_letters[key - ORB_KEY_A];
    if (key >= ORB_KEY_MINUS && key <= ORB_KEY_SLASH)
        return (uint32_t)headless_marks[key - ORB_KEY_MINUS];

    return 0;
}

int orb_os_key_position(uint32_t codepoint) {
    if (!codepoint || codepoint > 0x7f) return ORB_KEY_NONE;

    const char* letter = strchr(headless_letters, (int)codepoint);
    const char* mark = strchr(headless_marks, (int)codepoint);

    return letter ? ORB_KEY_A + (int)(letter - headless_letters)
           : mark ? ORB_KEY_MINUS + (int)(mark - headless_marks)
                  : ORB_KEY_NONE;
}

void orb_os_close(void) {
    free(headless_frame);
    headless_frame = nullptr;
}

const uint32_t* orb_os_headless_frame(void) {
    return headless_frame;
}

void orb_os_headless_set_input(const orb_input* in) {
    headless_input = *in;
}

#include "orb_os.h"
#include "posix.c"
#include <stdlib.h>
#include <string.h>

static orb_input headless_input;
static uint32_t* headless_frame;
static size_t headless_pixels;

bool orb_os_open(const orb_os_config* cfg) {
    headless_pixels = (size_t)cfg->size_w * cfg->size_h;
    headless_frame = calloc(headless_pixels, sizeof *headless_frame);

    return headless_frame != NULL;
}

void orb_os_close(void) {
    free(headless_frame);
    headless_frame = NULL;
}

bool orb_os_pump(orb_input* out) {
    *out = headless_input;

    return true;
}

void orb_os_present(const uint32_t* rgb) {
    memcpy(headless_frame, rgb, headless_pixels * sizeof *rgb);
}

void orb_os_headless_set_input(const orb_input* in) {
    headless_input = *in;
}

const uint32_t* orb_os_headless_frame(void) {
    return headless_frame;
}

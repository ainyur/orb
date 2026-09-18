#include "../core/log.h"
#include "../core/macros.h"
#include "os.h"
#include "posix.c"
#include "x11_keys.h"

#include "alsa.c"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

static Display* x11_display;
static Window x11_window;
static GC x11_gc;
static XImage* x11_image;
static uint32_t* x11_pixels;
static orb_size x11_screen, x11_win, x11_fb;
static Atom x11_wm_delete;
static bool x11_down[ORB_KEY_COUNT];
static bool x11_resized;

bool orb_os_open(const orb_os_config* cfg) {
    x11_display = XOpenDisplay(nullptr);

    if (!x11_display) {
        orb_log("cannot open the X display");
        return false;
    }

    int screen = DefaultScreen(x11_display);

    x11_screen = (orb_size) {DisplayWidth(x11_display, screen), DisplayHeight(x11_display, screen)};
    x11_fb = cfg->size;

    int scale = orb_os_open_scale(x11_fb, x11_screen);

    x11_win = (orb_size) {x11_fb.width * scale, x11_fb.height * scale};

    x11_window = XCreateSimpleWindow(
        x11_display, RootWindow(x11_display, screen), 0, 0, (unsigned)x11_win.width,
        (unsigned)x11_win.height, 0, 0, BlackPixel(x11_display, screen)
    );
    XStoreName(x11_display, x11_window, cfg->title);
    XSelectInput(x11_display, x11_window, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    x11_wm_delete = XInternAtom(x11_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11_display, x11_window, &x11_wm_delete, 1);
    XkbSetDetectableAutoRepeat(x11_display, True, nullptr);
    XMapWindow(x11_display, x11_window);
    x11_gc = DefaultGC(x11_display, screen);

    x11_pixels = calloc((size_t)x11_screen.width * x11_screen.height, sizeof *x11_pixels);
    x11_image = XCreateImage(
        x11_display, DefaultVisual(x11_display, screen),
        (unsigned)DefaultDepth(x11_display, screen), ZPixmap, 0, (char*)x11_pixels,
        (unsigned)x11_screen.width, (unsigned)x11_screen.height, 32, 0
    );

    if (!x11_image) {
        orb_log("XCreateImage failed; the display depth is probably not 24 or 32");
        return false;
    }

    alsa_open();

    return true;
}

bool orb_os_pump(orb_input* out) {
    while (XPending(x11_display)) {
        XEvent ev;

        XNextEvent(x11_display, &ev);

        if (ev.type == KeyPress || ev.type == KeyRelease) {
            unsigned code = ev.xkey.keycode;
            int key = code >= 8 && code < 8 + 256 ? x11_keys[code - 8] : ORB_KEY_NONE;

            if (key) x11_down[key] = ev.type == KeyPress;
        } else if (ev.type == ConfigureNotify) {
            x11_win = (orb_size) {ev.xconfigure.width, ev.xconfigure.height};
            x11_resized = true;
        } else if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == x11_wm_delete) {
            return false;
        }
    }

    memcpy(out->keys, x11_down, sizeof x11_down);

    return true;
}

void orb_os_present(const uint32_t* rgb) {
    int w = orb_min(x11_win.width, x11_screen.width);
    int h = orb_min(x11_win.height, x11_screen.height);
    int scale = orb_max(1, orb_min(w / x11_fb.width, h / x11_fb.height));

    // A window smaller than the frame shows its top-left corner.
    int cols = orb_min(x11_fb.width, w / scale), rows = orb_min(x11_fb.height, h / scale);
    int ox = (w - cols * scale) / 2, oy = (h - rows * scale) / 2;

    // The frame overwrites its own area every time; only the borders need clearing,
    // and only when they move.
    if (x11_resized) {
        memset(x11_pixels, 0, (size_t)x11_screen.width * x11_screen.height * sizeof *x11_pixels);
        x11_resized = false;
    }

    for (int y = 0; y < rows; y++) {
        const uint32_t* src = rgb + y * x11_fb.width;
        uint32_t* dst = x11_pixels + (oy + y * scale) * x11_screen.width + ox;

        for (int x = 0; x < cols; x++)
            for (int k = 0; k < scale; k++)
                dst[x * scale + k] = src[x];

        for (int k = 1; k < scale; k++)
            memcpy(dst + k * x11_screen.width, dst, (size_t)cols * scale * sizeof *dst);
    }

    XPutImage(x11_display, x11_window, x11_gc, x11_image, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFlush(x11_display);
}

uint32_t orb_os_key_symbol(int key) {
    int code = orb_os_key_code(x11_keys, key);

    if (code < 0) return 0;

    KeySym sym = XkbKeycodeToKeysym(x11_display, (KeyCode)(code + 8), 0, 0);

    if ((sym >= 0x20 && sym <= 0x7e) || (sym >= 0xa0 && sym <= 0xff)) return (uint32_t)sym;
    if ((sym & 0xff000000u) == 0x01000000u && (sym & 0xffffffu) <= 0x10ffff)
        return (uint32_t)(sym & 0xffffffu);

    return 0;
}

// A keysym is a Latin-1 codepoint in 0x20..0x7e and 0xa0..0xff, or a codepoint
// under the 0x01000000 prefix; anything else has no single symbol.
int orb_os_key_position(uint32_t codepoint) {
    KeySym sym = codepoint < 0x100 ? codepoint : (0x01000000u | codepoint);
    KeyCode code = XKeysymToKeycode(x11_display, sym);

    return code >= 8 ? x11_keys[code - 8] : ORB_KEY_NONE;
}

void orb_os_close(void) {
    alsa_close();
    XDestroyImage(x11_image); // also frees x11_pixels
    XDestroyWindow(x11_display, x11_window);
    XCloseDisplay(x11_display);
}

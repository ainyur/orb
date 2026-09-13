#include "../core/log.h"
#include "../core/macros.h"
#include "os.h"
#include "posix.c"

#include "alsa.c"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>

static Display* x11_display;
static Window x11_window;
static GC x11_gc;
static XImage* x11_image;
static uint32_t* x11_pixels;
static orb_size x11_screen, x11_win, x11_fb;
static Atom x11_wm_delete;
static bool x11_keys[ORB_BTN_COUNT];
static bool x11_resized;

// The key for each button, in ORB_BTN order.
static const uint32_t x11_keymap[ORB_BTN_COUNT] = {XK_Up, XK_Down, XK_Left,   XK_Right,
                                                   XK_z,  XK_x,    XK_a,      XK_s,
                                                   XK_q,  XK_w,    XK_Return, XK_Tab};

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

    x11_win = (orb_size) {x11_fb.w * scale, x11_fb.h * scale};

    x11_window = XCreateSimpleWindow(
        x11_display, RootWindow(x11_display, screen), 0, 0, (unsigned)x11_win.w,
        (unsigned)x11_win.h, 0, 0, BlackPixel(x11_display, screen)
    );
    XStoreName(x11_display, x11_window, cfg->title);
    XSelectInput(x11_display, x11_window, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    x11_wm_delete = XInternAtom(x11_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11_display, x11_window, &x11_wm_delete, 1);
    XkbSetDetectableAutoRepeat(x11_display, True, nullptr);
    XMapWindow(x11_display, x11_window);
    x11_gc = DefaultGC(x11_display, screen);

    x11_pixels = calloc((size_t)x11_screen.w * x11_screen.h, sizeof *x11_pixels);
    x11_image = XCreateImage(
        x11_display, DefaultVisual(x11_display, screen),
        (unsigned)DefaultDepth(x11_display, screen), ZPixmap, 0, (char*)x11_pixels,
        (unsigned)x11_screen.w, (unsigned)x11_screen.h, 32, 0
    );

    if (!x11_image) {
        orb_log("XCreateImage failed; the display depth is probably not 24 or 32");
        return false;
    }

    alsa_open();

    return true;
}

void orb_os_close(void) {
    alsa_close();
    XDestroyImage(x11_image); // also frees x11_pixels
    XDestroyWindow(x11_display, x11_window);
    XCloseDisplay(x11_display);
}

void orb_os_present(const uint32_t* rgb) {
    int w = orb_min(x11_win.w, x11_screen.w);
    int h = orb_min(x11_win.h, x11_screen.h);
    int scale = orb_max(1, orb_min(w / x11_fb.w, h / x11_fb.h));

    // A window smaller than the frame shows its top-left corner.
    int cols = orb_min(x11_fb.w, w / scale), rows = orb_min(x11_fb.h, h / scale);
    int ox = (w - cols * scale) / 2, oy = (h - rows * scale) / 2;

    // The frame overwrites its own area every time; only the borders need clearing,
    // and only when they move.
    if (x11_resized) {
        memset(x11_pixels, 0, (size_t)x11_screen.w * x11_screen.h * sizeof *x11_pixels);
        x11_resized = false;
    }

    for (int y = 0; y < rows; y++) {
        const uint32_t* src = rgb + y * x11_fb.w;
        uint32_t* dst = x11_pixels + (oy + y * scale) * x11_screen.w + ox;

        for (int x = 0; x < cols; x++)
            for (int k = 0; k < scale; k++)
                dst[x * scale + k] = src[x];

        for (int k = 1; k < scale; k++)
            memcpy(dst + k * x11_screen.w, dst, (size_t)cols * scale * sizeof *dst);
    }

    XPutImage(x11_display, x11_window, x11_gc, x11_image, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFlush(x11_display);
}

bool orb_os_pump(orb_input* out) {
    while (XPending(x11_display)) {
        XEvent ev;

        XNextEvent(x11_display, &ev);

        if (ev.type == KeyPress || ev.type == KeyRelease) {
            int button = orb_os_button(x11_keymap, (uint32_t)XLookupKeysym(&ev.xkey, 0));

            if (button >= 0) x11_keys[button] = ev.type == KeyPress;
        } else if (ev.type == ConfigureNotify) {
            x11_win = (orb_size) {ev.xconfigure.width, ev.xconfigure.height};
            x11_resized = true;
        } else if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == x11_wm_delete) {
            return false;
        }
    }

    memcpy(out->down, x11_keys, sizeof x11_keys);

    return true;
}

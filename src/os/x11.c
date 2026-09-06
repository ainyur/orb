#include "../core/log.h"
#include "orb_os.h"
#include "posix.c"
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
static int x11_max_w, x11_max_h, x11_win_w, x11_win_h, x11_fb_w, x11_fb_h;
static Atom x11_wm_delete;
static bool x11_keys[ORB_BTN_COUNT];

static int x11_button(KeySym key) {
    switch (key) {
    case XK_Up:
        return ORB_BTN_UP;
    case XK_Down:
        return ORB_BTN_DOWN;
    case XK_Left:
        return ORB_BTN_LEFT;
    case XK_Right:
        return ORB_BTN_RIGHT;
    case XK_z:
        return ORB_BTN_A;
    case XK_x:
        return ORB_BTN_B;
    case XK_a:
        return ORB_BTN_X;
    case XK_s:
        return ORB_BTN_Y;
    case XK_q:
        return ORB_BTN_L;
    case XK_w:
        return ORB_BTN_R;
    case XK_Return:
        return ORB_BTN_START;
    case XK_Tab:
        return ORB_BTN_SELECT;
    default:
        return -1;
    }
}

bool orb_os_open(const orb_os_config* cfg) {
    x11_display = XOpenDisplay(NULL);

    if (!x11_display) {
        orb_log("cannot open the X display");
        return false;
    }

    int screen = DefaultScreen(x11_display);

    x11_max_w = DisplayWidth(x11_display, screen);
    x11_max_h = DisplayHeight(x11_display, screen);
    x11_fb_w = cfg->size_w;
    x11_fb_h = cfg->size_h;

    int scale = 3;

    while (scale > 1 && (x11_fb_w * scale > x11_max_w || x11_fb_h * scale > x11_max_h))
        scale--;

    x11_win_w = x11_fb_w * scale;
    x11_win_h = x11_fb_h * scale;

    x11_window = XCreateSimpleWindow(
        x11_display, RootWindow(x11_display, screen), 0, 0, (unsigned)x11_win_w,
        (unsigned)x11_win_h, 0, 0, BlackPixel(x11_display, screen)
    );
    XStoreName(x11_display, x11_window, cfg->title);
    XSelectInput(x11_display, x11_window, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    x11_wm_delete = XInternAtom(x11_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11_display, x11_window, &x11_wm_delete, 1);
    XkbSetDetectableAutoRepeat(x11_display, True, NULL);
    XMapWindow(x11_display, x11_window);
    x11_gc = DefaultGC(x11_display, screen);

    x11_pixels = calloc((size_t)x11_max_w * x11_max_h, sizeof *x11_pixels);
    x11_image = XCreateImage(
        x11_display, DefaultVisual(x11_display, screen),
        (unsigned)DefaultDepth(x11_display, screen), ZPixmap, 0, (char*)x11_pixels,
        (unsigned)x11_max_w, (unsigned)x11_max_h, 32, 0
    );

    if (!x11_image) orb_log("XCreateImage failed; the display depth is probably not 24 or 32");

    return x11_image != NULL;
}

void orb_os_close(void) {
    XDestroyImage(x11_image); // also frees x11_pixels
    XDestroyWindow(x11_display, x11_window);
    XCloseDisplay(x11_display);
}

void orb_os_present(const uint32_t* rgb) {
    int w = x11_win_w < x11_max_w ? x11_win_w : x11_max_w;
    int h = x11_win_h < x11_max_h ? x11_win_h : x11_max_h;
    int scale = w / x11_fb_w;

    if (h / x11_fb_h < scale) scale = h / x11_fb_h;
    if (scale < 1) scale = 1;

    int ox = (w - x11_fb_w * scale) / 2, oy = (h - x11_fb_h * scale) / 2;

    memset(x11_pixels, 0, (size_t)x11_max_w * h * sizeof *x11_pixels);

    for (int y = 0; y < x11_fb_h * scale; y++) {
        int wy = oy + y;

        if (wy < 0 || wy >= h) continue;

        const uint32_t* src = rgb + (y / scale) * x11_fb_w;
        uint32_t* dst = x11_pixels + wy * x11_max_w;

        for (int x = 0; x < x11_fb_w * scale; x++) {
            int wx = ox + x;

            if (wx < 0 || wx >= w) continue;

            dst[wx] = src[x / scale];
        }
    }

    XPutImage(x11_display, x11_window, x11_gc, x11_image, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFlush(x11_display);
}

bool orb_os_pump(orb_input* out) {
    while (XPending(x11_display)) {
        XEvent ev;

        XNextEvent(x11_display, &ev);

        if (ev.type == KeyPress || ev.type == KeyRelease) {
            int button = x11_button(XLookupKeysym(&ev.xkey, 0));

            if (button >= 0) x11_keys[button] = ev.type == KeyPress;
        } else if (ev.type == ConfigureNotify) {
            x11_win_w = ev.xconfigure.width;
            x11_win_h = ev.xconfigure.height;
        } else if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == x11_wm_delete) {
            return false;
        }
    }

    memcpy(out->down, x11_keys, sizeof x11_keys);
    return true;
}

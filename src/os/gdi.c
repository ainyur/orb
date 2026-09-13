#include "../core/log.h"
#include "../core/macros.h"
#include "os.h"
#include "win32.c"

#include "wasapi.c"

#include <string.h>

static HWND gdi_window;
static ATOM gdi_class;
static orb_size gdi_fb, gdi_win;
static bool gdi_keys[ORB_BTN_COUNT];
static bool gdi_closed;
static const uint32_t* gdi_last_frame; // the frame most recently presented, for WM_PAINT

// The key for each button, in ORB_BTN order.
static const uint32_t gdi_keymap[ORB_BTN_COUNT] = {VK_UP, VK_DOWN, VK_LEFT,   VK_RIGHT,
                                                   'Z',   'X',     'A',       'S',
                                                   'Q',   'W',     VK_RETURN, VK_TAB};

// Integer-scale the frame into the client area, centered, borders left to the
// class background brush.
static void gdi_blit(HDC dc, const uint32_t* rgb) {
    int scale = orb_max(1, orb_min(gdi_win.w / gdi_fb.w, gdi_win.h / gdi_fb.h));

    int w = gdi_fb.w * scale, h = gdi_fb.h * scale;
    int ox = (gdi_win.w - w) / 2, oy = (gdi_win.h - h) / 2;
    BITMAPINFO info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = gdi_fb.w,
            .biHeight = -gdi_fb.h, // top-down
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };

    SetStretchBltMode(dc, COLORONCOLOR); // nearest neighbour: pixels stay square
    StretchDIBits(dc, ox, oy, w, h, 0, 0, gdi_fb.w, gdi_fb.h, rgb, &info, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK gdi_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int button = orb_os_button(gdi_keymap, (uint32_t)w);

        if (button >= 0) gdi_keys[button] = msg == WM_KEYDOWN;

        return 0;
    }
    case WM_SIZE:
        gdi_win.w = LOWORD(l);
        gdi_win.h = HIWORD(l);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);

        if (gdi_last_frame) gdi_blit(dc, gdi_last_frame);

        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        gdi_closed = true;
        return 0;
    default:
        return DefWindowProc(window, msg, w, l);
    }
}

bool orb_os_open(const orb_os_config* cfg) {
    orb_size screen = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int scale = orb_os_open_scale(cfg->size, screen);

    gdi_fb = cfg->size;
    gdi_win = (orb_size) {gdi_fb.w * scale, gdi_fb.h * scale};

    WNDCLASS wc = {
        .lpfnWndProc = gdi_proc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = GetStockObject(BLACK_BRUSH),
        .lpszClassName = L"orb",
    };

    gdi_class = RegisterClass(&wc);

    if (!gdi_class) {
        orb_log("cannot register the window class");
        return false;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT frame = {0, 0, gdi_win.w, gdi_win.h};

    AdjustWindowRect(&frame, style, FALSE); // grow the outer rect so the client is fb * scale

    int outer_w = frame.right - frame.left, outer_h = frame.bottom - frame.top;
    int x = (screen.w - outer_w) / 2, y = (screen.h - outer_h) / 2; // centered, not cascaded

    win32_wpath title;

    gdi_window = CreateWindowEx(
        0, MAKEINTATOM(gdi_class), win32_wide(cfg->title, title, ORB_PATH_MAX), style, x, y,
        outer_w, outer_h, nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!gdi_window) {
        orb_log("cannot create the window");
        return false;
    }

    ShowWindow(gdi_window, SW_SHOW);

    wasapi_open();

    return true;
}

void orb_os_close(void) {
    wasapi_close();

    DestroyWindow(gdi_window);
    UnregisterClass(MAKEINTATOM(gdi_class), GetModuleHandle(nullptr));
}

void orb_os_present(const uint32_t* rgb) {
    HDC dc = GetDC(gdi_window);

    gdi_last_frame = rgb;
    gdi_blit(dc, rgb);
    ReleaseDC(gdi_window, dc);
}

bool orb_os_pump(orb_input* out) {
    for (MSG msg; PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    memcpy(out->down, gdi_keys, sizeof gdi_keys);

    return !gdi_closed;
}

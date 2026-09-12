#include "../core/log.h"
#include "../core/macros.h"
#include "os.h"
#include "win32.c"

#include "wasapi.c"

#include <string.h>

static HWND gdi_window;
static ATOM gdi_class;
static int gdi_fb_w, gdi_fb_h, gdi_win_w, gdi_win_h;
static bool gdi_keys[ORB_BTN_COUNT];
static bool gdi_closed;
static const uint32_t* gdi_last; // the frame most recently presented, for WM_PAINT

static int gdi_button(WPARAM key) {
    switch (key) {
    case VK_UP:
        return ORB_BTN_UP;
    case VK_DOWN:
        return ORB_BTN_DOWN;
    case VK_LEFT:
        return ORB_BTN_LEFT;
    case VK_RIGHT:
        return ORB_BTN_RIGHT;
    case 'Z':
        return ORB_BTN_A;
    case 'X':
        return ORB_BTN_B;
    case 'A':
        return ORB_BTN_X;
    case 'S':
        return ORB_BTN_Y;
    case 'Q':
        return ORB_BTN_L;
    case 'W':
        return ORB_BTN_R;
    case VK_RETURN:
        return ORB_BTN_START;
    case VK_TAB:
        return ORB_BTN_SELECT;
    default:
        return -1;
    }
}

// Integer-scale the frame into the client area, centered, borders left to the
// class background brush.
static void gdi_blit(HDC dc, const uint32_t* rgb) {
    int scale = orb_max(1, orb_min(gdi_win_w / gdi_fb_w, gdi_win_h / gdi_fb_h));

    int w = gdi_fb_w * scale, h = gdi_fb_h * scale;
    int ox = (gdi_win_w - w) / 2, oy = (gdi_win_h - h) / 2;
    BITMAPINFO info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = gdi_fb_w,
            .biHeight = -gdi_fb_h, // top-down
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };

    SetStretchBltMode(dc, COLORONCOLOR); // nearest neighbour: pixels stay square
    StretchDIBits(dc, ox, oy, w, h, 0, 0, gdi_fb_w, gdi_fb_h, rgb, &info, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK gdi_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int button = gdi_button(w);

        if (button >= 0) gdi_keys[button] = msg == WM_KEYDOWN;

        return 0;
    }
    case WM_SIZE:
        gdi_win_w = LOWORD(l);
        gdi_win_h = HIWORD(l);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);

        if (gdi_last) gdi_blit(dc, gdi_last);

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
    gdi_fb_w = cfg->size_w;
    gdi_fb_h = cfg->size_h;

    int max_w = GetSystemMetrics(SM_CXSCREEN), max_h = GetSystemMetrics(SM_CYSCREEN);
    int scale = 3;

    while (scale > 1 && (gdi_fb_w * scale > max_w || gdi_fb_h * scale > max_h))
        scale--;

    gdi_win_w = gdi_fb_w * scale;
    gdi_win_h = gdi_fb_h * scale;

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
    RECT frame = {0, 0, gdi_win_w, gdi_win_h};

    AdjustWindowRect(&frame, style, FALSE); // grow the outer rect so the client is fb * scale

    int outer_w = frame.right - frame.left, outer_h = frame.bottom - frame.top;
    int x = (max_w - outer_w) / 2, y = (max_h - outer_h) / 2; // centered, not cascaded

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
    gdi_window = nullptr;
    gdi_class = 0;
    gdi_last = nullptr;
}

void orb_os_present(const uint32_t* rgb) {
    HDC dc = GetDC(gdi_window);

    gdi_last = rgb;
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

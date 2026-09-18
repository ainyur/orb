#include "../core/log.h"
#include "../core/macros.h"
#include "gdi_keys.h"
#include "os.h"
#include "win32.c"

#include "wasapi.c"

#include <string.h>

static HWND gdi_window;
static ATOM gdi_class;
static orb_size gdi_fb, gdi_win;
static bool gdi_down[ORB_KEY_COUNT];
static bool gdi_closed;
static const uint32_t* gdi_last_frame; // the frame most recently presented, for WM_PAINT

// Integer-scale the frame into the client area, centered, borders left to the
// class background brush.
static void gdi_blit(HDC dc, const uint32_t* rgb) {
    int scale = orb_max(1, orb_min(gdi_win.width / gdi_fb.width, gdi_win.height / gdi_fb.height));

    int w = gdi_fb.width * scale, h = gdi_fb.height * scale;
    int ox = (gdi_win.width - w) / 2, oy = (gdi_win.height - h) / 2;
    BITMAPINFO info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = gdi_fb.width,
            .biHeight = -gdi_fb.height, // top-down
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };

    SetStretchBltMode(dc, COLORONCOLOR); // nearest neighbour: pixels stay square
    StretchDIBits(
        dc, ox, oy, w, h, 0, 0, gdi_fb.width, gdi_fb.height, rgb, &info, DIB_RGB_COLORS, SRCCOPY
    );
}

static LRESULT CALLBACK gdi_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        unsigned code = (l >> 16) & 0x7f;

        // Injected input (accessibility tools, remote desktops) may carry no scancode.
        if (!code) code = MapVirtualKey((UINT)w, MAPVK_VK_TO_VSC) & 0x7f;

        // Print screen and pause do not arrive as plain set-1 codes.
        int key = w == VK_SNAPSHOT ? ORB_KEY_PRINT_SCREEN
                  : w == VK_PAUSE  ? ORB_KEY_PAUSE
                                   : gdi_keys[code + ((l >> 24) & 1) * 128];

        if (key) gdi_down[key] = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;

        // DefWindowProc turns WM_SYSKEYDOWN into WM_SYSCOMMAND/SC_CLOSE for Alt+F4.
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return DefWindowProc(window, msg, w, l);

        return 0;
    }
    case WM_SYSCOMMAND:
        // A lone Alt or F10 would otherwise put a window with no menu into the menu loop.
        if ((w & 0xfff0) == SC_KEYMENU) return 0;

        return DefWindowProc(window, msg, w, l);
    case WM_SIZE:
        gdi_win.width = LOWORD(l);
        gdi_win.height = HIWORD(l);
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

int orb_os_key_position(uint32_t codepoint) {
    if (codepoint > 0xffff) return ORB_KEY_NONE;

    SHORT scan = VkKeyScan((WCHAR)codepoint);

    if (scan == -1) return ORB_KEY_NONE;

    UINT code = MapVirtualKey((UINT)(scan & 0xff), MAPVK_VK_TO_VSC);

    return code < 128 ? gdi_keys[code] : ORB_KEY_NONE;
}

uint32_t orb_os_key_symbol(int key) {
    int code = orb_os_key_code(gdi_keys, key);

    if (code < 0) return 0;

    UINT scan = code < 128 ? (UINT)code : 0xe000u | (UINT)(code - 128);
    UINT vk = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);
    BYTE state[256] = {0};
    WCHAR text[4];
    // Flag 4 leaves any dead-key state alone.
    int n = ToUnicode(vk, scan, state, text, 4, 4);

    return n == 1 && text[0] > 0x20 ? (uint32_t)text[0] : 0;
}

bool orb_os_open(const orb_os_config* cfg) {
    orb_size screen = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int scale = orb_os_open_scale(cfg->size, screen);

    gdi_fb = cfg->size;
    gdi_win = (orb_size) {gdi_fb.width * scale, gdi_fb.height * scale};

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
    RECT frame = {0, 0, gdi_win.width, gdi_win.height};

    AdjustWindowRect(&frame, style, FALSE); // grow the outer rect so the client is fb * scale

    int outer_w = frame.right - frame.left, outer_h = frame.bottom - frame.top;
    int x = (screen.width - outer_w) / 2,
        y = (screen.height - outer_h) / 2; // centered, not cascaded

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

    memcpy(out->keys, gdi_down, sizeof gdi_down);

    return !gdi_closed;
}

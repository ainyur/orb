#include "../core/bytes.h"
#include "../core/log.h"
#include "../core/macros.h"
#include "gdi_keys.h"
#include "os.h"
#include "win32.c"

#include "wasapi.c"
#include "xinput.c"

#include <string.h>

static HWND gdi_window;
static ATOM gdi_class;
static orb_size gdi_fb, gdi_win;
static bool gdi_down[ORB_KEY_COUNT];
static bool gdi_closed;
static bool gdi_focused;
static const uint32_t* gdi_last_frame; // the frame most recently presented, for WM_PAINT
static char gdi_text[ORB_INPUT_TEXT];
static int gdi_text_len;
static uint32_t gdi_high_surrogate;

// Integer-scale the frame into the client area, centered, borders left to the
// class background brush.
static void gdi_blit(HDC device_context, const uint32_t* rgb) {
    int scale = orb_max(1, orb_min(gdi_win.width / gdi_fb.width, gdi_win.height / gdi_fb.height));

    int width = gdi_fb.width * scale, height = gdi_fb.height * scale;
    int origin_x = (gdi_win.width - width) / 2, origin_y = (gdi_win.height - height) / 2;
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

    SetStretchBltMode(device_context, COLORONCOLOR); // nearest neighbour: pixels stay square
    StretchDIBits(
        device_context, origin_x, origin_y, width, height, 0, 0, gdi_fb.width, gdi_fb.height, rgb,
        &info, DIB_RGB_COLORS, SRCCOPY
    );
}

// One WM_CHAR unit: a surrogate pair is joined, a high surrogate not immediately
// followed by a low one is dropped, control characters are dropped.
static void gdi_char(uint32_t unit) {
    uint32_t codepoint = unit;

    if (unit < 0xdc00 || unit >= 0xe000) gdi_high_surrogate = 0;

    if (unit >= 0xd800 && unit < 0xdc00) {
        gdi_high_surrogate = unit;
        return;
    }

    if (unit >= 0xdc00 && unit < 0xe000) {
        if (!gdi_high_surrogate) return;

        codepoint = 0x10000 + ((gdi_high_surrogate - 0xd800) << 10) + (unit - 0xdc00);
        gdi_high_surrogate = 0;
    }

    orb_bytes_utf8_push(gdi_text, &gdi_text_len, ORB_INPUT_TEXT, codepoint);
}

static LRESULT CALLBACK gdi_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CHAR:
        gdi_char((uint32_t)wparam);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        unsigned code = (lparam >> 16) & 0x7f;

        // Injected input (accessibility tools, remote desktops) may carry no scancode.
        if (!code) code = MapVirtualKey((UINT)wparam, MAPVK_VK_TO_VSC) & 0x7f;

        // Print screen and pause do not arrive as plain set-1 codes.
        int key = wparam == VK_SNAPSHOT ? ORB_KEY_PRINT_SCREEN
                  : wparam == VK_PAUSE  ? ORB_KEY_PAUSE
                                        : gdi_keys[code + ((lparam >> 24) & 1) * 128];

        if (key) gdi_down[key] = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;

        // DefWindowProc turns WM_SYSKEYDOWN into WM_SYSCOMMAND/SC_CLOSE for Alt+F4.
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
            return DefWindowProc(window, msg, wparam, lparam);

        return 0;
    }
    case WM_SETFOCUS:
        gdi_focused = true;
        return 0;
    case WM_KILLFOCUS:
        memset(gdi_down, 0, sizeof gdi_down);
        gdi_focused = false;
        return 0;
    case WM_SYSCOMMAND:
        // A lone Alt or F10 would otherwise put a window with no menu into the menu loop.
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;

        return DefWindowProc(window, msg, wparam, lparam);
    case WM_SIZE:
        gdi_win.width = LOWORD(lparam);
        gdi_win.height = HIWORD(lparam);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC device_context = BeginPaint(window, &paint);

        if (gdi_last_frame) gdi_blit(device_context, gdi_last_frame);

        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        gdi_closed = true;
        return 0;
    default:
        return DefWindowProc(window, msg, wparam, lparam);
    }
}

bool orb_os_open(const orb_os_config* config) {
    orb_size screen = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int scale = orb_os_open_scale(config->size, screen);

    gdi_fb = config->size;
    gdi_win = (orb_size) {gdi_fb.width * scale, gdi_fb.height * scale};

    WNDCLASS window_class = {
        .lpfnWndProc = gdi_proc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = GetStockObject(BLACK_BRUSH),
        .lpszClassName = L"orb",
    };

    gdi_class = RegisterClass(&window_class);

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
        0, MAKEINTATOM(gdi_class), win32_wide(config->title, title, ORB_PATH_MAX), style, x, y,
        outer_w, outer_h, nullptr, nullptr, window_class.hInstance, nullptr
    );

    if (!gdi_window) {
        orb_log("cannot create the window");
        return false;
    }

    gdi_focused = true;
    ShowWindow(gdi_window, SW_SHOW);

    wasapi_open();

    return true;
}

bool orb_os_pump(orb_input* out) {
    for (MSG msg; PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    memcpy(out->keys, gdi_down, sizeof gdi_down);
    xinput_pump(&out->pad, gdi_focused);
    memcpy(out->text, gdi_text, sizeof gdi_text);
    gdi_text[0] = 0;
    gdi_text_len = 0;
    gdi_high_surrogate = 0;

    return !gdi_closed;
}

void orb_os_present(const uint32_t* rgb) {
    HDC dc = GetDC(gdi_window);

    gdi_last_frame = rgb;
    gdi_blit(dc, rgb);
    ReleaseDC(gdi_window, dc);
}

uint32_t orb_os_key_symbol(int key) {
    int code = orb_os_key_code(gdi_keys, key);

    if (code < 0) return 0;

    UINT scan = code < 128 ? (UINT)code : 0xe000u | (UINT)(code - 128);
    UINT virtual_key = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);
    BYTE state[256] = {0};
    WCHAR text[4];
    // Flag 4 leaves any dead-key state alone.
    int n = ToUnicode(virtual_key, scan, state, text, 4, 4);

    return n == 1 && text[0] > 0x20 ? (uint32_t)text[0] : 0;
}

int orb_os_key_position(uint32_t codepoint) {
    if (codepoint > 0xffff) return ORB_KEY_NONE;

    SHORT scan = VkKeyScan((WCHAR)codepoint);

    if (scan == -1) return ORB_KEY_NONE;

    UINT code = MapVirtualKey((UINT)(scan & 0xff), MAPVK_VK_TO_VSC);

    return code < 128 ? gdi_keys[code] : ORB_KEY_NONE;
}

void orb_os_close(void) {
    wasapi_close();

    DestroyWindow(gdi_window);
    UnregisterClass(MAKEINTATOM(gdi_class), GetModuleHandle(nullptr));
}

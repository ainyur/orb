#pragma once

// Set-1 scancode to HID position; an extended code (E0 prefix) is indexed at 128 + code.
static const uint8_t gdi_keys[256] = {
    [0x01] = 41,  [0x02] = 30,  [0x03] = 31,  [0x04] = 32,  // esc 1 2 3
    [0x05] = 33,  [0x06] = 34,  [0x07] = 35,  [0x08] = 36,  // 4 5 6 7
    [0x09] = 37,  [0x0a] = 38,  [0x0b] = 39,  [0x0c] = 45,  // 8 9 0 -
    [0x0d] = 46,  [0x0e] = 42,  [0x0f] = 43,  [0x10] = 20,  // = backspace tab q
    [0x11] = 26,  [0x12] = 8,   [0x13] = 21,  [0x14] = 23,  // w e r t
    [0x15] = 28,  [0x16] = 24,  [0x17] = 12,  [0x18] = 18,  // y u i o
    [0x19] = 19,  [0x1a] = 47,  [0x1b] = 48,  [0x1c] = 40,  // p [ ] return
    [0x1d] = 224, [0x1e] = 4,   [0x1f] = 22,  [0x20] = 7,   // lctrl a s d
    [0x21] = 9,   [0x22] = 10,  [0x23] = 11,  [0x24] = 13,  // f g h j
    [0x25] = 14,  [0x26] = 15,  [0x27] = 51,  [0x28] = 52,  // k l ; '
    [0x29] = 53,  [0x2a] = 225, [0x2b] = 49,  [0x2c] = 29,  // ` lshift backslash z
    [0x2d] = 27,  [0x2e] = 6,   [0x2f] = 25,  [0x30] = 5,   // x c v b
    [0x31] = 17,  [0x32] = 16,  [0x33] = 54,  [0x34] = 55,  // n m , .
    [0x35] = 56,  [0x36] = 229, [0x37] = 85,  [0x38] = 226, // / rshift kp* lalt
    [0x39] = 44,  [0x3a] = 57,  [0x3b] = 58,  [0x3c] = 59,  // space caps f1 f2
    [0x3d] = 60,  [0x3e] = 61,  [0x3f] = 62,  [0x40] = 63,  // f3 f4 f5 f6
    [0x41] = 64,  [0x42] = 65,  [0x43] = 66,  [0x44] = 67,  // f7 f8 f9 f10
    [0x46] = 71,  [0x47] = 95,  [0x48] = 96,  [0x49] = 97,  // scrolllock kp7 kp8 kp9
    [0x4a] = 86,  [0x4b] = 92,  [0x4c] = 93,  [0x4d] = 94,  // kp- kp4 kp5 kp6
    [0x4e] = 87,  [0x4f] = 89,  [0x50] = 90,  [0x51] = 91,  // kp+ kp1 kp2 kp3
    [0x52] = 98,  [0x53] = 99,  [0x56] = 100, [0x57] = 68,  // kp0 kp. 102nd f11
    [0x58] = 69,                                            // f12
    [0x9c] = 88,  [0x9d] = 228, [0xb5] = 84,  [0xb8] = 230, // kpenter rctrl kp/ ralt
    [0xc5] = 83,  [0xc7] = 74,  [0xc8] = 82,  [0xc9] = 75,  // numlock home up pgup
    [0xcb] = 80,  [0xcd] = 79,  [0xcf] = 77,  [0xd0] = 81,  // left right end down
    [0xd1] = 78,  [0xd2] = 73,  [0xd3] = 76,  [0xdb] = 227, // pgdn insert delete lgui
    [0xdc] = 231, [0xdd] = 101,                             // rgui application
};

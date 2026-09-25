#pragma once

#include "../orb.h"

#include <stdlib.h>
#include <string.h>

// Codes are written as numbers so this header builds on any host, without <linux/input.h>.

// Whether a device's key bitmap, as sysfs prints it, is a gamepad's. The bitmap is hex
// words of 64 bits, most significant first; BTN_SOUTH (0x130) is bit 48 of the fifth word
// from the end and BTN_0 (0x100) bit 0 of the same word. A drawing tablet's pad sets both.
static inline bool evdev_is_pad_bitmap(char* bitmap) {
    char* words[16];
    int count = 0;

    for (char* word = strtok(bitmap, " \n"); word && count < (int)(sizeof words / sizeof *words);
         word = strtok(nullptr, " \n"))
        words[count++] = word;

    if (count < 5) return false;

    unsigned long long bits = strtoull(words[count - 5], nullptr, 16);

    return (bits >> 48 & 1) && !(bits & 1);
}

// The pad button an evdev key code reports. Drivers follow the kernel's positions, except
// that Xbox pads report X on BTN_NORTH and Y on BTN_WEST.
static inline orb_pad evdev_pad_button(int code, orb_pad_make make) {
    bool xbox = make == ORB_PAD_MAKE_XBOX;

    switch (code) {
    case 0x130:
        return ORB_PAD_SOUTH;
    case 0x131:
        return ORB_PAD_EAST;
    case 0x133:
        return xbox ? ORB_PAD_WEST : ORB_PAD_NORTH;
    case 0x134:
        return xbox ? ORB_PAD_NORTH : ORB_PAD_WEST;
    case 0x136:
        return ORB_PAD_LEFT_SHOULDER;
    case 0x137:
        return ORB_PAD_RIGHT_SHOULDER;
    case 0x13a:
        return ORB_PAD_BACK;
    case 0x13b:
        return ORB_PAD_START;
    case 0x13d:
        return ORB_PAD_LEFT_STICK;
    case 0x13e:
        return ORB_PAD_RIGHT_STICK;
    case 0x220:
        return ORB_PAD_UP;
    case 0x221:
        return ORB_PAD_DOWN;
    case 0x222:
        return ORB_PAD_LEFT;
    case 0x223:
        return ORB_PAD_RIGHT;
    default:
        return ORB_PAD_NONE;
    }
}

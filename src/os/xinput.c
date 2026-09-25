#include "../core/log.h"
#include "../core/macros.h"
#include "os.h"

#include <xinput.h>

static const struct {
    WORD bit;
    orb_pad pad;
} xinput_buttons[] = {
    {XINPUT_GAMEPAD_A, ORB_PAD_SOUTH},
    {XINPUT_GAMEPAD_B, ORB_PAD_EAST},
    {XINPUT_GAMEPAD_X, ORB_PAD_WEST},
    {XINPUT_GAMEPAD_Y, ORB_PAD_NORTH},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, ORB_PAD_LEFT_SHOULDER},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, ORB_PAD_RIGHT_SHOULDER},
    {XINPUT_GAMEPAD_BACK, ORB_PAD_BACK},
    {XINPUT_GAMEPAD_START, ORB_PAD_START},
    {XINPUT_GAMEPAD_LEFT_THUMB, ORB_PAD_LEFT_STICK},
    {XINPUT_GAMEPAD_RIGHT_THUMB, ORB_PAD_RIGHT_STICK},
    {XINPUT_GAMEPAD_DPAD_UP, ORB_PAD_UP},
    {XINPUT_GAMEPAD_DPAD_DOWN, ORB_PAD_DOWN},
    {XINPUT_GAMEPAD_DPAD_LEFT, ORB_PAD_LEFT},
    {XINPUT_GAMEPAD_DPAD_RIGHT, ORB_PAD_RIGHT},
};

static int xinput_slot = -1;
static uint64_t xinput_next_scan;

static int16_t xinput_axis(int value) {
    return (int16_t)orb_clamp(value, -32767, 32767);
}

static void xinput_pump(orb_pad_input* out, bool focused) {
    XINPUT_STATE state;

    // Probing an empty slot is slow, so the four are probed once a second.
    if (xinput_slot < 0 && orb_os_ticks() >= xinput_next_scan) {
        xinput_next_scan = orb_os_ticks() + ORB_PAD_SCAN_NS;

        for (DWORD slot = 0; slot < XUSER_MAX_COUNT && xinput_slot < 0; slot++)
            if (XInputGetState(slot, &state) == ERROR_SUCCESS) xinput_slot = (int)slot;

        if (xinput_slot >= 0) orb_log("pad: XInput %d (xbox)", xinput_slot);
    }

    *out = (orb_pad_input) {};

    if (xinput_slot < 0) return;

    if (XInputGetState((DWORD)xinput_slot, &state) != ERROR_SUCCESS) {
        orb_log("pad: XInput %d disconnected", xinput_slot);
        xinput_slot = -1;
        xinput_next_scan = 0;
        return;
    }

    const XINPUT_GAMEPAD* gamepad = &state.Gamepad;

    out->make = ORB_PAD_MAKE_XBOX;

    if (!focused) return;

    for (size_t i = 0; i < sizeof xinput_buttons / sizeof *xinput_buttons; i++)
        out->buttons[xinput_buttons[i].pad - ORB_PAD_NONE] =
            gamepad->wButtons & xinput_buttons[i].bit;

    out->left_stick.x = xinput_axis(gamepad->sThumbLX);
    out->left_stick.y = xinput_axis(-gamepad->sThumbLY);
    out->right_stick.x = xinput_axis(gamepad->sThumbRX);
    out->right_stick.y = xinput_axis(-gamepad->sThumbRY);
    out->left_trigger = (int16_t)(gamepad->bLeftTrigger * 32767 / 255);
    out->right_trigger = (int16_t)(gamepad->bRightTrigger * 32767 / 255);
}

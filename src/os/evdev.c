#include "../core/log.h"
#include "../core/macros.h"
#include "evdev_pad.h"
#include "os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* const evdev_make_names[] = {"none", "other", "xbox", "playstation", "nintendo"};
static const int evdev_axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};

static int evdev_fd = -1;
static uint64_t evdev_next_scan;
static bool evdev_open_failed;
static char evdev_name[64];
static orb_pad_make evdev_make;
static uint8_t evdev_abs_bits[ABS_CNT / 8];
static struct input_absinfo evdev_abs[ABS_HAT0Y + 1]; // each axis's value and range
static bool evdev_buttons[ORB_PAD_BUTTONS];

static bool evdev_bit(const uint8_t* bits, int code) {
    return bits[code / 8] >> code % 8 & 1;
}

static bool evdev_read(const char* node, const char* leaf, char* text, int size) {
    orb_path path;

    snprintf(path, sizeof path, "/sys/class/input/%s/device/%s", node, leaf);

    FILE* file = fopen(path, "r");

    if (!file) return false;

    bool filled = fgets(text, size, file) != nullptr;

    fclose(file);
    return filled;
}

static bool evdev_node_is_pad(const char* node) {
    char bitmap[512];

    return evdev_read(node, "capabilities/key", bitmap, sizeof bitmap) &&
           evdev_is_pad_bitmap(bitmap);
}

static orb_pad_make evdev_node_make(const char* node) {
    char text[16] = "";
    orb_path path;
    orb_path link;

    evdev_read(node, "id/vendor", text, sizeof text);

    unsigned long vendor = strtoul(text, nullptr, 16);

    if (vendor == 0x045e || vendor == 0x28de) return ORB_PAD_MAKE_XBOX;
    if (vendor == 0x054c) return ORB_PAD_MAKE_PLAYSTATION;
    if (vendor == 0x057e) return ORB_PAD_MAKE_NINTENDO;

    snprintf(path, sizeof path, "/sys/class/input/%s/device/device/driver", node);

    ssize_t length = readlink(path, link, sizeof link - 1);

    link[length > 0 ? length : 0] = 0;
    return orb_has_suffix(link, "/xpad") ? ORB_PAD_MAKE_XBOX : ORB_PAD_MAKE_OTHER;
}

// A key code's state: a pad button, or a trigger on a pad with no axis for it.
static void evdev_key(int code, bool down) {
    orb_pad pad = evdev_pad_button(code, evdev_make);

    if (pad != ORB_PAD_NONE) evdev_buttons[pad - ORB_PAD_NONE] = down;
    if (code == BTN_TL2 && !evdev_bit(evdev_abs_bits, ABS_Z)) evdev_abs[ABS_Z].value = down;
    if (code == BTN_TR2 && !evdev_bit(evdev_abs_bits, ABS_RZ)) evdev_abs[ABS_RZ].value = down;
}

// Every button and axis as the device holds them now, at open and after the kernel
// dropped events.
static void evdev_sync(void) {
    uint8_t keys[KEY_CNT / 8] = {};

    ioctl(evdev_fd, EVIOCGKEY(sizeof keys), keys);

    for (int code = BTN_SOUTH; code <= BTN_DPAD_RIGHT; code++)
        evdev_key(code, evdev_bit(keys, code));

    for (size_t i = 0; i < sizeof evdev_axes / sizeof *evdev_axes; i++)
        if (evdev_bit(evdev_abs_bits, evdev_axes[i]))
            ioctl(evdev_fd, EVIOCGABS(evdev_axes[i]), &evdev_abs[evdev_axes[i]]);
}

// An axis across its range: -32767..32767 centered, else 0..32767.
static int16_t evdev_axis(int code, bool centered) {
    const struct input_absinfo* axis = &evdev_abs[code];
    int64_t low = axis->minimum, high = axis->maximum, raw = axis->value;

    if (high <= low) return 0;

    int64_t value = centered ? (2 * raw - low - high) * 32767 / (high - low)
                             : (raw - low) * 32767 / (high - low);

    return (int16_t)orb_clamp(value, centered ? -32767 : 0, 32767);
}

static void evdev_open(const char* node) {
    orb_path path;

    snprintf(path, sizeof path, "/dev/input/%s", node);

    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0) {
        if (!evdev_open_failed) orb_log("pad: cannot open %s: %s", path, strerror(errno));

        evdev_open_failed = true;
        return;
    }

    evdev_fd = fd;
    evdev_make = evdev_node_make(node);
    memset(evdev_name, 0, sizeof evdev_name);
    memset(evdev_abs_bits, 0, sizeof evdev_abs_bits);
    memset(evdev_abs, 0, sizeof evdev_abs);
    ioctl(fd, EVIOCGNAME(sizeof evdev_name - 1), evdev_name);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof evdev_abs_bits), evdev_abs_bits);

    // A trigger with only a button reads 0 or 1 across this range.
    if (!evdev_bit(evdev_abs_bits, ABS_Z)) evdev_abs[ABS_Z].maximum = 1;
    if (!evdev_bit(evdev_abs_bits, ABS_RZ)) evdev_abs[ABS_RZ].maximum = 1;

    evdev_sync();
    orb_log("pad: %s (%s)", evdev_name, evdev_make_names[evdev_make]);
}

static void evdev_scan(void) {
    DIR* dir = opendir("/sys/class/input");

    if (!dir) return;

    for (struct dirent* entry; evdev_fd < 0 && (entry = readdir(dir));)
        if (strncmp(entry->d_name, "event", 5) == 0 && evdev_node_is_pad(entry->d_name))
            evdev_open(entry->d_name);

    closedir(dir);
}

static void evdev_drop(void) {
    orb_log("pad: %s disconnected", evdev_name);
    close(evdev_fd);
    evdev_fd = -1;
    evdev_next_scan = 0;
    memset(evdev_buttons, 0, sizeof evdev_buttons);
}

static void evdev_pump(orb_pad_input* out, bool focused) {
    struct input_event events[32];
    ssize_t bytes = 0;

    if (evdev_fd < 0 && orb_os_ticks() >= evdev_next_scan) {
        evdev_next_scan = orb_os_ticks() + ORB_PAD_SCAN_NS;
        evdev_scan();
    }

    while (evdev_fd >= 0 && (bytes = read(evdev_fd, events, sizeof events)) > 0) {
        for (ssize_t i = 0; i < bytes / (ssize_t)sizeof *events; i++) {
            const struct input_event* event = &events[i];

            // Events after a drop predate what the sync reads, and the sync discards the
            // newer key events still queued.
            if (event->type == EV_SYN && event->code == SYN_DROPPED) {
                evdev_sync();
                break;
            }

            if (event->type == EV_KEY) evdev_key(event->code, event->value != 0);
            if (event->type == EV_ABS && event->code <= ABS_HAT0Y)
                evdev_abs[event->code].value = event->value;
        }
    }

    if (evdev_fd >= 0 && bytes < 0 && errno != EAGAIN) evdev_drop();

    *out = (orb_pad_input) {};

    if (evdev_fd < 0) return;

    out->make = evdev_make;

    if (!focused) return;

    memcpy(out->buttons, evdev_buttons, sizeof evdev_buttons);
    out->buttons[ORB_PAD_UP - ORB_PAD_NONE] |= evdev_abs[ABS_HAT0Y].value < 0;
    out->buttons[ORB_PAD_DOWN - ORB_PAD_NONE] |= evdev_abs[ABS_HAT0Y].value > 0;
    out->buttons[ORB_PAD_LEFT - ORB_PAD_NONE] |= evdev_abs[ABS_HAT0X].value < 0;
    out->buttons[ORB_PAD_RIGHT - ORB_PAD_NONE] |= evdev_abs[ABS_HAT0X].value > 0;
    out->left_stick.x = evdev_axis(ABS_X, true);
    out->left_stick.y = evdev_axis(ABS_Y, true);
    out->right_stick.x = evdev_axis(ABS_RX, true);
    out->right_stick.y = evdev_axis(ABS_RY, true);
    out->left_trigger = evdev_axis(ABS_Z, false);
    out->right_trigger = evdev_axis(ABS_RZ, false);
}

static void evdev_close(void) {
    if (evdev_fd >= 0) close(evdev_fd);

    evdev_fd = -1;
    evdev_next_scan = 0;
}

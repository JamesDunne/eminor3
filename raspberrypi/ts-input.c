#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>
#include <termios.h>
#include <ctype.h>
#include <stdbool.h>
#include <bits/signum.h>
#include <signal.h>

#include "types.h"
#include "hardware.h"
#include "util.h"

#include "ux.h"

#define ts_input "/dev/input/event1"
#define ts_device_name "FT5406 memory based driver"

// Touchscreen input:
int ts_fd = -1;

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

// Initialize touchscreen input:
int ts_init(void) {
    char name[256] = "Unknown";
    unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
    unsigned int type, code;
    int abs[6] = {0};

    // Open touchscreen device:
    // TODO: might not always be event1
    if ((ts_fd = open(ts_input, O_RDONLY | O_NONBLOCK)) < 0) {
        perror("open(" ts_input ")");
        return 8;
    }

    // Get device name:
    ioctl(ts_fd, EVIOCGNAME(sizeof(name)), name);
    fprintf(stderr, "TS device name: %s\n", name);

    // Verify our expectations:
    if (strncmp(ts_device_name, name, 256) != 0) {
        fprintf(stderr, "TS device name does not match expected: \"" ts_device_name "\"");
        return 9;
    }

    // Get absolute coordinate bounds for device:
    memset(bit, 0, sizeof(bit));
    ioctl(ts_fd, EVIOCGBIT(0, EV_MAX), bit[0]);

    if (!test_bit(EV_ABS, bit[0])) {
        fprintf(stderr, "TS device does not support EV_ABS events!\n");
        return 10;
    }

    // Fetch EV_ABS bits:
    ioctl(ts_fd, EVIOCGBIT(EV_ABS, KEY_MAX), bit[EV_ABS]);

    // Read X,Y bounds:
    int ts_x_min;
    int ts_x_max;

    int ts_y_min;
    int ts_y_max;

    if (!test_bit(ABS_MT_POSITION_X, bit[EV_ABS])) {
        fprintf(stderr, "TS device does not support ABS_MT_POSITION_X!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_X), abs);
    ts_x_min = abs[1];
    ts_x_max = abs[2];

    if (!test_bit(ABS_MT_POSITION_Y, bit[EV_ABS])) {
        fprintf(stderr, "TS device does not support ABS_MT_POSITION_Y!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_Y), abs);
    ts_y_min = abs[1];
    ts_y_max = abs[2];

    ux_ts_update_extents(ts_x_min, ts_x_max, ts_y_min, ts_y_max);

    fprintf(stderr, "TS bounds: X: [%d, %d]; Y: [%d, %d]\n", ts_x_min, ts_x_max, ts_y_min, ts_y_max);

    return 0;
}

void ts_shutdown(void) {
    close(ts_fd);
}

bool ts_poll(void) {
    bool changed = false;
    struct input_event ev;
    size_t size = sizeof(struct input_event);

    while (read(ts_fd, &ev, size) == size) {
        if (ev.type != EV_ABS) continue;

        switch (ev.code) {
            case ABS_MT_POSITION_X:
                ux_ts_update_col(ev.value);
                changed = true;
                break;
            case ABS_MT_POSITION_Y:
                ux_ts_update_row(ev.value);
                changed = true;
                break;
            case ABS_MT_TRACKING_ID:
                ux_ts_update_touching(ev.value != -1);
                changed = true;
                break;
            default:
                break;
        }
    }

    return changed;
}

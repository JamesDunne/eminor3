
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <printf.h>

#include "types.h"

// Bus 001 Device 005: ID 0c45:7404 Microdia
const char *fsw_evdev_name = "/dev/input/by-id/usb-PCsensor_FootSwitch3-F1.8-event-mouse";

int fsw_fd = -1;
u16 fsw_state = 0;

int fsw_init(void) {
    int flags;

    fsw_fd = open(fsw_evdev_name, O_RDONLY);
    if (fsw_fd < 0) {
        return -1;
    }

    // Set to non-blocking mode:
    flags = fcntl(fsw_fd, F_GETFL, 0);
    fcntl(fsw_fd, F_SETFL, flags | O_NONBLOCK);

    // Initialize fsw state:
    fsw_state = 0;

    return 0;
}

u16 fsw_poll(void) {
    struct input_event ev[64];
    ssize_t rd;
    size_t size = sizeof(struct input_event);

    // Check for event data since last read:
    do {
        rd = read(fsw_fd, ev, size * 64);
        if (rd == -1) {
            return fsw_state;
        }

        // TODO: process the events and flip FSW bits
        for (int i = 0; i < rd / size; i++) {
            printf("0x%04X 0x%04X 0x%08X\n", ev[i].type, ev[i].code, ev[i].value);
        }
    } while (rd == size * 64);

    return fsw_state;
}

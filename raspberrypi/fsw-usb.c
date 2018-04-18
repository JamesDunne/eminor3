#include <stdio.h>

#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>

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

#if 0
        // debug code to view event data:
        for (int i = 0; i < rd / size; i++) {
            printf("0x%04X 0x%04X 0x%08X\n", ev[i].type, ev[i].code, ev[i].value);
        }
#endif
        // press left button
        //0x0004 0x0004 0x00070004
        //0x0001 0x001E 0x00000001
        //0x0000 0x0000 0x00000000
        // auto-repeat
        //0x0001 0x001E 0x00000002
        //0x0000 0x0000 0x00000001
        // release left button
        //0x0004 0x0004 0x00070004
        //0x0001 0x001E 0x00000000
        //0x0000 0x0000 0x00000000


        // press middle button
        //0x0004 0x0004 0x00070005
        //0x0001 0x0030 0x00000001
        //0x0000 0x0000 0x00000000
        // auto-repeat
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x0030 0x00000002
        //0x0000 0x0000 0x00000001
        // release middle button
        //0x0004 0x0004 0x00070005
        //0x0001 0x0030 0x00000000
        //0x0000 0x0000 0x00000000

        // press right button
        //0x0004 0x0004 0x00070006
        //0x0001 0x002E 0x00000001
        //0x0000 0x0000 0x00000000
        // auto-repeat
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        //0x0001 0x002E 0x00000002
        //0x0000 0x0000 0x00000001
        // release right button
        //0x0004 0x0004 0x00070006
        //0x0001 0x002E 0x00000000
        //0x0000 0x0000 0x00000000

        // TODO: process the events and flip FSW bits
    } while (rd == size * 64);

    return fsw_state;
}

int led_init(void) {
}

// Set 16 LED states:
void led_set(u16 leds) {
    (void)leds;
}

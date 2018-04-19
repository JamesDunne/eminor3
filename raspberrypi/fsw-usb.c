#include <stdio.h>

#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>

#include "types.h"
#include "hardware.h"

// Use USB PCsensor FootSwitch3-F1.8 as remote footswitch controller:
// P:  Vendor=0c45 ProdID=7404 Rev=00.01
// S:  Manufacturer=PCsensor
// S:  Product=FootSwitch3-F1.8
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
    struct input_event ev;
    ssize_t rd;
    size_t size = sizeof(struct input_event);

    // Check for event data since last read:
    while ((rd = read(fsw_fd, &ev, size)) == size) {
#if 1
        // debug code to view event data:
        printf("0x%04X 0x%04X 0x%08X\n", ev.type, ev.code, ev.value);
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

        switch (ev.type) {
            case 1:
                switch (ev.code) {
                    // Left:
                    case 0x1E:
                        break;
                    // Middle:
                    case 0x30:
                        if (ev.value == 0) {
                            fsw_state &= ~(M_8 << 8u);
                        } else if (ev.value == 1) {
                            fsw_state |= M_8 << 8u;
                        }
                        break;
                    // Right:
                    case 0x2E:
                        if (ev.value == 0) {
                            fsw_state &= ~(M_8);
                        } else if (ev.value == 1) {
                            fsw_state |= M_8;
                        }
                        break;
                }
                break;
            default:
                break;
        }
    };

    return fsw_state;
}

int led_init(void) {
}

// Set 16 LED states:
void led_set(u16 leds) {
    (void)leds;
}

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
#define fsw_evdev_name "/dev/input/by-id/usb-PCsensor_FootSwitch3-F1.8-event-mouse"

int fsw_fd = -1;
u16 fsw_state = 0;

int fsw_init(void) {
    unsigned int rep[2];

    fsw_fd = open(fsw_evdev_name, O_RDONLY | O_NONBLOCK);
    if (fsw_fd < 0) {
        perror("open(" fsw_evdev_name ")");
        return 0;
    }

    // Query repeat rate:
    if (ioctl(fsw_fd, EVIOCGREP, rep) < 0) {
        perror("ioctl EVIOCGREP");
        return 0;
    }

    // rep = {250, 33}. 250 is ms delay before repeat, 33 is ms repeat period.
#if 0
    fprintf(stderr, "rep = {%d, %d}\n", rep[0], rep[1]);
#endif
    rep[0] = 750;
    rep[1] = 100;

    // Set repeat rate:
    if (ioctl(fsw_fd, EVIOCSREP, rep) < 0) {
        perror("ioctl EVIOCSREP");
        return -1;
    }

    // Initialize fsw state:
    fsw_state = 0;

    return 0;
}

u16 fsw_poll(void) {
    struct input_event ev;
    size_t size = sizeof(struct input_event);

    // Return empty footswitch state if device not opened:
    if (fsw_fd < 0) {
        return 0;
    }

    // Clear auto-repeat flags:
    fsw_state &= ~((M_1 | M_2 | M_3) << 8u);

    // Check for event data since last read:
    while (read(fsw_fd, &ev, size) == size) {
#if 0
        // debug code to view event data:
        fprintf(stderr, "0x%04X 0x%04X 0x%08X\n", ev.type, ev.code, ev.value);
#endif

        if (ev.type != EV_KEY) continue;

        switch (ev.code) {
            case 0x1E:
                // Left:
                if (ev.value == 0) {
                    fsw_state &= ~(M_1);
                } else if (ev.value == 1) {
                    fsw_state |= (M_1);
                } else if (ev.value == 2) {
                    // auto-repeat:
                    fsw_state |= (M_1 << 8u);
                }
                break;
            case 0x30:
                // Middle:
                if (ev.value == 0) {
                    fsw_state &= ~(M_2);
                } else if (ev.value == 1) {
                    fsw_state |= (M_2);
                } else if (ev.value == 2) {
                    // auto-repeat:
                    fsw_state |= (M_2 << 8u);
                }
                break;
            case 0x2E:
                // Right:
                if (ev.value == 0) {
                    fsw_state &= ~(M_3);
                } else if (ev.value == 1) {
                    fsw_state |= (M_3);
                } else if (ev.value == 2) {
                    // auto-repeat:
                    fsw_state |= (M_3 << 8u);
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
    (void) leds;
}

#include "types.h"
#include "hardware.h"

int fsw_init(void) {
    return 0;
}

u16 fsw_poll(void) {
    return 0;
}

int led_init(void) {
}

// Set 16 LED states:
void led_set(u16 leds) {
    (void) leds;
}

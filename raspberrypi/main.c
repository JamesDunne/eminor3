#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>         //Used for UART
#include <fcntl.h>          //Used for UART
#include <termios.h>        //Used for UART
#include <time.h>

#ifdef __linux
#include <stdlib.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "types.h"
#include "hardware.h"
#include "midi.h"
#include "fsw.h"
#include "leds.h"
#include "ux.h"

// Hardware interface from controller:
void debug_log(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "DEBUG: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#ifdef HWFEAT_LABEL_UPDATES

// --------------- Change button labels (for Win32 / HTML5 interfaces only):

/* export */ char **label_row_get(u8 row);
/* export */ void label_row_update(u8 row);

#endif

// Main function:
int main(void) {
    int retval;
    struct timespec t;

    t.tv_sec  = 0;
    t.tv_nsec = 1L * 1000000L;  // 1 ms

    if ((retval = midi_init())) {
        return retval;
    }

    if ((retval = fsw_init())) {
        return retval;
    }

    if ((retval = led_init())) {
        return retval;
    }

    if ((retval = ux_init())) {
        return retval;
    }

    // Initialize controller:
    controller_init();

    while (1) {
        // Sleep:
        while (nanosleep(&t, &t));

        // Run timer handler:
        controller_10msec_timer();

        // Run controller code:
        controller_handle();

        // Poll for UX events:
        ux_poll();

        // Redraw the screen if needed:
        ux_draw();
    }
}

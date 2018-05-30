#include <stdio.h>
#include <stdarg.h>
#include <string.h>
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

// Main function:
int main(void) {
    int retval;

    if ((retval = midi_init())) {
        return retval;
    }

    if ((retval = ux_init())) {
        return retval;
    }

    // Initialize controller:
    controller_init();
    controller_update();

    while (1) {
        // Wait for UX events and handle them:
        ux_select();
    }
}

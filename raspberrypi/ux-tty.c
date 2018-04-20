#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "ux.h"

#define tty0 "/dev/tty0"

int tty_fd = -1;
struct winsize tty_win;

// Initialize UX for a tty CUI - open /dev/tty0 for text-mode GUI (CUI) and clear screen:
int ux_init(void) {
    if ((tty_fd = open(tty0, O_WRONLY))) {
        perror("open(" tty0 ")");
        return 6;
    }

    // Fetch tty window size:
    if (ioctl(tty_fd, TIOCGWINSZ, &tty_win)) {
        perror("ioctl(TIOCGWINSZ)");
        return 7;
    }

    printf("%d, %d\n", tty_win.ws_col, tty_win.ws_row);
    printf("%d, %d\n", tty_win.ws_xpixel, tty_win.ws_ypixel);

    return 0;
}

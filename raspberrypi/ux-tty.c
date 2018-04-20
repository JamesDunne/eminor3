#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <termios.h>
#include <ctype.h>
#include <stdlib.h>

#include "types.h"
#include "hardware.h"

#include "ux.h"

#define tty0 "/dev/tty0"

#define ANSI_RIS "\033c"
#define ANSI_CSI "\033["

#define STRING_(s) #s
#define STRING(s) STRING_(s)

int tty_fd = -1;
struct winsize tty_win;

struct termios saved_attributes;

// Resets tty0 to initial state on exit:
void reset_input_mode(void) {
    tcsetattr (tty_fd, TCSANOW, &saved_attributes);
}

void ux_settty(void) {
    struct termios tattr;

    // Save current state of tty0 and restore it at exit:
    tcgetattr (tty_fd, &saved_attributes);
    atexit (reset_input_mode);

    // Disable local echo:
    tcgetattr (tty_fd, &tattr);
    tattr.c_lflag &= ~(ICANON | ECHO);	/* Clear ICANON and ECHO. */
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (tty_fd, TCSAFLUSH, &tattr);
}

// Initialize UX for a tty CUI - open /dev/tty0 for text-mode GUI (CUI) and clear screen:
int ux_init(void) {
    // Open tty0:
    if ((tty_fd = open(tty0, O_WRONLY)) < 0) {
        perror("open(" tty0 ")");
        return 6;
    }

    // Fetch tty window size:
    if (ioctl(tty_fd, TIOCGWINSZ, &tty_win) < 0) {
        perror("ioctl(TIOCGWINSZ)");
        return 7;
    }

    // ws_xpixel, ws_ypixel are 0, 0
    printf("%d, %d\n", tty_win.ws_col, tty_win.ws_row);

    ux_settty();

    // Clear screen:
    write(tty_fd, ANSI_RIS, 2);

    return 0;
}

// Draw UX screen:
void ux_draw(void) {
    int row;

    // Move cursor to position to draw "LCD" text:
    dprintf(tty_fd, ANSI_CSI "%d;%dH", tty_win.ws_row / 2 - LCD_ROWS / 2, tty_win.ws_col / 2 - LCD_COLS / 2);
    for (row = 0; row < LCD_ROWS; row++) {
        // Write LCD text row:
        write(tty_fd, lcd_row_get(row), LCD_COLS);
        // Move back LCD_COLS columns and down one row
        dprintf(tty_fd, ANSI_CSI "B" ANSI_CSI STRING(LCD_COLS) "D");
    }

    //printf("/----------------------\\");
    //printf("| ");
    //printf(" |\n");
    //printf("\\----------------------/");
}
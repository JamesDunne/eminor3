#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>
#include <termios.h>
#include <ctype.h>

#include "types.h"
#include "hardware.h"

#include "ux.h"

#define tty0 "/dev/tty0"

#define ANSI_RIS "\033c"
#define ANSI_CSI "\033["

#define STRING_(s) #s
#define STRING(s) STRING_(s)

// TTY output:
int tty_fd = -1;
struct winsize tty_win;

struct termios saved_attributes;

// Touchscreen input:
#define ts_input "/dev/input/event1"
#define ts_device_name "FT5406 memory based driver"
int ts_fd = -1;

int ts_x_min;
int ts_x_max;

int ts_y_min;
int ts_y_max;

// Resets tty0 to initial state on exit:
void reset_input_mode(void) {
    tcsetattr(tty_fd, TCSANOW, &saved_attributes);

    // Clear screen:
    write(tty_fd, ANSI_RIS, 2);
}

void ux_settty(void) {
    struct termios tattr;

    // Save current state of tty0 and restore it at exit:
    tcgetattr(tty_fd, &saved_attributes);
    atexit(reset_input_mode);

    // Disable local echo:
    tcgetattr(tty_fd, &tattr);
    tattr.c_lflag &= ~(ICANON | ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr(tty_fd, TCSAFLUSH, &tattr);
}

// Initialize tty0 output (main text console on display):
int tty_init(void) {
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
    printf("TTY cols = %d, rows = %d\n", tty_win.ws_col, tty_win.ws_row);

    // Disable echo on tty0:
    ux_settty();

    // Clear screen:
    write(tty_fd, ANSI_RIS, 2);

    return 0;
}

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
    printf("TS device name: %s\n", name);

    // Verify our expectations:
    if (strncmp(ts_device_name, name, 256) != 0) {
        printf("TS device name does not match expected: \"" ts_device_name "\"");
        return 9;
    }

    // Get absolute coordinate bounds for device:
    memset(bit, 0, sizeof(bit));
    ioctl(ts_fd, EVIOCGBIT(0, EV_MAX), bit[0]);

    if (!test_bit(EV_ABS, bit[0])) {
        printf("TS device does not support EV_ABS events!\n");
        return 10;
    }

    // Fetch EV_ABS bits:
    ioctl(ts_fd, EVIOCGBIT(EV_ABS, KEY_MAX), bit[EV_ABS]);

    // Read X,Y bounds:
    if (!test_bit(ABS_MT_POSITION_X, bit[EV_ABS])) {
        printf("TS device does not support ABS_MT_POSITION_X!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_X), abs);
    ts_x_min = abs[1];
    ts_x_max = abs[2];

    if (!test_bit(ABS_MT_POSITION_Y, bit[EV_ABS])) {
        printf("TS device does not support ABS_MT_POSITION_Y!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_Y), abs);
    ts_y_min = abs[1];
    ts_y_max = abs[2];

    printf("TS bounds: X: [%d, %d]; Y: [%d, %d]\n", ts_x_min, ts_x_max, ts_y_min, ts_y_max);

    return 0;
}

// Initialize UX for a tty CUI - open /dev/tty0 for text-mode GUI (CUI) and clear screen:
int ux_init(void) {
    int retval;

    if ((retval = tty_init())) {
        return retval;
    }

    if ((retval = ts_init())) {
        return retval;
    }

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
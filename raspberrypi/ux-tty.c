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
    printf("%d, %d\n", tty_win.ws_col, tty_win.ws_row);

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

#define NAME_ELEMENT(element) [element] = #element

static const char *const absval[6] = {"Value", "Min  ", "Max  ", "Fuzz ", "Flat ", "Resolution "};

static const char *const absolutes[ABS_MAX + 1] = {
        [0 ... ABS_MAX] = NULL,
        NAME_ELEMENT(ABS_X), NAME_ELEMENT(ABS_Y),
        NAME_ELEMENT(ABS_Z), NAME_ELEMENT(ABS_RX),
        NAME_ELEMENT(ABS_RY), NAME_ELEMENT(ABS_RZ),
        NAME_ELEMENT(ABS_THROTTLE), NAME_ELEMENT(ABS_RUDDER),
        NAME_ELEMENT(ABS_WHEEL), NAME_ELEMENT(ABS_GAS),
        NAME_ELEMENT(ABS_BRAKE), NAME_ELEMENT(ABS_HAT0X),
        NAME_ELEMENT(ABS_HAT0Y), NAME_ELEMENT(ABS_HAT1X),
        NAME_ELEMENT(ABS_HAT1Y), NAME_ELEMENT(ABS_HAT2X),
        NAME_ELEMENT(ABS_HAT2Y), NAME_ELEMENT(ABS_HAT3X),
        NAME_ELEMENT(ABS_HAT3Y), NAME_ELEMENT(ABS_PRESSURE),
        NAME_ELEMENT(ABS_DISTANCE), NAME_ELEMENT(ABS_TILT_X),
        NAME_ELEMENT(ABS_TILT_Y), NAME_ELEMENT(ABS_TOOL_WIDTH),
        NAME_ELEMENT(ABS_VOLUME), NAME_ELEMENT(ABS_MISC),
#ifdef ABS_MT_BLOB_ID
NAME_ELEMENT(ABS_MT_TOUCH_MAJOR),
    NAME_ELEMENT(ABS_MT_TOUCH_MINOR),
    NAME_ELEMENT(ABS_MT_WIDTH_MAJOR),
    NAME_ELEMENT(ABS_MT_WIDTH_MINOR),
    NAME_ELEMENT(ABS_MT_ORIENTATION),
    NAME_ELEMENT(ABS_MT_POSITION_X),
    NAME_ELEMENT(ABS_MT_POSITION_Y),
    NAME_ELEMENT(ABS_MT_TOOL_TYPE),
    NAME_ELEMENT(ABS_MT_BLOB_ID),
#endif
#ifdef ABS_MT_TRACKING_ID
        NAME_ELEMENT(ABS_MT_TRACKING_ID),
#endif
#ifdef ABS_MT_PRESSURE
        NAME_ELEMENT(ABS_MT_PRESSURE),
#endif
#ifdef ABS_MT_SLOT
        NAME_ELEMENT(ABS_MT_SLOT),
#endif
#ifdef ABS_MT_TOOL_X
NAME_ELEMENT(ABS_MT_TOOL_X),
    NAME_ELEMENT(ABS_MT_TOOL_Y),
    NAME_ELEMENT(ABS_MT_DISTANCE),
#endif
};

static const char *const *const names[EV_MAX + 1] = {
        [0 ... EV_MAX] = NULL,
        [EV_ABS] = absolutes,
};

static const char *const events[EV_MAX + 1] = {
        [0 ... EV_MAX] = NULL,
        NAME_ELEMENT(EV_SYN), NAME_ELEMENT(EV_KEY),
        NAME_ELEMENT(EV_REL), NAME_ELEMENT(EV_ABS),
        NAME_ELEMENT(EV_MSC), NAME_ELEMENT(EV_LED),
        NAME_ELEMENT(EV_SND), NAME_ELEMENT(EV_REP),
        NAME_ELEMENT(EV_FF), NAME_ELEMENT(EV_PWR),
        NAME_ELEMENT(EV_FF_STATUS), NAME_ELEMENT(EV_SW),
};

static const int maxval[EV_MAX + 1] = {
        [0 ... EV_MAX] = -1,
        [EV_SYN] = SYN_MAX,
        [EV_KEY] = KEY_MAX,
        [EV_REL] = REL_MAX,
        [EV_ABS] = ABS_MAX,
        [EV_MSC] = MSC_MAX,
        [EV_SW] = SW_MAX,
        [EV_LED] = LED_MAX,
        [EV_SND] = SND_MAX,
        [EV_REP] = REP_MAX,
        [EV_FF] = FF_MAX,
        [EV_FF_STATUS] = FF_STATUS_MAX,
};

static void print_absdata(int fd, int axis) {
    int abs[6] = {0};
    int k;

    ioctl(fd, EVIOCGABS(axis), abs);
    for (k = 0; k < 6; k++)
        if ((k < 3) || abs[k])
            printf("      %s %6d\n", absval[k], abs[k]);
}

static inline const char *typename(unsigned int type) {
    return (type <= EV_MAX && events[type]) ? events[type] : "?";
}

static inline const char *codename(unsigned int type, unsigned int code) {
    return (type <= EV_MAX && code <= maxval[type] && names[type] && names[type][code]) ? names[type][code] : "?";
}

// Initialize touchscreen input:
int ts_init(void) {
    char name[256] = "Unknown";
    unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
    unsigned int type, code;

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
        printf("Device name does not match expected: \"" ts_device_name "\"");
        return 9;
    }

    // Get absolute coordinate bounds for device:
    memset(bit, 0, sizeof(bit));
    ioctl(ts_fd, EVIOCGBIT(0, EV_MAX), bit[0]);

    for (type = 0; type < EV_MAX; type++) {
        if (test_bit(type, bit[0]) && type != EV_REP) {
            printf("  Event type %d (%s)\n", type, typename(type));
            if (type == EV_SYN) continue;

            ioctl(ts_fd, EVIOCGBIT(type, KEY_MAX), bit[type]);
            for (code = 0; code < KEY_MAX; code++)
                if (test_bit(code, bit[type])) {
                    printf("    Event code %d (%s)\n", code, codename(type, code));
                    if (type == EV_ABS)
                        print_absdata(ts_fd, code);
                }
        }
    }


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
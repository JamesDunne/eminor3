#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>
#include <termios.h>
#include <ctype.h>
#include <stdbool.h>

#include "types.h"
#include "hardware.h"

#include "ux.h"

//#define tty0 "/dev/tty0"
#define tty0 "/dev/pts/0"

#define ts_input "/dev/input/event1"
#define ts_device_name "FT5406 memory based driver"

#define ANSI_RIS "\033c"
#define ANSI_CSI "\033["

#define STRING_(s) #s
#define STRING(s) STRING_(s)

#define STRLEN(s) (sizeof(s) - 1)

// TTY output:
int tty_fd = -1;
struct winsize tty_win;

struct termios saved_attributes;

// Touchscreen input:
int ts_fd = -1;

int ts_x_min;
int ts_x_max;

int ts_y_min;
int ts_y_max;

bool ts_touching = false;
bool ts_released = false;
int ts_x, ts_col;
int ts_y, ts_row;

bool ux_redraw = true;

struct report ux_report;


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
    fprintf(stderr, "TTY cols = %d, rows = %d\n", tty_win.ws_col, tty_win.ws_row);

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
    fprintf(stderr, "TS device name: %s\n", name);

    // Verify our expectations:
    if (strncmp(ts_device_name, name, 256) != 0) {
        fprintf(stderr, "TS device name does not match expected: \"" ts_device_name "\"");
        return 9;
    }

    // Get absolute coordinate bounds for device:
    memset(bit, 0, sizeof(bit));
    ioctl(ts_fd, EVIOCGBIT(0, EV_MAX), bit[0]);

    if (!test_bit(EV_ABS, bit[0])) {
        fprintf(stderr, "TS device does not support EV_ABS events!\n");
        return 10;
    }

    // Fetch EV_ABS bits:
    ioctl(ts_fd, EVIOCGBIT(EV_ABS, KEY_MAX), bit[EV_ABS]);

    // Read X,Y bounds:
    if (!test_bit(ABS_MT_POSITION_X, bit[EV_ABS])) {
        fprintf(stderr, "TS device does not support ABS_MT_POSITION_X!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_X), abs);
    ts_x_min = abs[1];
    ts_x_max = abs[2];

    if (!test_bit(ABS_MT_POSITION_Y, bit[EV_ABS])) {
        fprintf(stderr, "TS device does not support ABS_MT_POSITION_Y!\n");
    }
    ioctl(ts_fd, EVIOCGABS(ABS_MT_POSITION_Y), abs);
    ts_y_min = abs[1];
    ts_y_max = abs[2];

    fprintf(stderr, "TS bounds: X: [%d, %d]; Y: [%d, %d]\n", ts_x_min, ts_x_max, ts_y_min, ts_y_max);

    return 0;
}

bool ts_handle_input_event(struct input_event *ev);

bool mouse_poll();

bool ts_handle_input_event(struct input_event *ev) {
    bool changed;
    switch ((*ev).code) {
        case ABS_MT_POSITION_X:
            ts_x = (*ev).value;
            ts_col = ((*ev).value - ts_x_min) / ((ts_x_max - ts_x_min) / tty_win.ws_col);
            changed = true;
            break;
        case ABS_MT_POSITION_Y:
            ts_y = (*ev).value;
            ts_row = ((*ev).value - ts_y_min) / ((ts_y_max - ts_y_min) / tty_win.ws_row);
            changed = true;
            break;
        case ABS_MT_TRACKING_ID:
            ts_touching = ((*ev).value != -1);
            changed = true;
            break;
        default:
            break;
    }
    return changed;
}

bool ts_poll(void) {
    bool changed = false;
    struct input_event ev;
    size_t size = sizeof(struct input_event);

    while (read(ts_fd, &ev, size) == size) {
        if (ev.type != EV_ABS) continue;

        changed = ts_handle_input_event(&ev);
    }

#if 1
    if (changed) {
        fprintf(stderr, "TS (%d, %d)\n", ts_row, ts_col);
    }
#endif

    return changed;
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

    // set stdin to non-blocking so we can read mouse events:
    fcntl(0, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);

    // enable xterm mouse reporting:
    write(STDOUT_FILENO, ANSI_CSI "?1003h", STRLEN(ANSI_CSI "?1003h"));

    return 0;
}

// Poll for UX inputs:
bool mouse_poll() {
    bool changed = false;
    // `ESC` `[` `M` bxy
    char buf[7];
    size_t size = 7u;
    ssize_t n;

    // read mouse events from stdin:
    while ((n = read(STDIN_FILENO, buf, size)) > 0) {
        char *esc = strchr(buf, '\033');
        if (esc == NULL) continue;

        // NOTE: assume we have the full mouse report message in `buf` and that it doesn't cross into the next buffer.
        if (esc + 6 > buf + n) {
            fprintf(stderr, "buffer not large enough to contain full mouse report!\n");
            continue;
        }

        if (esc[1] != '[') continue;
        if (esc[2] != 'M') continue;
        // b encodes what mouse button was pressed or released combined with keyboard modifiers.
        char b = esc[3] - (char) 0x20;
        char x = esc[4] - (char) 0x20;
        char y = esc[5] - (char) 0x20;

        fprintf(stderr, "MOUSE: b=%d x=%d y=%d\n", (int) b, (int) x, (int) y);

        // generate input_events for touchscreen compatibility:
        struct input_event ev;
        ev.type = EV_ABS;
        ev.code = ABS_MT_TRACKING_ID;
        ev.value = (b & 3) != 3 ? 1 : -1; // pressed = 1 vs. released = -1
        changed |= ts_handle_input_event(&ev);

        ev.code = ABS_MT_POSITION_X;
        ev.value = (x - 1) * (ts_x_max - ts_x_min) / (tty_win.ws_col) + ts_x_min;
        changed |= ts_handle_input_event(&ev);

        ev.code = ABS_MT_POSITION_Y;
        ev.value = (y - 1) * (ts_y_max - ts_y_min) / (tty_win.ws_row) + ts_y_min;
        changed |= ts_handle_input_event(&ev);
    }

    return changed;
}

bool ux_poll(void) {
    // Poll for touchscreen input:
    bool changed = ts_poll();

    changed |= mouse_poll();

    // Register a redraw if touchscreen input changed:
    if (changed) {
        ux_notify_redraw();
    }

    return changed;
}

void ux_notify_redraw(void) {
    ux_redraw = true;
}

// Hand back the global variable location to write reports to:
struct report *report_target(void) {
    return &ux_report;
}

// When a new report is ready, redraw the UX:
void report_notify(void) {
    ux_notify_redraw();
}

#define LCD_ANSI_NEXT_ROW ANSI_CSI "B" ANSI_CSI STRING(LCD_COLS) "D"

int ansi_move_cursor(char *buf, int row, int col) {
    return sprintf(buf, ANSI_CSI "%d;%dH", row + 1, col + 1);
}

int ansi_move_cursor_row(char *buf, int row) {
    return sprintf(buf, ANSI_CSI "%dd", row + 1);
}

int ansi_move_cursor_col(char *buf, int col) {
    return sprintf(buf, ANSI_CSI "%dG", col + 1);
}

int ansi_clear_screen(char *buf) {
    return sprintf(buf, ANSI_RIS);
}

int ansi_erase_cols(char *buf, int cols) {
    return sprintf(buf, ANSI_CSI "%dX", cols);
}

// Draw UX screen:
void ux_draw(void) {
    // Only redraw if necessary:
    if (!ux_redraw) return;
    ux_redraw = false;

#ifdef HWFEAT_REPORT
    // Prefer report feature for rendering a UX:
    char out[1000] = "";
    char *buf = out;

    static bool last_ts_touching = false;
    static bool song_drop_down = false;

    bool ts_pressed = !last_ts_touching && ts_touching;

    // Show program name at top as a drop-down menu:
    // Tap to drop-down song list:
    if (!song_drop_down) {
        if (ts_pressed && (ts_row == 0) && (ts_col >= 6 && ts_col <= 29)) {
            // Show the drop-down:
            song_drop_down = true;
        }
    } else {
        if (ts_pressed && (ts_row >= 0 && ts_row <= 10) && (ts_col >= 6 && ts_col <= 29)) {
            // TODO: record which row started a drag, if any.
            // TODO: close drop-down if no drag and released on same row as pressed.
            // Close the drop-down:
            song_drop_down = false;
            for (int i = 0; i < 10; i++) {
                buf += ansi_move_cursor(buf, 0 + i, 6);
                // Erase 20+4 characters:
                buf += ansi_erase_cols(buf, REPORT_PR_NAME_LEN + 4);
            }
        }
    }

    if (!song_drop_down) {
        buf += ansi_move_cursor(buf, 0, 0);
        buf += sprintf(
                buf,
                "Song: [%c%-*s ]",
                ux_report.is_modified ? '*' : ' ',
                REPORT_PR_NAME_LEN,
                ux_report.pr_name
        );
    } else if (song_drop_down) {
        // Render drop-down:
        for (int i = 0; i < 10; i++) {
            int pr;
            char name[REPORT_PR_NAME_LEN];
            if (ux_report.is_setlist_mode) {
                pr = get_set_list_program(i);
            } else {
                pr = i;
            }
            get_program_name(pr, name);

            buf += ansi_move_cursor(buf, 0 + i, 6);
            buf += sprintf(buf, "| %-*s |", REPORT_PR_NAME_LEN, name);
        }
    }

    buf += ansi_move_cursor(buf, 0, 31);
    buf += sprintf(buf, "(%s)", ux_report.is_setlist_mode ? "SETLIST" : "PROGRAM");

    // Toggle setlist/program mode on first press:
    if (ts_pressed && (ts_row == 0) && (ts_col >= 31 && ts_col <= 39)) {
        toggle_setlist_mode();
    }

    buf += ansi_move_cursor_col(buf, 43);
    if (ux_report.is_setlist_mode) {
        buf += sprintf(buf, "%3d/%3d", ux_report.sl_val, ux_report.sl_max);
    } else {
        buf += sprintf(buf, "%3d/%3d", ux_report.pr_val, ux_report.pr_max);
    }

    // Move cursor to last touchscreen row,col:
    buf += ansi_move_cursor(buf, ts_row, ts_col);

    // Send update to tty in one write call to reduce lag/tear:
    write(tty_fd, out, buf - out);

    last_ts_touching = ts_touching;
#else
# ifdef FEAT_LCD
    u8 row;
    char lcd[LCD_ROWS * (LCD_COLS + STRLEN(LCD_ANSI_NEXT_ROW)) + STRLEN(ANSI_CSI "99;99H") * 2 + 1] = "";
    char *buf = lcd;

    // Move cursor to position to draw "LCD" text:
    int tty_lcd_row_center = tty_win.ws_row / 2 - LCD_ROWS / 2;
    int tty_lcd_col_center = tty_win.ws_col / 2 - LCD_COLS / 2;
    buf += ansi_move_cursor(buf, tty_lcd_row_center, tty_lcd_col_center);
    for (row = 0; row < LCD_ROWS; row++) {
        // Write LCD text row:
        strncat(buf, lcd_row_get(row), LCD_COLS);
        buf += LCD_COLS;
        // Move back LCD_COLS columns and down one row
        strcat(buf, LCD_ANSI_NEXT_ROW);
        buf += STRLEN(LCD_ANSI_NEXT_ROW);
    }

    // Move cursor to last touchscreen row,col:
    buf += ansi_move_cursor(buf, ts_row, ts_col);

    // Send update to tty in one write call to reduce lag/tear:
    write(tty_fd, lcd, buf - lcd);
# endif
#endif
}

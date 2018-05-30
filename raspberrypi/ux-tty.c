#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdbool.h>
#include <signal.h>

#include <sys/select.h>

#include "types.h"
#include "hardware.h"
#include "util.h"

#include "ux.h"
#include "ts-input.h"
#include "leds.h"
#include "fsw.h"

//#define tty0 "/dev/tty0"
#define tty0 "/dev/stdout"

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
int ux_ts_x_min;
int ux_ts_x_max;

int ux_ts_y_min;
int ux_ts_y_max;

bool ts_touching = false;
int ts_col;
int ts_row;

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

    ux_ts_update_extents(0, tty_win.ws_col, 0, tty_win.ws_row);

    return 0;
}

bool mouse_poll();

int max(int a, int b);

int min(int a, int b);

void ux_ts_update_extents(int x_min, int x_max, int y_min, int y_max) {
    ux_ts_x_min = x_min;
    ux_ts_x_max = x_max;
    ux_ts_y_min = y_min;
    ux_ts_y_max = y_max;
}

void ux_ts_update_row(int y) {
    ts_row = (y - ux_ts_y_min) / ((ux_ts_y_max - ux_ts_y_min) / tty_win.ws_row);
}

void ux_ts_update_col(int x) {
    ts_col = (x - ux_ts_x_min) / ((ux_ts_x_max - ux_ts_x_min) / tty_win.ws_col);
}

void ux_ts_update_touching(bool touching) {
    ts_touching = touching;
}

// Shutdown UX, close files, and restore sane tty:
void ux_shutdown() {
    // disable xterm mouse reporting:
    write(STDOUT_FILENO, ANSI_CSI "?1000l", STRLEN(ANSI_CSI "?1000l"));

    // reset stdin:
    reset_input_mode();

    close(tty_fd);

    ts_shutdown();
}

void ux_shutdown_signal(int signal) {
    (void) signal;

    // shutdown UX:
    ux_shutdown();

    // exit process cleanly:
    exit(0);
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

    // register atexit and SIGINT handler for CTRL-C:
    atexit(ux_shutdown);
    signal(SIGINT, ux_shutdown_signal);

    return 0;
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

bool ux_get_sl_name(int sl_idx, char *name) {
    int pr = get_set_list_program(sl_idx);
    if (pr < 0) return false;

    get_program_name(pr, name);
    return true;
}

bool ux_get_pr_name(int pr_idx, char *name) {
    get_program_name(pr_idx, name);
    return true;
}

struct dd_state {
    bool is_open;

    bool is_dragging;
    int drag_row;
    int drag_offset;

    int rows;
    int list_offset;
    int list_count;

    int item_index;

    bool (*list_item)(int i, char *name);
};

void dd_set_offset(struct dd_state *dd, int i) {
    dd->list_offset = min(max(0, i), max(0, dd->list_count - dd->rows + 1));
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
}

char *ux_hslider_draw(char *buf, int row, int col, int inner_width, int value, int value_max) {
    // Draw meter:
    buf += ansi_move_cursor(buf, row, col);
    buf += sprintf(buf, "[");
    int value_width = value / (value_max / inner_width);
    int value_remainder = value % (value_max / inner_width);
    int c = 0;
    for (c = 0; c < value_width; c++) {
        //buf += sprintf(buf, "\u2588");
        buf += sprintf(buf, "\u25A0");
    }
    for (; c < inner_width; c++) {
        buf += sprintf(buf, " ");
    }
    buf += sprintf(buf, "]");

    return buf;
}

static bool last_ts_touching = false;
static int touched_component = -1;

static struct dd_state dd_song = {
    .is_open = false,
    .rows = 14,
};
static bool ts_pressed = false;
static bool ts_released = false;

void do_callback(void *state) {
    void (*callback)(void) = state;
    callback();
}

void component_pressed_action(int component, int row, int col_min, int col_max, void *state, void (*action)(void *state)) {
    if ((ts_row == row) && (ts_col >= col_min && ts_col <= col_max)) {
        if ((touched_component == -1) && ts_pressed) {
            touched_component = component;
        }
        if (touched_component == component) {
            if (ts_pressed) {
                action(state);
            } else if (ts_released) {
                touched_component = -1;
            }
        }
    }
}

void component_touching_action(int component, int row, int col_min, int width, void *state, void (*action)(void *state)) {
    if ((ts_col >= col_min) && (ts_col <= col_min + width)) {
        if ((touched_component == -1) && ts_pressed && (ts_row == row)) {
            touched_component = component;
        }
        if (touched_component == component) {
            if (ts_touching) {
                action(state);
            } else if (ts_released) {
                touched_component = -1;
            }
        }
    }
}

void volume_slider_touching(void *state) {
    int a = (int)state;
    int new_volume = min(127, max(0, (ts_col - 12) * (128 / 32)));
    volume_set(a, (u8) new_volume);
}

void gain_slider_touching(void *state) {
    int a = (int)state;
    int new_gain = min(127, max(0, (ts_col - 12) * (128 / 32)));
    gain_set(a, (u8) new_gain);
}

// Draw UX screen:
void ux_draw(void) {
    // Only redraw if necessary:
    if (!ux_redraw) return;
    ux_redraw = false;

    // Prefer report feature for rendering a UX:
    char out[2500] = "";
    char *buf = out;

    ts_pressed = !last_ts_touching && ts_touching;
    ts_released = last_ts_touching && !ts_touching;

    int component = 0;

    // Show program name at top as a drop-down menu:
    // Tap to drop-down song list:
    if (!dd_song.is_open) {
        if (ts_released && (ts_row == 0) && (ts_col >= 6 && ts_col <= 29)) {
            // Show the drop-down:
            dd_song.is_open = true;
            if (ux_report.is_setlist_mode) {
                dd_song.item_index = ux_report.sl_val - 1;
                dd_song.list_item = ux_get_sl_name;
                dd_song.list_count = ux_report.sl_max - 1;
                dd_set_offset(&dd_song, dd_song.item_index - (dd_song.rows / 2));
            } else {
                dd_song.item_index = ux_report.pr_val - 1;
                dd_song.list_item = ux_get_pr_name;
                dd_song.list_count = ux_report.pr_max - 1;
                dd_set_offset(&dd_song, dd_song.item_index - (dd_song.rows / 2));
            }
        }
    } else {
        bool closing = false;

        if ((ts_row == 0 && ts_row <= 1 + dd_song.rows) && (ts_col >= 6 && ts_col <= 29)) {
            if (ts_released) {
                // Close the drop-down and cancel:
                closing = true;
            }
        } else if ((ts_row >= 1 && ts_row <= 1 + dd_song.rows) && (ts_col >= 6 && ts_col <= 29)) {
            if (ts_pressed) {
                // Record which row started a drag, if any.
                dd_song.drag_row = ts_row;
            } else if (ts_released) {
                // Close drop-down if no drag and released on same row as pressed.
                if (!dd_song.is_dragging) {
                    // Close the drop-down:
                    closing = true;
                    // Select current item:
                    dd_song.item_index = dd_song.list_offset + (ts_row - 1);
                    if (ux_report.is_setlist_mode) {
                        activate_song(dd_song.item_index);
                    } else {
                        activate_program(dd_song.item_index);
                    }
                } else {
                    // Stop dragging:
                    dd_song.is_dragging = false;
                    dd_song.drag_row = -1;
                }
            } else if (ts_touching) {
                if (dd_song.drag_row != ts_row) {
                    // Start dragging:
                    if (!dd_song.is_dragging) {
                        dd_song.is_dragging = true;
                        dd_song.drag_offset = dd_song.list_offset;
                    }
                    // Adjust list offset based on drag distance:
                    dd_set_offset(&dd_song, dd_song.drag_offset + (dd_song.drag_row - ts_row));
                }
            }
        }

        if (closing) {
            // Close the drop-down and erase dirty bit:
            dd_song.is_open = false;
            for (int i = 0; i < dd_song.rows; i++) {
                buf += ansi_move_cursor(buf, 1 + i, 6);
                // Erase 20+4 characters:
                buf += ansi_erase_cols(buf, REPORT_PR_NAME_LEN + 4);
            }
        }
    }

    // Render song drop-down control:
    buf += ansi_move_cursor(buf, 0, 0);
    buf += sprintf(
            buf,
            "Song: [%c%-*s ]",
            ux_report.is_modified ? '*' : ' ',
            REPORT_PR_NAME_LEN,
            ux_report.pr_name
    );

    // Setlist/program toggle button:
    buf += ansi_move_cursor(buf, 0, 31);
    buf += sprintf(buf, "(%s)", ux_report.is_setlist_mode ? "SETLIST" : "PROGRAM");

    // Toggle setlist/program mode on first press:
    component_pressed_action(component, 0, 31, 39, toggle_setlist_mode, do_callback);
    component++;

    if (!dd_song.is_open) {
        // Show song/program index:
        buf += ansi_move_cursor_col(buf, 43);
        if (ux_report.is_setlist_mode) {
            buf += sprintf(buf, "%3d/%3d", ux_report.sl_val, ux_report.sl_max);
        } else {
            buf += sprintf(buf, "%3d/%3d", ux_report.pr_val, ux_report.pr_max);
        }

        // Show second status line:
        buf += ansi_move_cursor(buf, 1, 0);
        buf += sprintf(buf, "Scene: %2d/%2d", ux_report.sc_val, ux_report.sc_max);
        buf += ansi_move_cursor(buf, 1, 13);
        buf += sprintf(buf, "(PREV) (NEXT)");
        component_pressed_action(component, 1, 13, 19, prev_scene, do_callback);
        component++;
        component_pressed_action(component, 1, 21, 26, next_scene, do_callback);
        component++;

        buf += ansi_move_cursor(buf, 1, 49 - 18);
        buf += sprintf(buf, "%3dbpm", ux_report.tempo);

#define AMP_UX_ROWS 6

        // Render each amp dialog:
        for (int a = 0; a < 2; a++) {
            int row = 2 + (a * AMP_UX_ROWS);
            struct amp_report amp = ux_report.amp[a];

            // Draw horizontal slider box for volume:
            buf += ansi_move_cursor(buf, row, 0);
            buf += sprintf(buf, "Volume: %3d", amp.volume);
            buf = ux_hslider_draw(buf, row, 12, 32, amp.volume + 1, 128);
            component_touching_action(component, row, 12, 32, (void *)a, volume_slider_touching);
            component++;

            // Draw horizontal slider box for gain:
            ++row;
            buf += ansi_move_cursor(buf, row, 0);
            buf += sprintf(buf, "Gain:   %3d", amp.gain[amp.tone]);
            buf = ux_hslider_draw(buf, row, 12, 32, amp.gain[amp.tone] + 1, 128);
            component_touching_action(component, row, 12, 32, (void *)a, gain_slider_touching);
            component++;

            ++row;
            for (int fx = 0; fx < FX_COUNT; fx++) {
                if (amp.fx_enabled[fx]) {
                    buf += sprintf(buf, ANSI_CSI"7m");
                }
                const char *fxName = fx_name(amp.fx_midi_cc[fx]);
                buf += ansi_move_cursor(buf, row, fx * (5+4));
                buf += sprintf(buf, "[ %.4s ]", fxName);
                if (amp.fx_enabled[fx]) {
                    buf += sprintf(buf, ANSI_CSI"0m");
                }
                //component_pressed_action(component, row, fx * (5+4), fx * (5+4) + 7, );
                component++;
            }
            ++row;
            buf += ansi_move_cursor(buf, row, 0);
            ++row;
            buf += ansi_move_cursor(buf, row, 0);
            ++row;
            buf += ansi_move_cursor(buf, row, 0);
        }
    } else {
        // Render drop-down list on top:
        for (int i = 0; i < dd_song.rows; i++) {
            char name[REPORT_PR_NAME_LEN];
            int item_index = i + dd_song.list_offset;
            dd_song.list_item(item_index, name);

            buf += ansi_move_cursor(buf, 1 + i, 6);
            if (item_index == dd_song.item_index) {
                buf += sprintf(buf, "\u2503" ANSI_CSI"7m" " %-*s " ANSI_CSI"0m" "\u2503", REPORT_PR_NAME_LEN, name);
            } else {
                buf += sprintf(buf, "\u2503 %-*s \u2503", REPORT_PR_NAME_LEN, name);
            }
        }
    }

    // Move cursor to last touchscreen row,col:
    buf += ansi_move_cursor(buf, ts_row, ts_col);

    //fprintf(stderr, "%lu\n", buf - out);

    // Send update to tty in one write call to reduce lag/tear:
    write(tty_fd, out, buf - out);

    // Clear touched component after release:
    if (ts_released && (touched_component != -1)) {
        touched_component = -1;
    }
    last_ts_touching = ts_touching;
}

// Poll for UX inputs:
bool mouse_poll() {
    bool changed = false;
    // `ESC` `[` `M` bxy
    char buf[6];
    ssize_t n;

    // read mouse events from stdin:
    while ((n = read(STDIN_FILENO, buf, 1)) == 1) {
        if (buf[0] != '\033') continue;

        n = read(STDIN_FILENO, buf, 5);
        if (n != 5) {
            fprintf(stderr, "Not enough bytes to complete mouse report from read\n");
            continue;
        }

        if (buf[0] != '[') continue;
        if (buf[1] != 'M') continue;
        // b encodes what mouse button was pressed or released combined with keyboard modifiers.
        unsigned char b = (unsigned char) buf[2] - (unsigned char) 0x20;
        unsigned char x = (unsigned char) buf[3] - (unsigned char) 0x20;
        unsigned char y = (unsigned char) buf[4] - (unsigned char) 0x20;

        //fprintf(stderr, "MOUSE: b=%d x=%d y=%d\n", (int) b, (int) x, (int) y);

        // generate input_events for touchscreen compatibility:
        ux_ts_update_touching((b & 3) != 3); // pressed = 1 vs. released = -1
        ux_ts_update_col((x - 1) * (ux_ts_x_max - ux_ts_x_min) / (tty_win.ws_col) + ux_ts_x_min);
        ux_ts_update_row((y - 1) * (ux_ts_y_max - ux_ts_y_min) / (tty_win.ws_row) + ux_ts_y_min);
        changed = true;
    }

    return changed;
}

int mouse_register(int nfds, fd_set *rfds) {
    FD_SET(STDIN_FILENO, rfds);
    if (STDIN_FILENO + 1 >= nfds) { nfds = STDIN_FILENO + 1; }

    return nfds;
}

bool mouse_has_events(fd_set *rfds) {
    return FD_ISSET(STDIN_FILENO, rfds) != 0;
}

// Wait for UX events:
void ux_select(void) {
    bool updated = false;
    fd_set rfds;
    int nfds = 0;

    // Get the UX report from the controller:
    report_fill(&ux_report);

    // Draw the screen if needed:
    ux_draw();

    // Register file descriptors for select():
    FD_ZERO(&rfds);
    nfds = mouse_register(nfds, &rfds);
    nfds = ts_register(nfds, &rfds);
    nfds = fsw_register(nfds, &rfds);

    // Wait for an event:
    if (select(nfds, &rfds, NULL, NULL, NULL) < 0) {
        perror("select");
    }

    // Check mouse events:
    if (mouse_has_events(&rfds)) {
        updated |= mouse_poll();
    }

    // Check touchscreen events:
    if (ts_has_events(&rfds)) {
        updated |= ts_poll();
    }

    // TODO: footswitch events:
    if (fsw_has_events(&rfds)) {
        fsw_poll();
    }

    // Update controller state:
    updated |= controller_update();

    // If controller state was modified, refresh the UX report to render the UI from:
    if (updated) {
        // Request a screen redraw next time around:
        ux_redraw = true;
    }
}
